#include "SyncOnAddress.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace PortPS5::Kernel::SyncOnAddress {

namespace {

// Interval between guest signal polling ticks during prolonged address wait operations.
constexpr uint32_t SIGNAL_POLL_MICROS = 10000;

using Clock = std::chrono::steady_clock;

// Invariant: The wait address must be non-null and naturally aligned to alignof(T) (4 or 8 bytes).
template <typename T>
[[nodiscard]] bool IsValidWaitAddress(const volatile T* address) {
    return address != nullptr && (reinterpret_cast<uintptr_t>(address) & (alignof(T) - 1u)) == 0;
}

// Invariant: Wake address must be non-null and aligned to at least 4 bytes.
[[nodiscard]] bool IsValidWakeAddress(const volatile void* address) {
    return address != nullptr &&
           (reinterpret_cast<uintptr_t>(address) & (alignof(uint32_t) - 1u)) == 0;
}

// Reads word with acquire memory semantics to prevent instruction reordering across the check.
template <typename T>
[[nodiscard]] T ReadWord(const volatile T* address) {
    return __atomic_load_n(address, __ATOMIC_ACQUIRE);
}

// Internal representation of finite or indefinite wait deadlines.
struct WaitDeadline {
    bool finite = false;
    Clock::time_point end{};
};

// Constructs a deadline from nanosecond duration, safely clamped against steady_clock overflow.
[[nodiscard]] WaitDeadline MakeDeadline(std::chrono::nanoseconds timeout) {
    const auto now = Clock::now();
    const auto remaining = Clock::time_point::max() - now;
    return {true, timeout >= remaining ? Clock::time_point::max() : now + timeout};
}

// Constructs a deadline from microsecond pointer (nullptr means infinite wait).
[[nodiscard]] WaitDeadline MakeDeadline(const uint32_t* timeout_micros) {
    if (timeout_micros == nullptr) {
        return {};
    }
    return MakeDeadline(std::chrono::microseconds(*timeout_micros));
}

// Calculates the time slice for the current wait loop iteration, bounded by SIGNAL_POLL_MICROS.
[[nodiscard]] uint32_t GetWaitSliceMicros(const WaitDeadline& deadline, bool first_wait) {
    if (!deadline.finite) {
        return SIGNAL_POLL_MICROS;
    }

    const auto now = Clock::now();
    if (now >= deadline.end) {
        return first_wait ? 0u : UINT32_MAX;
    }

    const auto remaining =
        std::chrono::ceil<std::chrono::microseconds>(deadline.end - now).count();
    return static_cast<uint32_t>(std::min<int64_t>(remaining, SIGNAL_POLL_MICROS));
}

// Helper to invoke the signal polling callback safely.
void PollSignals(signal_poll_func_t signal_poll) {
    if (signal_poll != nullptr) {
        signal_poll();
    }
}

// Per-waiter condition variable and wake state flag.
struct PortableWaiter {
    std::condition_variable condition;
    bool wake_requested = false;
};

// Per-address registry entry tracking active waiters on a given memory address.
struct PortableAddressEntry {
    std::mutex mutex;
    std::list<PortableWaiter*> waiters;
};

// Global address registry mapping monitored addresses to active waiter queues.
struct PortableAddressRegistry {
    std::mutex mutex;
    std::unordered_map<uintptr_t, std::shared_ptr<PortableAddressEntry>> entries;
};

PortableAddressRegistry& GetPortableRegistry() {
    static PortableAddressRegistry registry;
    return registry;
}

// Registers a waiter into the address registry.
// Locking invariant: Acquires registry.mutex FIRST, then entry->mutex SECOND.
// Holding registry.mutex while pushing into waiters guarantees that concurrent unregister/cleanup
// cannot erase the slot between allocation and waiter registration, preventing orphaned waiters.
std::shared_ptr<PortableAddressEntry> RegisterPortableWaiter(volatile void* address,
                                                             PortableWaiter* waiter) {
    auto& registry = GetPortableRegistry();
    std::lock_guard registry_lock(registry.mutex);
    auto& slot = registry.entries[reinterpret_cast<uintptr_t>(address)];
    if (!slot) {
        slot = std::make_shared<PortableAddressEntry>();
    }
    std::lock_guard entry_lock(slot->mutex);
    slot->waiters.push_back(waiter);
    return slot;
}

// Unregisters a waiter from the address registry upon wake or timeout.
// Locking invariant: Follows the same strict registry.mutex-then-entry->mutex lock hierarchy.
void UnregisterPortableWaiter(volatile void* address,
                              const std::shared_ptr<PortableAddressEntry>& entry,
                              PortableWaiter* waiter) {
    auto& registry = GetPortableRegistry();
    std::lock_guard registry_lock(registry.mutex);
    std::lock_guard entry_lock(entry->mutex);
    entry->waiters.remove(waiter);
    if (entry->waiters.empty()) {
        const auto it = registry.entries.find(reinterpret_cast<uintptr_t>(address));
        if (it != registry.entries.end() && it->second == entry) {
            registry.entries.erase(it);
        }
    }
}

// Executes sliced condition variable wait loop with periodic signal polling and value re-checking.
template <typename T>
int WaitPortable(volatile T* address, T expected, const WaitDeadline& deadline,
                 signal_poll_func_t signal_poll) {
    PortableWaiter waiter;
    auto entry = RegisterPortableWaiter(const_cast<T*>(address), &waiter);
    bool first_wait = true;
    int result = OK;

    std::unique_lock lock(entry->mutex);
    // Spurious wakeup & value change loop: continue waiting while address value matches expected
    // and no wake was explicitly targeted to this waiter.
    while (ReadWord(address) == expected && !waiter.wake_requested) {
        const auto slice_micros = GetWaitSliceMicros(deadline, first_wait);
        if (slice_micros == UINT32_MAX || slice_micros == 0) {
            result = KERNEL_ERROR_ETIMEDOUT;
            break;
        }

        waiter.condition.wait_for(lock, std::chrono::microseconds(slice_micros));
        // Drop entry lock momentarily during signal polling to prevent blocking concurrent wakers
        lock.unlock();
        PollSignals(signal_poll);
        lock.lock();

        if (deadline.finite && Clock::now() >= deadline.end && ReadWord(address) == expected &&
            !waiter.wake_requested) {
            result = KERNEL_ERROR_ETIMEDOUT;
            break;
        }
        first_wait = false;
    }
    lock.unlock();

    UnregisterPortableWaiter(const_cast<T*>(address), entry, &waiter);
    return result;
}

// Wakes up to 'count' waiters registered at the specified address.
int WakePortable(volatile void* address, int32_t count) {
    if (count == 0) {
        // Zero count wake is a valid no-op in PS5 semantics
        return OK;
    }
    auto& registry = GetPortableRegistry();
    std::shared_ptr<PortableAddressEntry> entry;
    {
        std::lock_guard registry_lock(registry.mutex);
        const auto it = registry.entries.find(reinterpret_cast<uintptr_t>(address));
        if (it == registry.entries.end()) {
            return OK; // No registered waiters
        }
        entry = it->second;
    }

    std::lock_guard entry_lock(entry->mutex);
    int32_t remaining = count;
    for (auto* waiter : entry->waiters) {
        if (!waiter->wake_requested) {
            waiter->wake_requested = true;
            waiter->condition.notify_one();
            if (remaining != INT32_MAX && --remaining == 0) {
                break;
            }
        }
    }
    return OK;
}

// Common parameter verification and dispatch implementation for Wait32/Wait64.
template <typename T>
int WaitImpl(volatile T* address, T expected, const WaitDeadline& deadline,
             signal_poll_func_t signal_poll) {
    if (!IsValidWaitAddress(address)) {
        return KERNEL_ERROR_EINVAL;
    }

    int result = WaitPortable(address, expected, deadline, signal_poll);
    PollSignals(signal_poll);
    return result;
}

} // namespace

int Wait32(volatile uint32_t* address, uint32_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll) {
    return WaitImpl(address, expected, MakeDeadline(timeout_micros), signal_poll);
}

int Wait64(volatile uint64_t* address, uint64_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll) {
    return WaitImpl(address, expected, MakeDeadline(timeout_micros), signal_poll);
}

int Wait64(volatile uint64_t* address, uint64_t expected, std::chrono::nanoseconds timeout,
           signal_poll_func_t signal_poll) {
    return WaitImpl(address, expected, MakeDeadline(timeout), signal_poll);
}

int Wake(volatile void* address, int32_t count) {
    if (!IsValidWakeAddress(address) || count < 0) {
        return KERNEL_ERROR_EINVAL;
    }

    return WakePortable(address, count);
}

} // namespace PortPS5::Kernel::SyncOnAddress
