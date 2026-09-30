// tests/kernel/SyncOnAddressTests.cpp
// Verification test suite for kernel sync-on-address (futex / WaitOnAddress) primitives in PortPS5.
// Verifies atomic wait, timed wait, spurious wakeup handling, and concurrent single/multi-thread wakeups.

#include "common/TestHarness.hpp"
#include "prx/libkernel/Sync/SyncOnAddress.hpp"

#include <atomic>
#include <chrono>
#include <climits>
#include <thread>
#include <vector>

namespace {

using namespace PortPS5::Kernel::SyncOnAddress;

// Global counter tracking periodic signal callback invocations during tests.
std::atomic<int> g_signal_poll_count{0};

// Atomically stores value to test address with release memory order.
template <typename T>
void Store(T* address, T value) {
    __atomic_store_n(address, value, __ATOMIC_RELEASE);
}

// Callback that increments the signal poll counter to verify guest signal delivery checkpoints.
void CountSignalPoll() {
    g_signal_poll_count.fetch_add(1, std::memory_order_relaxed);
}

// Verifies that Wait32, Wait64, and Wake reject null pointers, misaligned addresses, and negative counts.
TEST(SyncOnAddress, InvalidAddress) {
    uint32_t timeout = 1;
    // Null pointers must be rejected with KERNEL_ERROR_EINVAL
    EXPECT_EQ(Wait32(nullptr, 0, &timeout), KERNEL_ERROR_EINVAL);
    EXPECT_EQ(Wait64(nullptr, 0, &timeout), KERNEL_ERROR_EINVAL);
    EXPECT_EQ(Wake(nullptr, 1), KERNEL_ERROR_EINVAL);

    // Misaligned addresses (non-multiples of alignof(T)) must be rejected
    alignas(uint64_t) uint8_t bytes[16] = {};
    auto* misaligned32 = reinterpret_cast<uint32_t*>(bytes + 1);
    EXPECT_EQ(Wait32(misaligned32, 0, &timeout), KERNEL_ERROR_EINVAL);
    EXPECT_EQ(Wait64(reinterpret_cast<uint64_t*>(bytes + 4), 0, &timeout), KERNEL_ERROR_EINVAL);
    EXPECT_EQ(Wake(misaligned32, 1), KERNEL_ERROR_EINVAL);

    // Negative wake counts must be rejected with KERNEL_ERROR_EINVAL
    uint64_t aligned = 0;
    EXPECT_EQ(Wake(&aligned, -1), KERNEL_ERROR_EINVAL);
}

// Verifies that when the memory word value does not match the expected value, wait returns OK immediately.
TEST(SyncOnAddress, MismatchReturnsImmediately) {
    uint32_t word = 7;
    uint64_t word64 = UINT64_C(0x100000000);
    uint32_t timeout = 500000;
    g_signal_poll_count.store(0, std::memory_order_relaxed);

    const auto start = std::chrono::steady_clock::now();
    // Comparing 7 against expected 6: immediate return
    EXPECT_EQ(Wait32(&word, 6, &timeout, CountSignalPoll), OK);
    // Comparing 0x100000000 against expected 0: all 64 bits compared, immediate return
    EXPECT_EQ(Wait64(&word64, 0, &timeout, CountSignalPoll), OK);
    // Fast path must complete well under 100 milliseconds
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    // Verify guest signal poll was still performed on the fast path
    EXPECT_GE(g_signal_poll_count.load(std::memory_order_relaxed), 2);
}

// Verifies that a matching value times out accurately according to specified microseconds.
TEST(SyncOnAddress, Timeout) {
    uint32_t word = 0;
    uint32_t timeout = 20000; // 20 ms
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(Wait32(&word, 0, &timeout), KERNEL_ERROR_ETIMEDOUT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // Must respect minimum duration and stay bounded
    EXPECT_GE(elapsed, std::chrono::milliseconds(10));
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    // Zero timeout polls matching value and immediately returns ETIMEDOUT
    timeout = 0;
    EXPECT_EQ(Wait32(&word, 0, &timeout), KERNEL_ERROR_ETIMEDOUT);
    // Zero timeout on mismatched value returns OK
    word = 1;
    EXPECT_EQ(Wait32(&word, 0, &timeout), OK);
}

// Verifies that modifying the address value followed by a Wake() releases the parked waiter thread.
TEST(SyncOnAddress, ValueChangeAndWake) {
    uint64_t word = 0;
    uint32_t timeout = 1000000; // 1 second
    std::atomic<bool> ready{false};
    int result = KERNEL_ERROR_ETIMEDOUT;

    std::thread waiter([&] {
        ready.store(true, std::memory_order_release);
        result = Wait64(&word, 0, &timeout);
    });
    // Wait until waiter thread is active
    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Change value and wake single waiter
    Store(&word, UINT64_C(0x100000000));
    EXPECT_EQ(Wake(&word, 1), OK);
    waiter.join();
    EXPECT_EQ(result, OK);
}

// Verifies 64-bit nanosecond timeout semantics, deadline preservation, and signal poll invocation.
TEST(SyncOnAddress, NanosecondTimeout) {
    uint64_t word = 0;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(Wait64(&word, 0, std::chrono::milliseconds(1)), KERNEL_ERROR_ETIMEDOUT);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1));
    EXPECT_EQ(Wait64(&word, 0, std::chrono::nanoseconds(0)), KERNEL_ERROR_ETIMEDOUT);
    EXPECT_EQ(Wait64(&word, 1, std::chrono::nanoseconds(0)), OK);

    g_signal_poll_count.store(0, std::memory_order_relaxed);
    std::atomic<bool> returned{false};
    std::thread waker([&] {
        // Wait until waiter has executed multiple signal poll ticks
        while (g_signal_poll_count.load(std::memory_order_relaxed) < 2 &&
               !returned.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Store(&word, UINT64_C(0x100000000));
        EXPECT_EQ(Wake(&word, 1), OK);
    });
    const int result = Wait64(&word, 0, std::chrono::nanoseconds::max(), CountSignalPoll);
    returned.store(true, std::memory_order_release);
    waker.join();
    EXPECT_EQ(result, OK);
    EXPECT_GT(g_signal_poll_count.load(std::memory_order_relaxed), 1);
}

// Verifies granular waiter wakeups: waking 1 waiter, then 2 waiters, then all remaining waiters.
TEST(SyncOnAddress, WakeOneThenAll) {
    constexpr int WAITER_COUNT = 4;
    uint32_t word = 0;
    uint32_t timeout = 1000000;
    std::atomic<int> ready{0};
    std::atomic<int> returned{0};
    int results[WAITER_COUNT] = {};
    std::vector<std::thread> waiters;

    for (int i = 0; i < WAITER_COUNT; i++) {
        waiters.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            results[i] = Wait32(&word, 0, &timeout);
            returned.fetch_add(1, std::memory_order_release);
        });
    }
    while (ready.load(std::memory_order_acquire) != WAITER_COUNT) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Wake 1 waiter
    EXPECT_EQ(Wake(&word, 1), OK);
    const auto one_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (returned.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < one_deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(returned.load(std::memory_order_acquire), 1);

    // Wake 2 more waiters
    EXPECT_EQ(Wake(&word, 2), OK);
    const auto two_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (returned.load(std::memory_order_acquire) < 3 &&
           std::chrono::steady_clock::now() < two_deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(returned.load(std::memory_order_acquire), 3);

    // Wake all remaining waiters (INT_MAX)
    EXPECT_EQ(Wake(&word, INT_MAX), OK);
    for (auto& waiter : waiters) {
        waiter.join();
    }
    EXPECT_EQ(returned.load(std::memory_order_acquire), WAITER_COUNT);
    for (int r : results) {
        EXPECT_EQ(r, OK);
    }
}

// Verifies that waiters on different addresses remain isolated and are not cross-woken.
TEST(SyncOnAddress, AddressesAreIsolated) {
    uint32_t first = 0;
    uint32_t second = 0;
    uint32_t first_timeout = 1000000;
    uint32_t second_timeout = 1000000;
    std::atomic<int> ready{0};
    std::atomic<bool> first_returned{false};
    std::atomic<bool> second_returned{false};
    int first_result = 0;
    int second_result = 0;

    std::thread first_waiter([&] {
        ready.fetch_add(1, std::memory_order_release);
        first_result = Wait32(&first, 0, &first_timeout);
        first_returned.store(true, std::memory_order_release);
    });
    std::thread second_waiter([&] {
        ready.fetch_add(1, std::memory_order_release);
        second_result = Wait32(&second, 0, &second_timeout);
        second_returned.store(true, std::memory_order_release);
    });
    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Waking 'first' must only wake the first waiter
    EXPECT_EQ(Wake(&first, 1), OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(first_returned.load(std::memory_order_acquire));
    EXPECT_FALSE(second_returned.load(std::memory_order_acquire));

    // Waking 'second' wakes the second waiter
    EXPECT_EQ(Wake(&second, 1), OK);

    first_waiter.join();
    second_waiter.join();
    EXPECT_EQ(first_result, OK);
    EXPECT_EQ(second_result, OK);
}

// Stress test verifying that compare-register-wake race conditions never result in lost wakeups.
TEST(SyncOnAddress, CompareRegisterWakeRace) {
    for (int i = 0; i < 50; i++) {
        uint32_t word = 0;
        uint32_t timeout = 500000;
        std::atomic<bool> ready{false};
        int result = KERNEL_ERROR_ETIMEDOUT;
        std::thread waiter([&] {
            ready.store(true, std::memory_order_release);
            result = Wait32(&word, 0, &timeout);
        });
        while (!ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Store(&word, uint32_t{1});
        (void)Wake(&word, 1);
        waiter.join();
        EXPECT_EQ(result, OK);
    }
}

// Verifies that Wake(address, 0) is a valid no-op that wakes no waiters.
TEST(SyncOnAddress, WakeZeroIsNoOp) {
    constexpr int WAITER_COUNT = 2;
    uint32_t word = 0;
    uint32_t timeout = 1000000;
    std::atomic<int> ready{0};
    std::atomic<int> returned{0};
    int results[WAITER_COUNT] = {};
    std::vector<std::thread> waiters;

    for (int i = 0; i < WAITER_COUNT; i++) {
        waiters.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            results[i] = Wait32(&word, 0, &timeout);
            returned.fetch_add(1, std::memory_order_release);
        });
    }
    while (ready.load(std::memory_order_acquire) != WAITER_COUNT) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Zero-count wake: returns OK but releases zero waiters
    EXPECT_EQ(Wake(&word, 0), OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(returned.load(std::memory_order_acquire), 0);

    // Subsequent wake all: releases all waiters cleanly
    EXPECT_EQ(Wake(&word, INT_MAX), OK);
    for (auto& waiter : waiters) {
        waiter.join();
    }
    for (int r : results) {
        EXPECT_EQ(r, OK);
    }
}

// Verifies WaitOnAddress race perturbation under concurrent updates, spurious wakeups, and yields.
TEST(SyncOnAddress, RacePerturbationUnderYield) {
    constexpr int ITERATIONS = 20;
    constexpr int THREADS = 4;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        uint32_t word = 1; // Start odd so workers enter wait condition
        std::atomic<bool> stop{false};
        std::atomic<int> completed{0};
        std::array<std::atomic<int>, THREADS> worker_waits{};
        std::vector<std::thread> workers;

        for (int t = 0; t < THREADS; ++t) {
            workers.emplace_back([&, t] {
                while (!stop.load(std::memory_order_relaxed)) {
                    uint32_t cur = __atomic_load_n(&word, __ATOMIC_ACQUIRE);
                    if (cur % 2 == 1) {
                        uint32_t timeout = 5000; // 5ms
                        int res = Wait32(&word, cur, &timeout);
                        // Result must be either OK (woken or value changed) or ETIMEDOUT
                        EXPECT_TRUE(res == OK || res == KERNEL_ERROR_ETIMEDOUT);
                        worker_waits[t].fetch_add(1, std::memory_order_release);
                    } else {
                        std::this_thread::yield();
                    }
                }
                completed.fetch_add(1, std::memory_order_release);
            });
        }

        // Ensure every worker has completed at least one Wait32 call before perturbation begins
        for (int t = 0; t < THREADS; ++t) {
            while (worker_waits[t].load(std::memory_order_acquire) == 0) {
                // Wake the starting odd word if a worker is parked waiting for initial update
                (void)Wake(&word, INT_MAX);
                std::this_thread::yield();
            }
        }

        // Perturb the address value and issue wakes concurrently
        for (int step = 0; step < 100; ++step) {
            Store(&word, static_cast<uint32_t>(step));
            if (step % 3 == 0) {
                (void)Wake(&word, 1);
            } else if (step % 5 == 0) {
                (void)Wake(&word, INT_MAX);
            }
            std::this_thread::yield();
        }

        stop.store(true, std::memory_order_release);
        Store(&word, 999998u); // Even number to ensure workers break out of loop
        EXPECT_EQ(Wake(&word, INT_MAX), OK);

        for (auto& w : workers) {
            w.join();
        }
        EXPECT_EQ(completed.load(std::memory_order_acquire), THREADS);
        for (int t = 0; t < THREADS; ++t) {
            EXPECT_GT(worker_waits[t].load(std::memory_order_relaxed), 0);
        }
    }
}

} // namespace

