#include "prx/libkernel/Pthread/include/Pthread.hpp"
#include "prx/libkernel/Pthread/include/Mutex.hpp"
#include "prx/libkernel/Pthread/include/Cond.hpp"
#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace {

using SyncWords::kSceEbusy;
using SyncWords::kSceEinval;
using SyncWords::kSceEperm;
using SyncWords::kSceOk;
using SyncWords::kSceTimedOut;
using CW = SyncWords::CondWord;
using MW = SyncWords::MutexWord;

inline std::uint64_t* CondPtr(PthreadCond* slot) noexcept {
    return reinterpret_cast<std::uint64_t*>(slot);
}
inline std::atomic_ref<std::uint64_t> CondRef(PthreadCond* slot) noexcept {
    return std::atomic_ref<std::uint64_t>(*CondPtr(slot));
}
inline std::uint64_t* MutexPtr(PthreadMutex* slot) noexcept {
    return reinterpret_cast<std::uint64_t*>(slot);
}
inline std::atomic_ref<std::uint64_t> MutexRef(PthreadMutex* slot) noexcept {
    return std::atomic_ref<std::uint64_t>(*MutexPtr(slot));
}

// Verify the wait mutex is owned by us; return its word + type/recursion so
// the waiter can fully unlock (including recursive count) and restore after.
struct HeldMutex {
    std::uint64_t word;
    std::uint32_t type;
    std::uint32_t recStored;
};

int CheckHeldMutex(PthreadMutex* mutex, std::uint32_t tid, HeldMutex& out) noexcept {
    if (!mutex)
        return kSceEinval;
    const std::uint64_t w = MutexRef(mutex).load(std::memory_order_acquire);
    if (w == 0 || w == 1)
        return kSceEperm;
    if (!MW::IsInit(w) || MW::IsDestroyed(w))
        return kSceEinval;
    const std::uint32_t type = MW::Type(w);
    if (type != MW::kErrCheck && type != MW::kRecursive && type != MW::kNormal)
        return kSceEinval;
    if (MW::Owner(w) != tid)
        return kSceEperm;
    out.word = w;
    out.type = type;
    out.recStored = MW::RecStored(w);
    return kSceOk;
}

// Fully unlock for cond wait (clear owner + recursion, keep type). Wake one
// waiter if the mutex itself was contended, mirroring Mutex.cpp unlock.
void MutexUnlockForWait(PthreadMutex* mutex, const HeldMutex& held, std::uint32_t tid) noexcept {
    auto ref = MutexRef(mutex);
    while (true) {
        const std::uint64_t w = ref.load(std::memory_order_acquire);
        if (!MW::IsInit(w) || MW::IsDestroyed(w) || MW::Owner(w) != tid)
            return;
        const bool hadWaiters = MW::IsContended(w);
        const std::uint64_t desired = MW::Make(held.type, 0, 0, hadWaiters);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            FutexCore::WakeSingle(MutexPtr(mutex));
            return;
        }
    }
}

// Relock after wait in CONTENDED state (spec), so the next unlock hands off.
// For recursive mutexes the saved recursion count is restored.
int MutexRelockAfterWait(PthreadMutex* mutex, const HeldMutex& held, std::uint32_t tid,
                         std::uint64_t deadline) noexcept {
    auto ref = MutexRef(mutex);
    // First acquire ownership (contended path sets CONTENDED as needed).
    while (true) {
        const std::uint64_t w = ref.load(std::memory_order_acquire);
        if (!MW::IsInit(w) || MW::IsDestroyed(w))
            return kSceEinval;
        const std::uint32_t type = MW::Type(w);
        const std::uint32_t owner = MW::Owner(w);
        if (owner == 0) {
            std::uint64_t desired;
            if (held.type == MW::kRecursive)
                desired = MW::Make(type, tid, held.recStored, true);
            else
                desired = MW::Make(type, tid, 0, true);
            std::uint64_t expected = w;
            // Preserve the unlocked word's type (== held.type in practice).
            desired = MW::Make(type, tid,
                               (held.type == MW::kRecursive) ? held.recStored : 0, true);
            if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        if (owner == tid) {
            // Already own (spurious re-entry after broadcast race): ensure
            // recursion matches and CONTENDED is set.
            if (held.type == MW::kRecursive) {
                const std::uint64_t desired = MW::Make(type, tid, held.recStored, true);
                std::uint64_t expected = w;
                if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                std::memory_order_acquire))
                    return kSceOk;
                continue;
            }
            const std::uint64_t desired = w | MW::kContended;
            if (w == desired)
                return kSceOk;
            std::uint64_t expected = w;
            if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        if (deadline != FutexCore::kInfinite && FutexCore::NowNanos() >= deadline)
            return kSceTimedOut;
        const std::uint64_t withContended = w | MW::kContended;
        if (w != withContended) {
            std::uint64_t expected = w;
            ref.compare_exchange_strong(expected, withContended, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
        }
        const std::uint64_t expect = ref.load(std::memory_order_acquire);
        // Lost-wakeup guard (mirrors Mutex.cpp): if unlocked before we slept,
        // retry instead of waiting on a free word (unlock already returned).
        if (!MW::IsInit(expect) || MW::IsDestroyed(expect))
            continue;
        if (MW::Owner(expect) == 0 || MW::Owner(expect) == tid)
            continue;
        if (!FutexCore::WaitU64(MutexPtr(mutex), expect, deadline))
            return kSceTimedOut;
    }
}

int WaitInternal(PthreadCond* cond, PthreadMutex* mutex, std::uint64_t deadline,
                 bool hasDeadline) noexcept {
    if (!cond || !mutex)
        return kSceEinval;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return SyncWords::kSceEagain;
    HeldMutex held{};
    if (const int rc = CheckHeldMutex(mutex, tid, held))
        return rc;

    auto cref = CondRef(cond);
    // Lazy init of a zero slot (plain INIT, realtime clock). Slot 1 is not a
    // valid cond static; treat any non-INIT non-zero word as EINVAL.
    std::uint64_t cw = cref.load(std::memory_order_acquire);
    if (cw == 0) {
        std::uint64_t expected = 0;
        const std::uint64_t init = CW::Make(false, 0, 0);
        cref.compare_exchange_strong(expected, init, std::memory_order_acq_rel,
                                     std::memory_order_acquire);
        cw = cref.load(std::memory_order_acquire);
    }
    if (!CW::IsInit(cw) || CW::IsDestroyed(cw))
        return kSceEinval;

    // Read seq, increment waiters, THEN unlock the mutex (release): the
    // increment must be visible before we sleep, otherwise a signal between
    // the read and the sleep would be lost. The signal side bumps seq BEFORE
    // waking (release), and we only sleep while seq is unchanged, so the
    // seq check closes the lost-wakeup window.
    std::uint64_t seq = 0;
    while (true) {
        cw = cref.load(std::memory_order_acquire);
        if (!CW::IsInit(cw) || CW::IsDestroyed(cw))
            return kSceEinval;
        const std::uint32_t waiters = CW::Waiters(cw);
        if (waiters >= 8191u)
            return SyncWords::kSceEagain;
        seq = CW::Seq(cw);
        // Rebuild preserving flags exactly:
        const std::uint64_t want = CW::kInit | (CW::IsMono(cw) ? CW::kClockMono : 0ULL) |
                                   (static_cast<std::uint64_t>(waiters + 1) << CW::kWaitersShift) |
                                   seq;
        std::uint64_t expected = cw;
        if (cref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            break;
        }
    }

    MutexUnlockForWait(mutex, held, tid);

    // Wait while seq is unchanged. A wake with only non-seq bits changed
    // (e.g. a concurrent destroy flag, which we already reject) waits again
    // with the new value; only a seq bump returns.
    int waitRc = kSceOk;
    while (true) {
        const std::uint64_t cur = cref.load(std::memory_order_acquire);
        if (!CW::IsInit(cur) || CW::IsDestroyed(cur)) {
            waitRc = kSceEinval;
            break;
        }
        if (CW::Seq(cur) != seq)
            break;
        if (hasDeadline && FutexCore::NowNanos() >= deadline) {
            waitRc = kSceTimedOut;
            break;
        }
        // Decrement our waiter slot on timeout/invalid before relocking, so
        // Signal's waiters==0 fast path stays accurate. On success the
        // signal/broadcast already dequeued us (signal decrements, broadcast
        // zeroes), so only adjust when we leave without a seq change.
        const bool woken = FutexCore::WaitU64(CondPtr(cond), cur, deadline);
        if (!woken) {
            waitRc = kSceTimedOut;
            break;
        }
    }

    if (waitRc == kSceTimedOut || waitRc == kSceEinval) {
        // Dequeue this timed out/invalid waiter so waiters count stays accurate.
        while (true) {
            const std::uint64_t cur = cref.load(std::memory_order_acquire);
            if (!CW::IsInit(cur))
                break;
            const std::uint32_t w = CW::Waiters(cur);
            if (w == 0)
                break;
            const std::uint64_t want = CW::kInit | (CW::IsMono(cur) ? CW::kClockMono : 0ULL) |
                                       (static_cast<std::uint64_t>(w - 1) << CW::kWaitersShift) |
                                       CW::Seq(cur);
            std::uint64_t expected = cur;
            if (cref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                             std::memory_order_acquire))
                break;
        }
    }

    // POSIX requires the mutex held on return (even after timeout). Re-acquiring
    // the mutex is untimed; the timeout only bounded waiting on the condition.
    const int relock = MutexRelockAfterWait(mutex, held, tid, FutexCore::kInfinite);
    if (relock != kSceOk) {
        return (waitRc == kSceOk) ? relock : waitRc;
    }
    return waitRc;
}

}  // namespace

int CondOperations::AbsoluteTimedwait(PthreadCond* cond, PthreadMutex* mutex,
                                      const KernelTimespec* abstime) {
    if (!cond || !mutex || !abstime)
        return kSceEinval;
    if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000LL)
        return kSceEinval;
    // Clock selection comes from the cond's MONOTONIC bit when initialized;
    // default (zero slot) is REALTIME.
    bool mono = false;
    if (cond) {
        const std::uint64_t cw =
            std::atomic_ref<std::uint64_t>(*reinterpret_cast<std::uint64_t*>(cond)).load(
                std::memory_order_acquire);
        if (CW::IsInit(cw))
            mono = CW::IsMono(cw);
    }
    const std::uint64_t deadline =
        FutexCore::AbsoluteToDeadline(abstime->tv_sec, abstime->tv_nsec, mono);
    return WaitInternal(cond, mutex, deadline, true);
}

extern "C" {

int APS5_VABI scePthreadCondattrInit(PthreadCondattr* attr) noexcept {
    if (!attr)
        return kSceEinval;
    *attr = new (std::nothrow) PthreadCondattrPrivate{0};
    return *attr ? kSceOk : SyncWords::kSceEnomem;
}

int APS5_VABI scePthreadCondattrDestroy(PthreadCondattr* attr) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    delete *attr;
    *attr = nullptr;
    return kSceOk;
}

int APS5_VABI scePthreadCondattrSetclock(PthreadCondattr* attr, KernelClockid clockId) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    const int clk = static_cast<int>(clockId);
    if (clk < 0 || clk > 13)
        return kSceEinval;
    (*attr)->_clockid = clk;
    return kSceOk;
}

int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr,
                                const char*) noexcept {
    if (!cond)
        return kSceEinval;
    if (attr && !*attr)
        return kSceEinval;
    bool mono = false;
    if (attr && *attr) {
        const int clk = (*attr)->_clockid;
        // Monotonic ids per Time.cpp (1/4/5/7/8/11/12); 0/9/10/13 realtime.
        mono = (clk == 1 || clk == 4 || clk == 5 || clk == 7 || clk == 8 || clk == 11 || clk == 12);
    }
    CondRef(cond).store(CW::Make(mono, 0, 0), std::memory_order_release);
    return kSceOk;
}

int APS5_VABI scePthreadCondDestroy(PthreadCond* cond) noexcept {
    if (!cond)
        return kSceEinval;
    auto ref = CondRef(cond);
    const std::uint64_t w = ref.load(std::memory_order_acquire);
    if (w == 0) {
        ref.store(CW::kDestroyedWord, std::memory_order_release);
        return kSceOk;
    }
    if (!CW::IsInit(w) || CW::IsDestroyed(w))
        return kSceEinval;
    if (CW::Waiters(w) != 0)
        return kSceEbusy;
    ref.store(CW::kDestroyedWord, std::memory_order_release);
    return kSceOk;
}

int APS5_VABI scePthreadCondSignal(PthreadCond* cond) noexcept {
    if (!cond)
        return kSceEinval;
    auto ref = CondRef(cond);
    std::uint64_t w = ref.load(std::memory_order_acquire);
    if (w == 0) {
        std::uint64_t expected = 0;
        if (ref.compare_exchange_strong(expected, CW::Make(false, 0, 0), std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return kSceOk;  // no waiters on a fresh cond.
    }
    if (!CW::IsInit(w) || CW::IsDestroyed(w))
        return kSceEinval;
    while (true) {
        w = ref.load(std::memory_order_acquire);
        if (!CW::IsInit(w) || CW::IsDestroyed(w))
            return kSceEinval;
        const std::uint32_t waiters = CW::Waiters(w);
        if (waiters == 0) {
            return kSceOk;  // fast path: nothing to wake.
        }
        const std::uint64_t want = CW::kInit | (CW::IsMono(w) ? CW::kClockMono : 0ULL) |
                                   (static_cast<std::uint64_t>(waiters - 1) << CW::kWaitersShift) |
                                   ((CW::Seq(w) + 1) & CW::kSeqMask);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            FutexCore::WakeSingle(CondPtr(cond));
            return kSceOk;
        }
    }
}

int APS5_VABI scePthreadCondBroadcast(PthreadCond* cond) noexcept {
    if (!cond)
        return kSceEinval;
    auto ref = CondRef(cond);
    std::uint64_t w = ref.load(std::memory_order_acquire);
    if (w == 0) {
        std::uint64_t expected = 0;
        if (ref.compare_exchange_strong(expected, CW::Make(false, 0, 0), std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return kSceOk;
    }
    if (!CW::IsInit(w) || CW::IsDestroyed(w))
        return kSceEinval;
    while (true) {
        w = ref.load(std::memory_order_acquire);
        if (!CW::IsInit(w) || CW::IsDestroyed(w))
            return kSceEinval;
        if (CW::Waiters(w) == 0) {
            // Still bump seq so a waiter between check and sleep cannot miss
            // us (its seq read predates our bump, so it sleeps and our wake
            // arrives after; with zero waiters the wake is harmless).
            const std::uint64_t want = CW::kInit | (CW::IsMono(w) ? CW::kClockMono : 0ULL) |
                                       (static_cast<std::uint64_t>(0) << CW::kWaitersShift) |
                                       ((CW::Seq(w) + 1) & CW::kSeqMask);
            std::uint64_t expected = w;
            if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        const std::uint64_t want = CW::kInit | (CW::IsMono(w) ? CW::kClockMono : 0ULL) |
                                   ((CW::Seq(w) + 1) & CW::kSeqMask);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, want, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            FutexCore::WakeAll(CondPtr(cond));
            return kSceOk;
        }
    }
}

int APS5_VABI scePthreadCondSignalto(PthreadCond* cond, Pthread thread) noexcept {
    (void)thread;
    // POSIX permits broadcast-as-signal (spurious wakeup); matches PR5.
    return scePthreadCondBroadcast(cond);
}

int APS5_VABI scePthreadCondWait(PthreadCond* cond, PthreadMutex* mutex) noexcept {
    return WaitInternal(cond, mutex, FutexCore::kInfinite, false);
}

int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex,
                                      KernelUseconds usec) noexcept {
    if (!cond || !mutex)
        return kSceEinval;
    const std::uint64_t deadline = FutexCore::NowNanos() + static_cast<std::uint64_t>(usec) * 1000ULL;
    return WaitInternal(cond, mutex, deadline, true);
}

}
