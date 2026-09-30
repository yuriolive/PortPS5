// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_FUTEXCORE_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_FUTEXCORE_HPP

// Futex core for the M1 threading slice (docs/spec/threading.md Target design).
// Why WaitOnAddress: guest sync words live in place (8-byte slots) and must
// sleep without a process-global mutex. Every wait loops and re-checks the
// word after each return, so spurious wakeups are allowed. Wakes use
// WakeByAddressSingle/All. Deadlines are QPC nanoseconds; waits >=1ms pass
// the millisecond floor of the remaining time, under 1ms the caller loops in
// ~100us slices spinning at most 50us (this replaces the 0.5ms spin in AnyPS5 main (merged PR #5)).
// Ordering: all word accesses are 64/32-bit atomics via atomic_ref; the
// expected-value check inside WaitOnAddress plus the re-check loop prevents
// lost wakeups (publish word update before wake, with release/acquire).

#include <atomic>
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace FutexCore {

// Infinite deadline marker (QPC nanos never reach this).
inline constexpr std::uint64_t kInfinite = 0xFFFFFFFFFFFFFFFFULL;
// Max compact guest tid is 2^24-1; tids live in [1, 2^24).
inline constexpr std::uint32_t kMaxTid = 0xFFFFFFu;

#ifdef _WIN32
// Why QPC: QueryPerformanceCounter is the only monotonic nanosecond source
// stable across the 0.5ms tick raise; GetTickCount is too coarse for sub-ms
// timed waits. Frequency is cached once (cold path, benign race is fine).
inline std::uint64_t NowNanos() noexcept {
    static const std::uint64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<std::uint64_t>(f.QuadPart ? f.QuadPart : 1000000000LL);
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const std::uint64_t ticks = static_cast<std::uint64_t>(c.QuadPart);
    return (ticks / freq) * 1000000000ULL + (ticks % freq) * 1000000000ULL / freq;
}

// Wall-clock nanos since the Unix epoch, for REALTIME absolute deadlines.
inline std::uint64_t RealtimeNanos() noexcept {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;  // 1601 -> 1970 in 100ns units.
    return t * 100ULL;
}

// Timeout deadline representation supporting monotonic or realtime clocks.
// For realtime deadlines, targetNanos is the absolute Unix-epoch wall time in nanoseconds;
// re-evaluating RealtimeNanos() >= targetNanos during wait slices ensures that forward
// or backward wall-clock adjustments (e.g. NTP or manual adjustments) are properly honored.
struct Deadline {
    std::uint64_t targetNanos{kInfinite};
    bool isRealtime{false};

    constexpr Deadline() noexcept = default;
    constexpr Deadline(std::uint64_t nanos) noexcept : targetNanos(nanos), isRealtime(false) {}
    constexpr Deadline(std::uint64_t nanos, bool realtime) noexcept
        : targetNanos(nanos), isRealtime(realtime && nanos != kInfinite) {}

    constexpr operator std::uint64_t() const noexcept { return targetNanos; }

    bool IsExpired() const noexcept {
        if (targetNanos == kInfinite)
            return false;
        return isRealtime ? (RealtimeNanos() >= targetNanos) : (NowNanos() >= targetNanos);
    }

    std::uint64_t RemainingNanos() const noexcept {
        if (targetNanos == kInfinite)
            return kInfinite;
        const std::uint64_t cur = isRealtime ? RealtimeNanos() : NowNanos();
        return (cur >= targetNanos) ? 0 : (targetNanos - cur);
    }
};

// Convert an absolute KernelTimespec (tv_sec/tv_nsec) to a Deadline.
// When monotonic, targetNanos is monotonic boot nanoseconds;
// when realtime, targetNanos is Unix-epoch wall nanoseconds, and isRealtime is true.
inline Deadline AbsoluteToDeadline(std::int64_t sec, std::int64_t nsec, bool monotonic) noexcept {
    if (sec < 0 || sec > 18446744073LL || nsec < 0 || nsec >= 1000000000LL)
        return Deadline{0, false};  // Invalid or overflow; caller returns EINVAL before waiting.
    const std::uint64_t absNanos =
        static_cast<std::uint64_t>(sec) * 1000000000ULL + static_cast<std::uint64_t>(nsec);
    if (absNanos == kInfinite)
        return Deadline{kInfinite - 1, !monotonic};
    return Deadline{absNanos, !monotonic};
}

inline void WakeSingle(volatile void* addr) noexcept {
    WakeByAddressSingle(const_cast<void*>(addr));
}

inline void WakeAll(volatile void* addr) noexcept {
    WakeByAddressAll(const_cast<void*>(addr));
}

// Waits on address until woken by WakeByAddress* or value changes, or deadline expires.
// Returns true when woken or value changed; returns false only when the deadline expires.
inline bool WaitOnce(volatile void* addr, const void* expected, std::size_t size,
                     Deadline deadline) noexcept {
    if (deadline.targetNanos == kInfinite) {
        WaitOnAddress(const_cast<void*>(addr), const_cast<void*>(expected), size, INFINITE);
        return true;
    }
    while (true) {
        if (deadline.IsExpired())
            return false;
        const std::uint64_t remaining = deadline.RemainingNanos();
        if (remaining < 1000000ULL) {
            // Sub-millisecond: spin at most 50us checking for a change,
            // then yield so the loop re-checks in ~100us slices.
            const std::uint64_t spinStart = NowNanos();
            if (size == 8) {
                auto* a = reinterpret_cast<std::uint64_t*>(const_cast<void*>(addr));
                const std::uint64_t exp = *static_cast<const std::uint64_t*>(expected);
                std::atomic_ref<std::uint64_t> aref(*a);
                while (NowNanos() - spinStart < 50000ULL) {
                    if (aref.load(std::memory_order_acquire) != exp)
                        return true;
                    YieldProcessor();
                }
                if (aref.load(std::memory_order_acquire) != exp)
                    return true;
            } else {
                auto* a = reinterpret_cast<std::uint32_t*>(const_cast<void*>(addr));
                const std::uint32_t exp = *static_cast<const std::uint32_t*>(expected);
                std::atomic_ref<std::uint32_t> aref(*a);
                while (NowNanos() - spinStart < 50000ULL) {
                    if (aref.load(std::memory_order_acquire) != exp)
                        return true;
                    YieldProcessor();
                }
                if (aref.load(std::memory_order_acquire) != exp)
                    return true;
            }
            if (deadline.IsExpired())
                return false;
            SwitchToThread();
            continue;
        }
        std::uint64_t ms = remaining / 1000000ULL;
        // For realtime waits, clamp wait slice to at most 100ms so forward wall-clock adjustments
        // are detected promptly.
        if (deadline.isRealtime && ms > 100)
            ms = 100;
        if (ms > 0xFFFFFFFEULL)
            ms = 0xFFFFFFFEULL;
        const BOOL ok = WaitOnAddress(const_cast<void*>(addr), const_cast<void*>(expected), size,
                                      static_cast<DWORD>(ms));
        if (ok)
            return true;  // Woken by WakeByAddress* or value changed.
        if (deadline.IsExpired())
            return false;  // Deadline expired.
    }
}

inline bool WaitU64(volatile std::uint64_t* addr, std::uint64_t expected,
                    Deadline deadline) noexcept {
    return WaitOnce(addr, &expected, 8, deadline);
}

inline bool WaitU32(volatile std::uint32_t* addr, std::uint32_t expected,
                    Deadline deadline) noexcept {
    return WaitOnce(addr, &expected, 4, deadline);
}

#else  // Non-Windows fallback (hosted CI): polling with yield; correctness
       // comes from the re-check loop, not the sleep precision.

inline std::uint64_t NowNanos() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

inline std::uint64_t RealtimeNanos() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

inline Deadline AbsoluteToDeadline(std::int64_t sec, std::int64_t nsec, bool monotonic) noexcept {
    if (sec < 0 || sec > 18446744073LL || nsec < 0 || nsec >= 1000000000LL)
        return Deadline{0, false};
    const std::uint64_t absNanos =
        static_cast<std::uint64_t>(sec) * 1000000000ULL + static_cast<std::uint64_t>(nsec);
    if (absNanos == kInfinite)
        return Deadline{kInfinite - 1, !monotonic};
    return Deadline{absNanos, !monotonic};
}

inline void WakeSingle(volatile void* addr) noexcept {
    (void)addr;
    std::this_thread::yield();
}

inline void WakeAll(volatile void* addr) noexcept {
    (void)addr;
    std::this_thread::yield();
}

inline bool WaitOnce(volatile void* addr, const void* expected, std::size_t size,
                     Deadline deadline) noexcept {
    (void)addr;
    (void)expected;
    (void)size;
    if (deadline.IsExpired())
        return false;
    if (deadline.targetNanos == kInfinite) {
        std::this_thread::yield();
    } else {
        const std::uint64_t remaining = deadline.RemainingNanos();
        if (remaining >= 1000000ULL)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
    if (deadline.IsExpired())
        return false;
    return true;
}

inline bool WaitU64(volatile std::uint64_t* addr, std::uint64_t expected,
                    Deadline deadline) noexcept {
    (void)addr;
    (void)expected;
    return WaitOnce(addr, &expected, 8, deadline);
}

inline bool WaitU32(volatile std::uint32_t* addr, std::uint32_t expected,
                    Deadline deadline) noexcept {
    (void)addr;
    (void)expected;
    return WaitOnce(addr, &expected, 4, deadline);
}
#endif

}  // namespace FutexCore

#endif
