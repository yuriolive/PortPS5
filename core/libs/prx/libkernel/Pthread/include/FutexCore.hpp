#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_FUTEXCORE_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_FUTEXCORE_HPP

// Futex core for the M1 threading slice (docs/spec/threading.md Target design).
// Why WaitOnAddress: guest sync words live in place (8-byte slots) and must
// sleep without a process-global mutex. Every wait loops and re-checks the
// word after each return, so spurious wakeups are allowed. Wakes use
// WakeByAddressSingle/All. Deadlines are QPC nanoseconds; waits >=1ms pass
// the millisecond floor of the remaining time, under 1ms the caller loops in
// ~100us slices spinning at most 50us (this replaces PR5's 0.5ms spin).
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

// Convert an absolute KernelTimespec (tv_sec/tv_nsec) to a QPC deadline.
// When monotonic, abstime shares the boot origin with QPC, so it maps
// directly; when realtime, remaining = abstime - wallNow, added to QPC now.
inline std::uint64_t AbsoluteToDeadline(std::int64_t sec, std::int64_t nsec, bool monotonic) noexcept {
    if (sec < 0 || sec > 18446744073LL || nsec < 0 || nsec >= 1000000000LL)
        return 0;  // Invalid or overflow; caller returns EINVAL before waiting.
    const std::uint64_t absNanos =
        static_cast<std::uint64_t>(sec) * 1000000000ULL + static_cast<std::uint64_t>(nsec);
    if (absNanos == kInfinite)
        return kInfinite - 1;  // Reserve kInfinite as the no-deadline sentinel.
    if (monotonic)
        return absNanos;
    const std::uint64_t wall = RealtimeNanos();
    const std::uint64_t now = NowNanos();
    if (absNanos <= wall)
        return now;  // Already expired.
    return now + (absNanos - wall);
}

inline void WakeSingle(volatile void* addr) noexcept {
    WakeByAddressSingle(const_cast<void*>(addr));
}

inline void WakeAll(volatile void* addr) noexcept {
    WakeByAddressAll(const_cast<void*>(addr));
}

// One wait slice. Returns true when the caller must re-check the word
// (woken or slice expired but deadline not yet reached), false only when the
// deadline has passed. The caller always loops and re-checks the word, so a
// spurious TRUE is harmless but a lost wakeup is not: callers set the
// CONTENDED/waiters bit BEFORE waiting and publish with release ordering.
inline bool WaitOnce(volatile void* addr, const void* expected, std::size_t size,
                     std::uint64_t deadline) noexcept {
    if (deadline != kInfinite) {
        const std::uint64_t now = NowNanos();
        if (now >= deadline)
            return false;
        const std::uint64_t remaining = deadline - now;
        if (remaining < 1000000ULL) {
            // Sub-millisecond: spin at most 50us checking for a change,
            // then yield so the outer loop re-checks in ~100us slices.
            // Why 50us: bounds the spin cost while keeping timed-wait error
            // small; the outer re-check loop provides the 100us cadence.
            const std::uint64_t spinStart = now;
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
            if (NowNanos() >= deadline)
                return false;
            SwitchToThread();
            return true;
        }
        // Millisecond floor of the remaining time (spec: floor, not ceil, so
        // we never overshoot the deadline in one slice).
        std::uint64_t ms = remaining / 1000000ULL;
        if (ms > 0xFFFFFFFEULL)
            ms = 0xFFFFFFFEULL;
        const BOOL ok = WaitOnAddress(const_cast<void*>(addr), const_cast<void*>(expected), size,
                                      static_cast<DWORD>(ms));
        if (ok)
            return true;
        // ERROR_TIMEOUT here means the slice expired, not necessarily the
        // deadline: re-check time so the outer loop continues until deadline.
        if (NowNanos() >= deadline)
            return false;
        return true;
    }
    WaitOnAddress(const_cast<void*>(addr), const_cast<void*>(expected), size, INFINITE);
    return true;
}

inline bool WaitU64(volatile std::uint64_t* addr, std::uint64_t expected,
                    std::uint64_t deadline) noexcept {
    return WaitOnce(addr, &expected, 8, deadline);
}

inline bool WaitU32(volatile std::uint32_t* addr, std::uint32_t expected,
                    std::uint64_t deadline) noexcept {
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

inline std::uint64_t AbsoluteToDeadline(std::int64_t sec, std::int64_t nsec, bool monotonic) noexcept {
    if (sec < 0 || sec > 18446744073LL || nsec < 0 || nsec >= 1000000000LL)
        return 0;
    const std::uint64_t absNanos =
        static_cast<std::uint64_t>(sec) * 1000000000ULL + static_cast<std::uint64_t>(nsec);
    if (absNanos == kInfinite)
        return kInfinite - 1;
    if (monotonic)
        return absNanos;
    const std::uint64_t wall = RealtimeNanos();
    const std::uint64_t now = NowNanos();
    if (absNanos <= wall)
        return now;
    return now + (absNanos - wall);
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
                     std::uint64_t deadline) noexcept {
    (void)addr;
    (void)expected;
    (void)size;
    if (deadline != kInfinite && NowNanos() >= deadline)
        return false;
    if (deadline == kInfinite) {
        std::this_thread::yield();
    } else {
        const std::uint64_t now = NowNanos();
        if (now < deadline && deadline - now >= 1000000ULL)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
    if (deadline != kInfinite && NowNanos() >= deadline)
        return false;
    return true;
}

inline bool WaitU64(volatile std::uint64_t* addr, std::uint64_t expected,
                    std::uint64_t deadline) noexcept {
    (void)addr;
    (void)expected;
    return WaitOnce(addr, &expected, 8, deadline);
}

inline bool WaitU32(volatile std::uint32_t* addr, std::uint32_t expected,
                    std::uint64_t deadline) noexcept {
    (void)addr;
    (void)expected;
    return WaitOnce(addr, &expected, 4, deadline);
}
#endif

}  // namespace FutexCore

#endif
