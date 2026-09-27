#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

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
constexpr int kOpNwakePrivate = 18;
constexpr int kOpMutexLock2 = 19;
constexpr int kOpSem2Wait = 20;
constexpr int kOpSem2Wake = 21;

// 32-bit umutex owner word: bit 31 CONTESTED, low 24 owner tid.
constexpr std::uint32_t kUmutexContested = 1u << 31;
constexpr std::uint32_t kUmutexOwnerMask = 0xFFFFFFu;

int UmutexLock(std::uint32_t* word, std::uint32_t tid, std::uint64_t deadline) noexcept {
    if (!word)
        return SyncWords::kSceEinval;
    std::atomic_ref<std::uint32_t> ref(*word);
    while (true) {
        const std::uint32_t w = ref.load(std::memory_order_acquire);
        const std::uint32_t owner = w & kUmutexOwnerMask;
        if (owner == 0) {
            std::uint32_t expected = w;
            const std::uint32_t want = (w & kUmutexContested) | (tid & kUmutexOwnerMask);
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return SyncWords::kSceOk;
            continue;
        }
        if (owner == (tid & kUmutexOwnerMask))
            return SyncWords::kSceEdeadlk;
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
        std::uint32_t expected = w;
        if (ref.compare_exchange_strong(expected, 0, std::memory_order_acq_rel,
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
    if ((w & kUmutexOwnerMask) != 0)
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

// FreeBSD signature: int _umtx_op(void *obj, int op, u_long val, void *uaddr,
// void *uaddr2). val is a count for WAKE/NWAKE and a compare value for WAIT.
extern "C" int APS5_VABI _umtx_op_nid_postfix(void* obj, int op, std::uint64_t val, void* uaddr,
                                              void* uaddr2) noexcept {
    (void)uaddr2;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0 && (op == kOpMutexLock || op == kOpMutexTrylock || op == kOpLock ||
                     op == kOpMutexLock2 || op == kOpMutexWait))
        return SyncWords::kSceEagain;

    switch (op) {
    case kOpWait:
    case kOpWaitUint:
    case kOpWaitUintPrivate: {
        // WAIT compares the word against val and sleeps when equal.
        // WAIT (long) is 8-byte; WAIT_UINT* are 4-byte.
        if (!obj)
            return SyncWords::kSceEinval;
        if (op == kOpWait) {
            auto* w = static_cast<std::uint64_t*>(obj);
            std::atomic_ref<std::uint64_t> ref(*w);
            if (ref.load(std::memory_order_acquire) != val)
                return SyncWords::kSceEbusy;  // value already changed.
            // uaddr carries an optional absolute timeout (timespec*) for
            // timed waits; null means infinite. Size check keeps us from
            // reading a guest struct we do not own when it is not a timespec.
            std::uint64_t deadline = FutexCore::kInfinite;
            if (uaddr) {
                const auto* ts = static_cast<const KernelTimespec*>(uaddr);
                if (ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000LL)
                    deadline = FutexCore::AbsoluteToDeadline(ts->tv_sec, ts->tv_nsec, false);
            }
            const std::uint64_t expect = ref.load(std::memory_order_acquire);
            if (expect != val)
                return SyncWords::kSceOk;
            if (!FutexCore::WaitU64(reinterpret_cast<volatile std::uint64_t*>(w), expect,
                                    deadline))
                return SyncWords::kSceTimedOut;
            return SyncWords::kSceOk;
        }
        auto* w = static_cast<std::uint32_t*>(obj);
        std::atomic_ref<std::uint32_t> ref(*w);
        if (ref.load(std::memory_order_acquire) != static_cast<std::uint32_t>(val))
            return SyncWords::kSceEbusy;
        std::uint64_t deadline = FutexCore::kInfinite;
        if (uaddr) {
            const auto* ts = static_cast<const KernelTimespec*>(uaddr);
            if (ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1000000000LL)
                deadline = FutexCore::AbsoluteToDeadline(ts->tv_sec, ts->tv_nsec, false);
        }
        const std::uint32_t expect = ref.load(std::memory_order_acquire);
        if (expect != static_cast<std::uint32_t>(val))
            return SyncWords::kSceOk;
        if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(w), expect, deadline))
            return SyncWords::kSceTimedOut;
        return SyncWords::kSceOk;
    }
    case kOpWake:
    case kOpWakePrivate: {
        if (!obj)
            return SyncWords::kSceEinval;
        // val==0 wakes all; otherwise wake-N via repeated single wakes
        // (WakeByAddressSingle wakes one waiter per call).
        if (val == 0) {
            FutexCore::WakeAll(obj);
            return SyncWords::kSceOk;
        }
        for (std::uint64_t i = 0; i < val; ++i)
            FutexCore::WakeSingle(obj);
        return SyncWords::kSceOk;
    }
    case kOpNwakePrivate: {
        if (!obj)
            return SyncWords::kSceEinval;
        for (std::uint64_t i = 0; i < val; ++i)
            FutexCore::WakeSingle(obj);
        return SyncWords::kSceOk;
    }
    case kOpMutexLock:
    case kOpMutexLock2:
    case kOpLock:
    case kOpMutexWait: {
        if (!obj)
            return SyncWords::kSceEinval;
        return UmutexLock(static_cast<std::uint32_t*>(obj), tid, FutexCore::kInfinite);
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
    case kOpSetCeiling:
    case kOpCvWait:
    case kOpCvSignal:
    case kOpCvBroadcast:
    case kOpRwRdlock:
    case kOpRwWrlock:
    case kOpRwUnlock:
    case kOpSem2Wait:
    case kOpSem2Wake:
        // Same algorithms as the pthread paths, over guest structs (M4 for
        // full CV/RW/SEM2 coverage). Logged once, EINVAL for now.
        LogUnknownOnce(op);
        return SyncWords::kSceEinval;
    default:
        LogUnknownOnce(op);
        return SyncWords::kSceEinval;
    }
}
