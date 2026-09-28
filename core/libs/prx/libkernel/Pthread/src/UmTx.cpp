// _umtx_op implementation for PortPS5 libkernel.
// Implements FreeBSD _umtx_op operations on in-place futex words using FutexCore.

#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

// _umtx_op (public FreeBSD semantics, docs/spec/threading.md Target design).
// Why here: _umtx_op shares the futex core with mutex/cond/rwlock. WAIT of
// size 8/4 maps to WaitOnAddress; WAKE maps to wake-N; MUTEX_* uses the
// 32-bit umutex owner word with UMUTEX_CONTESTED as bit 31; an unknown op
// returns EINVAL and is logged once per op (not Unsupported, because the
// caller passes a meaningful op number we simply do not implement yet).
// Ordering mirrors the mutex path: CONTESTED is set before sleeping, the
// unlock publishes before waking, and every wait re-checks.

namespace {

// FreeBSD UMTX_OP_* numbers (sys/umtx.h, public header values).
constexpr int kOpLock = 0;
constexpr int kOpUnlock = 1;
constexpr int kOpWait = 2;    // long (8-byte) wait.
constexpr int kOpWake = 3;
constexpr int kOpMutexTrylock = 4;
constexpr int kOpMutexLock = 5;
constexpr int kOpMutexUnlock = 6;
constexpr int kOpSetCeiling = 7;
constexpr int kOpCvWait = 8;
constexpr int kOpCvSignal = 9;
constexpr int kOpCvBroadcast = 10;
constexpr int kOpWaitUint = 11;  // 4-byte wait.
constexpr int kOpRwRdlock = 12;
constexpr int kOpRwWrlock = 13;
constexpr int kOpRwUnlock = 14;
constexpr int kOpWaitUintPrivate = 15;
constexpr int kOpWakePrivate = 16;
constexpr int kOpMutexWait = 17;
constexpr int kOpMutexWake = 18;
constexpr int kOpSemWait = 19;
constexpr int kOpSemWake = 20;
constexpr int kOpNwakePrivate = 21;
constexpr int kOpMutexLock2 = 22;
constexpr int kOpSem2Wait = 23;
constexpr int kOpSem2Wake = 24;

// 32-bit umutex owner word:
// bit 31: CONTESTED (UMUTEX_CONTESTED)
// bit 30: RB_OWNERDEAD (UMUTEX_RB_OWNERDEAD)
// bit 29: RB_NOTRECOV  (UMUTEX_RB_NOTRECOV)
// low 24: owner tid (1..0xFFFFFF)
constexpr std::uint32_t kUmutexContested = 1u << 31;
constexpr std::uint32_t kUmutexRbOwnerDead = 1u << 30;
constexpr std::uint32_t kUmutexRbNotRecov = 1u << 29;
constexpr std::uint32_t kUmutexOwnerMask = 0xFFFFFFu;

// FreeBSD struct _umtx_time from <sys/_umtx.h>
struct UmtxTime {
    KernelTimespec timeout;
    std::uint32_t flags;   // UMTX_ABSTIME = 0x01
    std::uint32_t clockid; // CLOCK_REALTIME = 0, CLOCK_MONOTONIC = 4, etc.
};

constexpr std::uint32_t kUmtxAbstime = 0x01;

int ParseUmtxTimeout(const void* uaddr, const void* uaddr2, std::uint64_t& outDeadline) noexcept {
    outDeadline = FutexCore::kInfinite;
    const void* timePtr = nullptr;
    std::size_t size = 0;
    if (uaddr2 != nullptr) {
        timePtr = uaddr2;
        size = reinterpret_cast<std::uintptr_t>(uaddr);
    } else if (uaddr != nullptr) {
        timePtr = uaddr;
        size = sizeof(UmtxTime);
    } else {
        return SyncWords::kSceOk;
    }

    if (!timePtr)
        return SyncWords::kSceOk;

    KernelTimespec ts{};
    std::uint32_t flags = 0;
    std::uint32_t clockid = 0;

    if (size >= sizeof(UmtxTime)) {
        const auto* ut = static_cast<const UmtxTime*>(timePtr);
        ts = ut->timeout;
        flags = ut->flags;
        clockid = ut->clockid;
    } else {
        const auto* kts = static_cast<const KernelTimespec*>(timePtr);
        ts = *kts;
        flags = 0;
        clockid = 0;
    }

    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
        return SyncWords::kSceEinval;

    const bool isMono = (clockid == 4 || clockid == 5 || clockid == 12);

    if ((flags & kUmtxAbstime) != 0) {
        outDeadline = FutexCore::AbsoluteToDeadline(ts.tv_sec, ts.tv_nsec, isMono);
    } else {
        constexpr std::uint64_t kMaxSec = (UINT64_MAX - 1000000000ULL) / 1000000000ULL;
        if (static_cast<std::uint64_t>(ts.tv_sec) > kMaxSec) {
            outDeadline = FutexCore::kInfinite;
        } else {
            const std::uint64_t relNanos = static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
                                           static_cast<std::uint64_t>(ts.tv_nsec);
            const std::uint64_t now = FutexCore::NowNanos();
            if (UINT64_MAX - now <= relNanos)
                outDeadline = FutexCore::kInfinite;
            else
                outDeadline = now + relNanos;
        }
    }
    return SyncWords::kSceOk;
}

int UmutexLock(std::uint32_t* word, std::uint32_t tid, std::uint64_t deadline,
               bool isUmutexStruct) noexcept {
    if (!word)
        return SyncWords::kSceEinval;
    std::atomic_ref<std::uint32_t> ref(*word);
    while (true) {
        const std::uint32_t w = ref.load(std::memory_order_acquire);
        if ((w & kUmutexRbNotRecov) != 0)
            return SyncWords::kSceEnotrecoverable;
        if ((w & kUmutexRbOwnerDead) != 0) {
            std::uint32_t expected = w;
            const std::uint32_t want = (w & kUmutexContested) | (tid & kUmutexOwnerMask);
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return SyncWords::kSceEownerdead;
            continue;
        }
        const std::uint32_t owner = w & kUmutexOwnerMask;
        if (owner == 0) {
            std::uint32_t expected = w;
            const std::uint32_t want = (w & kUmutexContested) | (tid & kUmutexOwnerMask);
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return SyncWords::kSceOk;
            continue;
        }
        if (isUmutexStruct) {
            // FreeBSD umutex: m_flags is the second 32-bit word, m_ceilings is third.
            // UMUTEX_ERROR_CHECK = 0x0002, UMUTEX_PRIO_PROTECT = 0x0008.
            const auto* m = static_cast<const std::uint32_t*>(word);
            const std::uint32_t flags = m[1];
            if ((flags & 0x0008u) != 0) {
                // Priority protect: m[2] is m_ceilings[0]. Reject if priority exceeds ceiling.
                const std::uint32_t ceiling = m[2];
                if (ceiling != 0 && ceiling < 700u) {
                    return SyncWords::kSceEinval;
                }
            }
            if (owner == (tid & kUmutexOwnerMask)) {
                if ((flags & 0x0002u) != 0)
                    return SyncWords::kSceEdeadlk;
                // Normal mutex: self-relock waits (per FreeBSD spec).
            }
        }
        if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
            return SyncWords::kSceTimedOut;
        if ((w & kUmutexContested) == 0) {
            std::uint32_t expected = w;
            ref.compare_exchange_strong(expected, w | kUmutexContested, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
        }
        const std::uint32_t expect = ref.load(std::memory_order_acquire);
        // Lost-wakeup guard: if freed before we slept, retry instead of
        // waiting on a free word (unlock already returned, no wake coming).
        if ((expect & kUmutexOwnerMask) == 0)
            continue;
        if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(word), expect,
                                deadline))
            return SyncWords::kSceTimedOut;
    }
}

int UmutexUnlock(std::uint32_t* word, std::uint32_t tid) noexcept {
    if (!word)
        return SyncWords::kSceEinval;
    std::atomic_ref<std::uint32_t> ref(*word);
    while (true) {
        const std::uint32_t w = ref.load(std::memory_order_acquire);
        if ((w & kUmutexOwnerMask) != (tid & kUmutexOwnerMask))
            return SyncWords::kSceEperm;
        const bool hadWaiters = (w & kUmutexContested) != 0;
        const std::uint32_t desired = hadWaiters ? kUmutexContested : 0;
        std::uint32_t expected = w;
        if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            if (hadWaiters)
                FutexCore::WakeSingle(word);
            return SyncWords::kSceOk;
        }
    }
}

int UmutexTrylock(std::uint32_t* word, std::uint32_t tid) noexcept {
    if (!word)
        return SyncWords::kSceEinval;
    std::atomic_ref<std::uint32_t> ref(*word);
    const std::uint32_t w = ref.load(std::memory_order_acquire);
    if ((w & kUmutexRbNotRecov) != 0)
        return SyncWords::kSceEnotrecoverable;
    if ((w & kUmutexRbOwnerDead) != 0) {
        std::uint32_t robustExpected = w;
        const std::uint32_t want = (w & kUmutexContested) | (tid & kUmutexOwnerMask);
        if (ref.compare_exchange_strong(robustExpected, want, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return SyncWords::kSceEownerdead;
        return SyncWords::kSceEbusy;
    }
    const std::uint32_t owner = w & kUmutexOwnerMask;
    if (owner != 0)
        return SyncWords::kSceEbusy;
    std::uint32_t expected = w;
    const std::uint32_t want = (w & kUmutexContested) | (tid & kUmutexOwnerMask);
    if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
        return SyncWords::kSceOk;
    return SyncWords::kSceEbusy;
}

// Log-once per unknown op (cold path only; a small spin-guarded table would
// be a global lock, so use a fixed 64-slot atomic seen-set indexed by hash).
void LogUnknownOnce(int op) noexcept {
    static std::atomic<std::uint64_t> seen[1] = {};
    // Only 0..63 tracked; ops beyond that log every time (still cold).
    if (op < 0 || op >= 64) {
        APS5_LOG_ERR("Unknown _umtx_op %d", op);
        return;
    }
    const std::uint64_t bit = 1ULL << static_cast<unsigned>(op);
    const std::uint64_t prev = seen[0].fetch_or(bit, std::memory_order_relaxed);
    if ((prev & bit) == 0)
        APS5_LOG_ERR("Unknown _umtx_op %d", op);
}

}  // namespace

/**
 * _umtx_op implementation for libkernel.
 * Performs fast userspace locking and futex wait/wake operations.
 *
 * @param obj Target synchronization word or object pointer.
 * @param op Operation code (UMTX_OP_*).
 * @param val Value parameter (expected word value for WAIT, waiter count for WAKE).
 * @param uaddr Optional timeout (KernelTimespec*) or secondary argument.
 * @param uaddr2 Optional secondary argument (_umtx_time* or flags).
 * @return 0 on success, or SCE/POSIX error code.
 */
extern "C" int APS5_VABI _umtx_op_nid_postfix(void* obj, int op, std::uint64_t val, void* uaddr,
                                              void* uaddr2) noexcept {
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0 && (op == kOpMutexLock || op == kOpMutexTrylock || op == kOpLock ||
                     op == kOpMutexLock2 || op == kOpMutexWait))
        return SyncWords::kSceEagain;

    switch (op) {
    case kOpWait: {
        // WAIT compares the 8-byte word against val and sleeps when equal.
        if (!obj)
            return SyncWords::kSceEinval;
        auto* w = static_cast<std::uint64_t*>(obj);
        std::atomic_ref<std::uint64_t> ref(*w);
        if (ref.load(std::memory_order_acquire) != val)
            return SyncWords::kSceEbusy;  // value already changed.
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        while (true) {
            const std::uint64_t expect = ref.load(std::memory_order_acquire);
            if (expect != val)
                return SyncWords::kSceOk;
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
                return SyncWords::kSceTimedOut;
            if (!FutexCore::WaitU64(reinterpret_cast<volatile std::uint64_t*>(w), expect,
                                    deadline))
                return SyncWords::kSceTimedOut;
        }
    }
    case kOpWaitUint:
    case kOpWaitUintPrivate: {
        // WAIT_UINT* compares the 4-byte word against val and sleeps when equal.
        if (!obj)
            return SyncWords::kSceEinval;
        auto* w = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> ref(*w);
        if (ref.load(std::memory_order_acquire) != static_cast<std::uint32_t>(val))
            return SyncWords::kSceEbusy;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        while (true) {
            const std::uint32_t expect = ref.load(std::memory_order_acquire);
            if (expect != static_cast<std::uint32_t>(val))
                return SyncWords::kSceOk;
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
                return SyncWords::kSceTimedOut;
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(w), expect, deadline))
                return SyncWords::kSceTimedOut;
        }
    }
    case kOpWake:
    case kOpWakePrivate: {
        if (!obj)
            return SyncWords::kSceEinval;
        if (val == 0)
            return SyncWords::kSceOk;
        if (val >= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            FutexCore::WakeAll(obj);
            return SyncWords::kSceOk;
        }
        for (std::uint64_t i = 0; i < val; ++i)
            FutexCore::WakeSingle(obj);
        return SyncWords::kSceOk;
    }
    case kOpMutexWake: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto* w = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> ref(*w);
        while (true) {
            const std::uint32_t cur = ref.load(std::memory_order_acquire);
            if ((cur & kUmutexOwnerMask) != 0)
                break;
            if ((cur & kUmutexContested) == 0)
                break;
            std::uint32_t expected = cur;
            if (ref.compare_exchange_strong(expected, 0, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                break;
        }
        FutexCore::WakeSingle(obj);
        return SyncWords::kSceOk;
    }
    case kOpNwakePrivate: {
        if (!obj)
            return SyncWords::kSceEinval;
        if (val == 0)
            return SyncWords::kSceOk;
        auto* addresses = static_cast<void* const*>(obj);
        for (std::uint64_t i = 0; i < val; ++i) {
            if (addresses[i])
                FutexCore::WakeAll(addresses[i]);
        }
        return SyncWords::kSceOk;
    }
    case kOpMutexLock:
    case kOpMutexLock2: {
        if (!obj)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        return UmutexLock(static_cast<std::uint32_t*>(obj), tid, deadline, true);
    }
    case kOpLock: {
        if (!obj)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        return UmutexLock(static_cast<std::uint32_t*>(obj), tid, deadline, false);
    }
    case kOpMutexWait: {
        if (!obj)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        // Wait-only: sleep until mutex is available without acquiring it.
        auto* w = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> ref(*w);
        while (true) {
            const std::uint32_t valCurrent = ref.load(std::memory_order_acquire);
            if ((valCurrent & kUmutexOwnerMask) == 0)
                return SyncWords::kSceOk;
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
                return SyncWords::kSceTimedOut;
            if ((valCurrent & kUmutexContested) == 0) {
                std::uint32_t expected = valCurrent;
                ref.compare_exchange_strong(expected, valCurrent | kUmutexContested,
                                            std::memory_order_acq_rel, std::memory_order_acquire);
            }
            const std::uint32_t expect = ref.load(std::memory_order_acquire);
            if ((expect & kUmutexOwnerMask) == 0)
                return SyncWords::kSceOk;
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(w), expect, deadline))
                return SyncWords::kSceTimedOut;
        }
    }
    case kOpMutexTrylock: {
        if (!obj)
            return SyncWords::kSceEinval;
        return UmutexTrylock(static_cast<std::uint32_t*>(obj), tid);
    }
    case kOpMutexUnlock:
    case kOpUnlock: {
        if (!obj)
            return SyncWords::kSceEinval;
        return UmutexUnlock(static_cast<std::uint32_t*>(obj), tid);
    }
    case kOpSetCeiling: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto* m = static_cast<std::uint32_t*>(obj);
        if (uaddr)
            *static_cast<std::uint32_t*>(uaddr) = m[2];
        m[2] = static_cast<std::uint32_t>(val);
        return SyncWords::kSceOk;
    }
    case kOpCvWait: {
        if (!obj || !uaddr)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(nullptr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        auto* cvWord = static_cast<std::uint32_t*>(obj);
        auto* mutexWord = static_cast<std::uint32_t*>(uaddr);
        std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
        const std::uint32_t seq = cvRef.fetch_add(1, std::memory_order_acq_rel) + 1;
        const int unlockErr = UmutexUnlock(mutexWord, tid);
        if (unlockErr != SyncWords::kSceOk)
            return unlockErr;
        bool timedOut = false;
        while (true) {
            const std::uint32_t cur = cvRef.load(std::memory_order_acquire);
            if (cur != seq)
                break;
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline) {
                timedOut = true;
                break;
            }
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(cvWord), cur, deadline)) {
                timedOut = true;
                break;
            }
        }
        // Per POSIX / FreeBSD, condition waiters always reacquire the mutex before returning.
        const int lockErr = UmutexLock(mutexWord, tid, FutexCore::kInfinite, true);
        if (lockErr != SyncWords::kSceOk)
            return lockErr;
        return timedOut ? SyncWords::kSceTimedOut : SyncWords::kSceOk;
    }
    case kOpCvSignal: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto* cvWord = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
        cvRef.fetch_add(1, std::memory_order_acq_rel);
        FutexCore::WakeSingle(cvWord);
        return SyncWords::kSceOk;
    }
    case kOpCvBroadcast: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto* cvWord = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
        cvRef.fetch_add(1, std::memory_order_acq_rel);
        FutexCore::WakeAll(cvWord);
        return SyncWords::kSceOk;
    }
    case kOpRwRdlock: {
        if (!obj)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        auto* rwWord = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> stateRef(rwWord[0]);
        constexpr std::uint32_t kRwWriteOwner = 0x80000000u;
        constexpr std::uint32_t kRwWriteWaiters = 0x40000000u;
        constexpr std::uint32_t kRwReadWaiters = 0x20000000u;
        constexpr std::uint32_t kRwMaxReaders = 0x1FFFFFFFu;
        constexpr std::uint32_t kRwPreferReader = 0x02u;

        std::uint32_t wrflags = kRwWriteOwner;
        if ((val & kRwPreferReader) == 0)
            wrflags |= kRwWriteWaiters;

        while (true) {
            std::uint32_t state = stateRef.load(std::memory_order_acquire);
            if ((state & wrflags) == 0) {
                if ((state & kRwMaxReaders) == kRwMaxReaders)
                    return SyncWords::kSceEagain;
                std::uint32_t expected = state;
                if (stateRef.compare_exchange_strong(expected, state + 1, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
                    return SyncWords::kSceOk;
                continue;
            }
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
                return SyncWords::kSceTimedOut;
            if ((state & kRwReadWaiters) == 0) {
                std::uint32_t expected = state;
                stateRef.compare_exchange_strong(expected, state | kRwReadWaiters,
                                                 std::memory_order_acq_rel, std::memory_order_acquire);
            }
            const std::uint32_t expect = stateRef.load(std::memory_order_acquire);
            if ((expect & wrflags) == 0)
                continue;
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(rwWord), expect, deadline))
                return SyncWords::kSceTimedOut;
        }
    }
    case kOpRwWrlock: {
        if (!obj)
            return SyncWords::kSceEinval;
        std::uint64_t deadline = FutexCore::kInfinite;
        const int err = ParseUmtxTimeout(uaddr, uaddr2, deadline);
        if (err != SyncWords::kSceOk)
            return err;
        auto* rwWord = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> stateRef(rwWord[0]);
        constexpr std::uint32_t kRwWriteOwner = 0x80000000u;
        constexpr std::uint32_t kRwWriteWaiters = 0x40000000u;

        while (true) {
            std::uint32_t state = stateRef.load(std::memory_order_acquire);
            if (state == 0) {
                std::uint32_t expected = 0;
                if (stateRef.compare_exchange_strong(expected, kRwWriteOwner, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
                    return SyncWords::kSceOk;
                continue;
            }
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
                return SyncWords::kSceTimedOut;
            if ((state & kRwWriteWaiters) == 0) {
                std::uint32_t expected = state;
                stateRef.compare_exchange_strong(expected, state | kRwWriteWaiters,
                                                 std::memory_order_acq_rel, std::memory_order_acquire);
            }
            const std::uint32_t expect = stateRef.load(std::memory_order_acquire);
            if (expect == 0)
                continue;
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(rwWord), expect, deadline))
                return SyncWords::kSceTimedOut;
        }
    }
    case kOpRwUnlock: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto* rwWord = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> stateRef(rwWord[0]);
        constexpr std::uint32_t kRwWriteOwner = 0x80000000u;
        constexpr std::uint32_t kRwWriteWaiters = 0x40000000u;
        constexpr std::uint32_t kRwReadWaiters = 0x20000000u;
        constexpr std::uint32_t kRwMaxReaders = 0x1FFFFFFFu;

        while (true) {
            std::uint32_t state = stateRef.load(std::memory_order_acquire);
            if ((state & kRwWriteOwner) != 0) {
                // Writer release
                std::uint32_t expected = state;
                const std::uint32_t desired = state & (kRwWriteWaiters | kRwReadWaiters);
                if (stateRef.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                    FutexCore::WakeAll(rwWord);
                    return SyncWords::kSceOk;
                }
                continue;
            }
            const std::uint32_t readers = state & kRwMaxReaders;
            if (readers == 0)
                return SyncWords::kSceEperm;
            std::uint32_t expected = state;
            const std::uint32_t desired = state - 1;
            if (stateRef.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                if ((desired & kRwMaxReaders) == 0 && (desired & kRwWriteWaiters) != 0)
                    FutexCore::WakeSingle(rwWord);
                return SyncWords::kSceOk;
            }
        }
    }
    case kOpSemWait:
    case kOpSemWake:
    case kOpSem2Wait:
    case kOpSem2Wake:
        // Logged once, EINVAL for now.
        LogUnknownOnce(op);
        return SyncWords::kSceEinval;
    default:
        LogUnknownOnce(op);
        return SyncWords::kSceEinval;
    }
}
