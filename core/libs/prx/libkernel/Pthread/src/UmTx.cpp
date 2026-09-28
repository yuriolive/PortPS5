// _umtx_op implementation for PortPS5 libkernel.
// Implements FreeBSD _umtx_op operations on in-place futex words using FutexCore.

#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

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
        size = sizeof(KernelTimespec);
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

    if (clockid != 0 && clockid != 4 && clockid != 5 && clockid != 12)
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
        if (isUmutexStruct) {
            // FreeBSD umutex: m_flags is the second 32-bit word, m_ceilings is third.
            // UMUTEX_ERROR_CHECK = 0x0002, UMUTEX_PRIO_PROTECT = 0x0008.
            const auto* m = static_cast<const std::uint32_t*>(word);
            const std::uint32_t flags = m[1];
            if ((flags & 0x0008u) != 0) {
                // Priority protect: m[2] is m_ceilings[0]. Reject if priority exceeds ceiling (FreeBSD RTP_PRIO_MAX = 700).
                const std::uint32_t ceiling = m[2];
                if (ceiling > 700u) {
                    return SyncWords::kSceEinval;
                }
            }
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
            const auto* m = static_cast<const std::uint32_t*>(word);
            const std::uint32_t flags = m[1];
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

// Kernel condition variable wait queue representation for UMTX_OP_CV_*.
// Maintains waiter registration so that cvWord[0] (c_has_waiters) reflects active
// waiters, and signal/broadcast selectively notify waiting threads without spurious
// cross-waiter wakeups.
struct CvWaitNode {
    CvWaitNode* next{nullptr};
    CvWaitNode* prev{nullptr};
    void* obj{nullptr};
    std::atomic<bool> awakened{false};
    std::mutex mtx;
    std::condition_variable cv;
};

struct CvBucket {
    std::mutex mtx;
    CvWaitNode* head{nullptr};
};

constexpr std::size_t kCvBucketCount = 64;
CvBucket s_cvBuckets[kCvBucketCount];

inline CvBucket& GetCvBucket(void* obj) noexcept {
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(obj);
    return s_cvBuckets[(addr >> 4) % kCvBucketCount];
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
                     op == kOpMutexLock2 || op == kOpMutexWait || op == kOpRwWrlock ||
                     op == kOpRwUnlock))
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
        if (!FutexCore::WaitU64(reinterpret_cast<volatile std::uint64_t*>(w), val, deadline))
            return SyncWords::kSceTimedOut;
        return SyncWords::kSceOk;
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
        if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(w),
                                static_cast<std::uint32_t>(val), deadline))
            return SyncWords::kSceTimedOut;
        return SyncWords::kSceOk;
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
        // FreeBSD UMUTEX_PRIO_PROTECT = 0x0008: umutex must be priority-protect.
        if ((m[1] & 0x0008u) == 0)
            return SyncWords::kSceEinval;
        if (val > 700u)
            return SyncWords::kSceEinval;
        // Lock umutex to serialize ceiling update:
        const int lockRc = UmutexLock(m, tid, FutexCore::kInfinite, true);
        if (lockRc != SyncWords::kSceOk)
            return lockRc;
        if (uaddr)
            *static_cast<std::uint32_t*>(uaddr) = m[2];
        m[2] = static_cast<std::uint32_t>(val);
        UmutexUnlock(m, tid);
        return SyncWords::kSceOk;
    }
    case kOpCvWait: {
        if (!obj || !uaddr)
            return SyncWords::kSceEinval;
        // FreeBSD sys/umtx.h: CVWAIT_ABSTIME = 0x02, CVWAIT_CLOCKID = 0x04
        constexpr std::uint64_t kCvWaitAbstime = 0x02;
        constexpr std::uint64_t kCvWaitClockid = 0x04;

        auto* cvWord = static_cast<std::uint32_t*>(obj);
        auto* mutexWord = static_cast<std::uint32_t*>(uaddr);

        FutexCore::Deadline deadline{FutexCore::kInfinite};
        if (uaddr2 != nullptr) {
            const auto* ts = static_cast<const KernelTimespec*>(uaddr2);
            if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL)
                return SyncWords::kSceEinval;
            std::uint32_t clockid = 0;
            if ((val & kCvWaitClockid) != 0) {
                clockid = cvWord[2];
            }
            const bool isMono = (clockid == 1 || clockid == 4 || clockid == 5 ||
                                 clockid == 7 || clockid == 8 || clockid == 11 || clockid == 12);
            if (!isMono && clockid != 0 && clockid != 9 && clockid != 10 && clockid != 13)
                return SyncWords::kSceEinval;
            if ((val & kCvWaitAbstime) != 0) {
                deadline = FutexCore::AbsoluteToDeadline(ts->tv_sec, ts->tv_nsec, isMono);
            } else {
                constexpr std::uint64_t kMaxSec = (UINT64_MAX - 1000000000ULL) / 1000000000ULL;
                if (static_cast<std::uint64_t>(ts->tv_sec) > kMaxSec) {
                    deadline = FutexCore::Deadline{FutexCore::kInfinite};
                } else {
                    const std::uint64_t relNanos = static_cast<std::uint64_t>(ts->tv_sec) * 1000000000ULL +
                                                   static_cast<std::uint64_t>(ts->tv_nsec);
                    const std::uint64_t now = FutexCore::NowNanos();
                    if (UINT64_MAX - now <= relNanos)
                        deadline = FutexCore::Deadline{FutexCore::kInfinite};
                    else
                        deadline = FutexCore::Deadline{now + relNanos, false};
                }
            }
        }

        CvWaitNode node;
        node.obj = obj;
        auto& bucket = GetCvBucket(obj);
        {
            std::lock_guard<std::mutex> bLock(bucket.mtx);
            node.next = bucket.head;
            if (bucket.head)
                bucket.head->prev = &node;
            bucket.head = &node;
            // Mark condition variable as having waiters (c_has_waiters = 1) for userland fast-path checks.
            std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
            cvRef.store(1, std::memory_order_release);
        }

        const int unlockErr = UmutexUnlock(mutexWord, tid);
        if (unlockErr != SyncWords::kSceOk) {
            std::lock_guard<std::mutex> bLock(bucket.mtx);
            if (node.prev)
                node.prev->next = node.next;
            else
                bucket.head = node.next;
            if (node.next)
                node.next->prev = node.prev;
            bool anyRemaining = false;
            for (auto* cur = bucket.head; cur != nullptr; cur = cur->next) {
                if (cur->obj == obj) {
                    anyRemaining = true;
                    break;
                }
            }
            if (!anyRemaining) {
                std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
                cvRef.store(0, std::memory_order_release);
            }
            return unlockErr;
        }

        std::unique_lock<std::mutex> nodeLock(node.mtx);
        bool timedOut = false;
        while (!node.awakened.load(std::memory_order_acquire)) {
            if (deadline.targetNanos == FutexCore::kInfinite) {
                node.cv.wait(nodeLock);
            } else {
                if (deadline.IsExpired()) {
                    timedOut = true;
                    break;
                }
                std::uint64_t waitSlice = deadline.RemainingNanos();
                constexpr auto maxWait = static_cast<std::uint64_t>(std::chrono::nanoseconds::max().count());
                if (waitSlice > maxWait) {
                    waitSlice = maxWait;
                }
                if (deadline.isRealtime && waitSlice > 100'000'000ULL) {
                    waitSlice = 100'000'000ULL; // Slice to 100ms so wall-clock changes are noticed
                }
                const auto status = node.cv.wait_for(nodeLock, std::chrono::nanoseconds(waitSlice));
                if (deadline.IsExpired()) {
                    if (!node.awakened.load(std::memory_order_acquire)) {
                        timedOut = true;
                        break;
                    }
                }
            }
        }
        nodeLock.unlock();

        {
            std::lock_guard<std::mutex> bLock(bucket.mtx);
            if (node.prev)
                node.prev->next = node.next;
            else
                bucket.head = node.next;
            if (node.next)
                node.next->prev = node.prev;
            bool anyRemaining = false;
            for (auto* cur = bucket.head; cur != nullptr; cur = cur->next) {
                if (cur->obj == obj) {
                    anyRemaining = true;
                    break;
                }
            }
            if (!anyRemaining) {
                std::atomic_ref<std::uint32_t> cvRef(cvWord[0]);
                cvRef.store(0, std::memory_order_release);
            }
        }

        return timedOut ? SyncWords::kSceTimedOut : SyncWords::kSceOk;
    }
    case kOpCvSignal: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto& bucket = GetCvBucket(obj);
        std::lock_guard<std::mutex> bLock(bucket.mtx);
        for (auto* cur = bucket.head; cur != nullptr; cur = cur->next) {
            if (cur->obj == obj && !cur->awakened.load(std::memory_order_relaxed)) {
                cur->awakened.store(true, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> nLock(cur->mtx);
                }
                cur->cv.notify_one();
                break;
            }
        }
        return SyncWords::kSceOk;
    }
    case kOpCvBroadcast: {
        if (!obj)
            return SyncWords::kSceEinval;
        auto& bucket = GetCvBucket(obj);
        std::lock_guard<std::mutex> bLock(bucket.mtx);
        for (auto* cur = bucket.head; cur != nullptr; cur = cur->next) {
            if (cur->obj == obj && !cur->awakened.load(std::memory_order_relaxed)) {
                cur->awakened.store(true, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> nLock(cur->mtx);
                }
                cur->cv.notify_all();
            }
        }
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

        // Honor URWLOCK_PREFER_READER from either val flags or struct flags (rwWord[1]).
        std::uint32_t wrflags = kRwWriteOwner;
        if (((val | rwWord[1]) & kRwPreferReader) == 0)
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
        constexpr std::uint32_t kRwReadWaiters = 0x20000000u;
        constexpr std::uint32_t kRwMaxReaders = 0x1FFFFFFFu;

        auto cleanupWaitersOnTimeout = [&]() noexcept {
            while (true) {
                std::uint32_t s = stateRef.load(std::memory_order_acquire);
                if ((s & kRwWriteWaiters) == 0)
                    break;
                std::uint32_t exp = s;
                if (stateRef.compare_exchange_strong(exp, s & ~kRwWriteWaiters,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                    // Wake potential readers blocked on kRwWriteWaiters.
                    FutexCore::WakeAll(rwWord);
                    break;
                }
            }
        };

        while (true) {
            std::uint32_t state = stateRef.load(std::memory_order_acquire);
            // Recursive write lock detection: state low 24 bits hold writer TID atomically.
            if ((state & kRwWriteOwner) != 0 && (state & kUmutexOwnerMask) == (tid & kUmutexOwnerMask))
                return SyncWords::kSceEdeadlk;

            // Acquire write lock if no writer holds and no readers hold.
            // Publish write owner bit and writer TID simultaneously in a single atomic CAS.
            if ((state & (kRwWriteOwner | kRwMaxReaders)) == 0) {
                std::uint32_t expected = state;
                const std::uint32_t desired = kRwWriteOwner | (state & (kRwWriteWaiters | kRwReadWaiters)) |
                                             (tid & kUmutexOwnerMask);
                if (stateRef.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                    return SyncWords::kSceOk;
                }
                continue;
            }
            if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline) {
                cleanupWaitersOnTimeout();
                return SyncWords::kSceTimedOut;
            }
            if ((state & kRwWriteWaiters) == 0) {
                std::uint32_t expected = state;
                stateRef.compare_exchange_strong(expected, state | kRwWriteWaiters,
                                                 std::memory_order_acq_rel, std::memory_order_acquire);
            }
            const std::uint32_t expect = stateRef.load(std::memory_order_acquire);
            if ((expect & (kRwWriteOwner | kRwMaxReaders)) == 0)
                continue;
            if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(rwWord), expect, deadline)) {
                cleanupWaitersOnTimeout();
                return SyncWords::kSceTimedOut;
            }
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
                // Writer release: verify calling thread is the writer that holds the lock.
                const std::uint32_t owner = state & kUmutexOwnerMask;
                if (owner != 0 && owner != (tid & kUmutexOwnerMask))
                    return SyncWords::kSceEperm;
                // Clear owner and TID without mutating state before CAS succeeds;
                // preserve waiters so queued threads can wake and acquire.
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
            const bool wakeWaiters = readers == 1 && (state & (kRwWriteWaiters | kRwReadWaiters)) != 0;
            // Preserve waiter bits when removing the final reader so waking a waiter preserves visibility.
            const std::uint32_t desired = readers == 1 ? (state & (kRwWriteWaiters | kRwReadWaiters)) : state - 1;
            if (stateRef.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                if (wakeWaiters)
                    FutexCore::WakeAll(rwWord);
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
