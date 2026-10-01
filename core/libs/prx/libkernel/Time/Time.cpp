// PortPS5 libkernel Time subsystem.
// Implements guest time, clock, and sleep primitives with System V ABI invariants.

#include "prx/libkernel/Time/include/Time.hpp"

#include "prx/libc/include/General.hpp"
#include <cerrno>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
#endif

#ifdef _WIN32
// 0.5ms tick raise, kept unconditionally (threading.md: timer kept).
// Why NtSetTimerResolution: timeBeginPeriod caps at 1ms; the 0.5ms tick needs
// the native 100ns-unit call (5000 = 0.5ms). Loaded dynamically so MinGW
// links without ntdll import hassle; failure falls back to 1ms and never
// changes behaviour via env (no APS5_* switches).
void EnsureTimerTick() noexcept {
    using NtSetFn = LONG(WINAPI*)(ULONG, BOOLEAN, PULONG);
    static const bool done = [] {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto fn = reinterpret_cast<NtSetFn>(GetProcAddress(ntdll, "NtSetTimerResolution"));
            if (fn) {
                ULONG current = 0;
                fn(5000, TRUE, &current);
                return true;
            }
        }
        timeBeginPeriod(1);
        return true;
    }();
    (void)done;
}
#else
inline void EnsureTimerTick() noexcept {}
#endif

static std::uint64_t GetMonotonicNanos() {
#ifdef _WIN32
    static const std::uint64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<std::uint64_t>(f.QuadPart);
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart) * 1000000000ULL / freq;
#else
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

static std::uint64_t GetStartNanos() {
    static const std::uint64_t start = GetMonotonicNanos();
    return start;
}

static void SleepNanos(std::uint64_t nanos) noexcept {
    if (nanos == 0) {
#ifdef _WIN32
        // sceKernelUsleep(0) yields (spec); SwitchToThread hands off to a
        // ready same-priority thread without sleeping.
        EnsureTimerTick();
        SwitchToThread();
#endif
        return;
    }
#ifdef _WIN32
    EnsureTimerTick();
    const std::uint64_t start = GetMonotonicNanos();
    const std::uint64_t target = start + nanos;
    std::uint64_t remainingMillis = nanos / 1000000ULL;
    // Chunk large sleeps into bounded DWORD intervals (max ~24 days per Sleep call)
    // so millis >= 2^32 does not wrap when cast to DWORD.
    constexpr DWORD kMaxChunkMs = 0x7FFFFFFFUL;
    while (remainingMillis > 1) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::uint64_t>(remainingMillis - 1, kMaxChunkMs));
        Sleep(chunk);
        const std::uint64_t now = GetMonotonicNanos();
        if (now >= target)
            break;
        remainingMillis = (target - now) / 1000000ULL;
    }
    while (GetMonotonicNanos() < target) {
        YieldProcessor();
    }
#else
    struct timespec req{};
    req.tv_sec = static_cast<time_t>(nanos / 1000000000ULL);
    req.tv_nsec = static_cast<long>(nanos % 1000000000ULL);
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
#endif
}

extern "C" {

/**
 * @brief sceKernelGetProcessTime implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
std::uint64_t APS5_VABI sceKernelGetProcessTime() noexcept {
    return (GetMonotonicNanos() - GetStartNanos()) / 1000ULL;
}

/**
 * @brief sceKernelGetProcessTimeCounter implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
std::uint64_t APS5_VABI sceKernelGetProcessTimeCounter() noexcept {
    return GetMonotonicNanos() - GetStartNanos();
}

/**
 * @brief sceKernelGetProcessTimeCounterFrequency implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
std::uint64_t APS5_VABI sceKernelGetProcessTimeCounterFrequency() noexcept {
    return 1000000000ULL;
}

/**
 * @brief sceKernelUsleep_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelUsleep_nid_postfix(KernelUseconds microseconds) noexcept {
    SleepNanos(static_cast<std::uint64_t>(microseconds) * 1000ULL);
    return 0;
}

/**
 * @brief usleep_nid_postfix implementation (POSIX alias of sceKernelUsleep).
 * Invoked by guest code using System V ABI calling convention. FreeBSD usleep reports success
 * with 0; there are no signals to interrupt the sleep (no EINTR path) and 0 microseconds yields.
 * @return Always 0.
 */
int APS5_VABI usleep_nid_postfix(KernelUseconds microseconds) noexcept {
    SleepNanos(static_cast<std::uint64_t>(microseconds) * 1000ULL);
    return 0;
}

/**
 * @brief sceKernelNanosleep implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelNanosleep(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept {
    if (rqtp == nullptr) {
        return -1;
    }
    if (rqtp->tv_sec < 0 || rqtp->tv_nsec < 0 || rqtp->tv_nsec >= 1000000000LL) {
        errno = 22;
        return -1;
    }
    SleepNanos(static_cast<std::uint64_t>(rqtp->tv_sec) * 1000000000ULL +
               static_cast<std::uint64_t>(rqtp->tv_nsec));
    if (rmtp != nullptr) {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

/**
 * @brief nanosleep_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI nanosleep_nid_postfix(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept {
    return sceKernelNanosleep(rqtp, rmtp);
}

/**
 * @brief _nanosleep_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI _nanosleep_nid_postfix(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept {
    return sceKernelNanosleep(rqtp, rmtp);
}

/**
 * @brief clock_gettime_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI clock_gettime_nid_postfix(int clockId, KernelTimespec* tp) noexcept {
    if (tp == nullptr) {
        errno = 22;
        return -1;
    }
#ifdef _WIN32
    if (clockId == 0 || clockId == 9) {
        FILETIME ft{};
        GetSystemTimePreciseAsFileTime(&ft);
        std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        t -= 116444736000000000ULL;
        t *= 100ULL;
        tp->tv_sec = static_cast<std::int64_t>(t / 1000000000ULL);
        tp->tv_nsec = static_cast<std::int64_t>(t % 1000000000ULL);
        return 0;
    }
    if (clockId == 10 || clockId == 13) {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        t -= 116444736000000000ULL;
        t *= 100ULL;
        tp->tv_sec = static_cast<std::int64_t>(t / 1000000000ULL);
        tp->tv_nsec = static_cast<std::int64_t>(t % 1000000000ULL);
        return 0;
    }
    if (clockId == 4 || clockId == 1 || clockId == 5 || clockId == 7 || clockId == 8 || clockId == 11 || clockId == 12) {
        std::uint64_t nanos = GetMonotonicNanos();
        tp->tv_sec = static_cast<std::int64_t>(nanos / 1000000000ULL);
        tp->tv_nsec = static_cast<std::int64_t>(nanos % 1000000000ULL);
        return 0;
    }
    errno = 22;
    return -1;
#else
    clockid_t nativeId;
    switch (clockId) {
        case 0:
        case 9:
            nativeId = CLOCK_REALTIME;
            break;
        case 10:
        case 13:
#ifdef CLOCK_REALTIME_COARSE
            nativeId = CLOCK_REALTIME_COARSE;
#else
            nativeId = CLOCK_REALTIME;
#endif
            break;
        case 4:
        case 7:
        case 11:
            nativeId = CLOCK_MONOTONIC;
            break;
        case 5:
        case 8:
        case 12:
            nativeId = CLOCK_MONOTONIC;
            break;
        case 14:
            nativeId = CLOCK_THREAD_CPUTIME_ID;
            break;
        case 15:
            nativeId = CLOCK_PROCESS_CPUTIME_ID;
            break;
        default:
            errno = 22;
            return -1;
    }
    struct timespec ts{};
    if (clock_gettime(nativeId, &ts) != 0) {
        return -1;
    }
    tp->tv_sec = static_cast<std::int64_t>(ts.tv_sec);
    tp->tv_nsec = static_cast<std::int64_t>(ts.tv_nsec);
    return 0;
#endif
}

/**
 * @brief gettimeofday_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI gettimeofday_nid_postfix(KernelTimeval* tv, KernelTimezone* tz) noexcept {
    if (tv == nullptr) {
        errno = 22;
        return -1;
    }
#ifdef _WIN32
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    tv->tv_sec = static_cast<std::int64_t>(t / 10000000ULL);
    tv->tv_usec = static_cast<std::int64_t>((t % 10000000ULL) / 10ULL);
#else
    struct timeval native{};
    if (gettimeofday(&native, nullptr) != 0) {
        return -1;
    }
    tv->tv_sec = static_cast<std::int64_t>(native.tv_sec);
    tv->tv_usec = static_cast<std::int64_t>(native.tv_usec);
#endif
    if (tz != nullptr) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

/**
 * @brief clock_getres_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI clock_getres_nid_postfix(int clockId, KernelTimespec* res) noexcept {
    if (res == nullptr) {
        errno = 22;
        return -1;
    }
#ifdef _WIN32
    if (clockId == 0 || clockId == 9) {
        res->tv_sec = 0;
        res->tv_nsec = 100LL;
        return 0;
    }
    if (clockId == 4 || clockId == 1 || clockId == 5 || clockId == 7 || clockId == 8 || clockId == 10 || clockId == 11 || clockId == 12 || clockId == 13) {
        static const std::uint64_t freq = [] {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return static_cast<std::uint64_t>(f.QuadPart);
        }();
        std::uint64_t nsPerTick = (1000000000ULL + freq - 1ULL) / freq;
        res->tv_sec = 0;
        res->tv_nsec = static_cast<std::int64_t>(nsPerTick);
        return 0;
    }
    errno = 22;
    return -1;
#else
    clockid_t nativeId;
    switch (clockId) {
        case 0:
        case 9:
            nativeId = CLOCK_REALTIME;
            break;
        case 10:
        case 13:
#ifdef CLOCK_REALTIME_COARSE
            nativeId = CLOCK_REALTIME_COARSE;
#else
            nativeId = CLOCK_REALTIME;
#endif
            break;
        case 4:
        case 7:
        case 11:
            nativeId = CLOCK_MONOTONIC;
            break;
        case 5:
        case 8:
        case 12:
            nativeId = CLOCK_MONOTONIC;
            break;
        case 14:
            nativeId = CLOCK_THREAD_CPUTIME_ID;
            break;
        case 15:
            nativeId = CLOCK_PROCESS_CPUTIME_ID;
            break;
        default:
            errno = 22;
            return -1;
    }
    struct timespec ts{};
    if (clock_getres(nativeId, &ts) != 0) {
        return -1;
    }
    res->tv_sec = static_cast<std::int64_t>(ts.tv_sec);
    res->tv_nsec = static_cast<std::int64_t>(ts.tv_nsec);
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Moved as-is (not yet implemented) from the monolithic libkernel/Export.cpp.
// ---------------------------------------------------------------------------

int APS5_VABI sceKernelClockGetres(KernelClockid clock_id, KernelTimespec* tp) {
 (void)clock_id;
 (void)tp;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelClockGettime implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelClockGettime(KernelClockid clock_id, KernelTimespec* tp) {
 (void)clock_id;
 (void)tp;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelConvertLocaltimeToUtc implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelConvertLocaltimeToUtc(int64_t local_time, int64_t reserved, int64_t* utc_time, KernelTimezone* timezone, int32_t* dst_seconds) {
 (void)local_time;
 (void)reserved;
 (void)utc_time;
 (void)timezone;
 (void)dst_seconds;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelConvertUtcToLocaltime implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelConvertUtcToLocaltime(int64_t utc_time, int64_t* local_time, KernelTimesec* st, uint64_t* dst_sec) {
 (void)utc_time;
 (void)local_time;
 (void)st;
 (void)dst_sec;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelGettimeofday implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelGettimeofday(KernelTimeval* tp) {
 (void)tp;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelGettimezone implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI sceKernelGettimezone(KernelTimezone* tz) {
 (void)tz;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelReadTsc implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
uint64_t APS5_VABI sceKernelReadTsc(void) {
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelGetTscFrequency implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
uint64_t APS5_VABI sceKernelGetTscFrequency(void) {
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * @brief sceKernelSleep implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
unsigned int APS5_VABI sceKernelSleep(unsigned int seconds) {
    // Shares sceKernelUsleep's SleepNanos (high-resolution timer, chunked for
    // very long sleeps). Returns the unslept seconds; nothing interrupts a
    // host sleep, so that is always 0. (AnyPS5 c6d098d4.)
    SleepNanos(static_cast<std::uint64_t>(seconds) * 1000000000ULL);
    return 0;
}

}
