// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include "../include/Pthread.hpp"
#include "../include/Mutex.hpp"
#include "../include/FutexCore.hpp"
#include "../include/GuestTid.hpp"
#include "../include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace {

using SyncWords::kSceEagain;
using SyncWords::kSceEbusy;
using SyncWords::kSceEdeadlk;
using SyncWords::kSceEinval;
using SyncWords::kSceEperm;
using SyncWords::kSceOk;
using SyncWords::kSceTimedOut;
using MW = SyncWords::MutexWord;

// The slot address IS the word address: PthreadMutex* points at 8 bytes of
// guest storage, accessed as uint64_t via atomic_ref. INIT (bit 63) never
// collides with canonical user pointers, so pointer/word confusion is
// impossible; 0/1 are the static initializers.
inline std::uint64_t* WordPtr(PthreadMutex* slot) noexcept {
    return reinterpret_cast<std::uint64_t*>(slot);
}

inline std::atomic_ref<std::uint64_t> WordRef(PthreadMutex* slot) noexcept {
    return std::atomic_ref<std::uint64_t>(*WordPtr(slot));
}

// Map an attr type to the 3-bit word type. Null attr means Normal for
// explicit init (static slot 0 still lazily becomes ErrorCheck).
inline std::uint32_t AttrType(const PthreadMutexattr* attr) noexcept {
    if (!attr || !*attr)
        return MW::kNormal;
    switch ((*attr)->type) {
    case MutexType::ErrorCheck: return MW::kErrCheck;
    case MutexType::Recursive: return MW::kRecursive;
    case MutexType::Normal: return MW::kNormal;
    default: return MW::kNormal;
    }
}

// Fast uncontended acquire: CAS(unlocked INIT|type -> locked INIT|type|tid).
// Returns kSceOk on success, -1 when the word was not in the expected
// unlocked state (caller falls through to the contended path).
int TryFastAcquire(std::atomic_ref<std::uint64_t>& ref, std::uint64_t unlocked,
                   std::uint32_t tid) noexcept {
    const std::uint64_t desired =
        MW::Make(MW::Type(unlocked), tid, 0, false);
    std::uint64_t expected = unlocked;
    if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
        return kSceOk;
    return -1;
}

// Core lock with optional deadline (kInfinite = blocking). No global mutex:
// Drepper three-state (unlocked / locked / locked+CONTENDED) over the word.
int LockInternal(PthreadMutex* slot, FutexCore::Deadline deadline) noexcept {
    if (!slot)
        return kSceEinval;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return kSceEagain;  // >2^24 live threads.
    auto ref = WordRef(slot);

    // Lazy static init + acquisition in one CAS lives inside the loop below
    // (words 0 and 1 have no INIT bit).
    while (true) {
        std::uint64_t w = ref.load(std::memory_order_acquire);

        // Static defaults: 0 -> ErrorCheck, 1 -> Normal (adaptive).
        if (w == 0 || w == 1) {
            const std::uint32_t type = (w == 0) ? MW::kErrCheck : MW::kNormal;
            const std::uint64_t desired = MW::Make(type, tid, 0, false);
            if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;  // Lost the INIT race; exactly one winner, retry.
        }

        if (!MW::IsInit(w)) {
            // Old pointer representation without INIT: a guest bug (copied or
            // forged slot). POSIX calls memcpy of a live object undefined;
            // report EINVAL rather than corrupt.
            return kSceEinval;
        }
        if (MW::IsDestroyed(w))
            return kSceEinval;

        const std::uint32_t type = MW::Type(w);
        if (type != MW::kErrCheck && type != MW::kRecursive && type != MW::kNormal)
            return kSceEinval;
        const std::uint32_t owner = MW::Owner(w);

        if (owner == tid) {
            // Relock by owner.
            if (type == MW::kRecursive) {
                const std::uint32_t rec = MW::RecStored(w);
                if (rec == 0xFFFFu)
                    return kSceEagain;  // recursion overflow.
                const std::uint64_t desired =
                    MW::Make(type, tid, rec + 1, MW::IsContended(w));
                if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                                std::memory_order_acquire))
                    return kSceOk;
                continue;
            }
            // ErrorCheck AND Normal relock report EDEADLK (Normal relock is
            // POSIX-undefined; returning instead of deadlocking keeps a guest
            // bug diagnosable and avoids hanging the watchdog).
            return kSceEdeadlk;
        }

        if (owner == 0) {
            // Unlocked INIT word: try the fast CAS (clears CONTENDED).
            const std::uint64_t unlocked = MW::Make(type, 0, 0, false);
            if (w == unlocked) {
                if (TryFastAcquire(ref, unlocked, tid) == kSceOk)
                    return kSceOk;
                continue;
            }
            // Unlocked but CONTENDED flag set (waiters queued): steal it with
            // CAS to locked, preserving CONTENDED so the next unlock wakes them.
            const std::uint64_t desired = MW::Make(type, tid, 0, true);
            if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }

        // Locked by another thread.
        if (deadline.IsExpired())
            return kSceTimedOut;
        // Mark CONTENDED before sleeping (release), so unlock's load sees us
        // and wakes exactly one waiter. Then re-check before sleeping: if the
        // word became unlocked between marking and loading (unlock raced
        // ahead and already returned, so no further wake is coming), retry
        // acquisition instead of sleeping on an unlocked word forever.
        std::uint64_t withContended = w | MW::kContended;
        if (w != withContended)
            ref.compare_exchange_strong(w, withContended, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
        const std::uint64_t expect = ref.load(std::memory_order_acquire);
        if (!MW::IsInit(expect) || MW::IsDestroyed(expect))
            continue;  // next loop returns EINVAL.
        if (MW::Owner(expect) == 0)
            continue;  // unlocked now: retry acquire, do not wait.
        if (MW::Owner(expect) == tid)
            continue;  // raced with our own relock: re-evaluate (EDEADLK/recursive).
        // If the word changed between marking and waiting, WaitOnce returns
        // immediately (expected mismatch) and we retry without sleeping.
        const bool progress = FutexCore::WaitU64(WordPtr(slot), expect, deadline);
        if (!progress) {
            if (deadline == 0)
                return kSceEbusy;
            return kSceTimedOut;
        }
    }
}

int UnlockInternal(PthreadMutex* slot) noexcept {
    if (!slot)
        return kSceEinval;
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return kSceEagain;
    auto ref = WordRef(slot);
    while (true) {
        const std::uint64_t w = ref.load(std::memory_order_acquire);
        if (w == 0 || w == 1)
            return kSceEperm;  // never locked static.
        if (!MW::IsInit(w) || MW::IsDestroyed(w))
            return kSceEinval;
        const std::uint32_t type = MW::Type(w);
        if (type != MW::kErrCheck && type != MW::kRecursive && type != MW::kNormal)
            return kSceEinval;
        if (MW::Owner(w) != tid)
            return kSceEperm;
        if (type == MW::kRecursive && MW::RecStored(w) != 0) {
            // Recursive inner unlock: decrement, keep owner, no wake.
            const std::uint64_t desired =
                MW::Make(type, tid, MW::RecStored(w) - 1, MW::IsContended(w));
            std::uint64_t expected = w;
            if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        // Final unlock: clear owner, preserving CONTENDED if waiters were queued
        // so the next owner wakes the remaining waiters (Drepper handoff).
        const bool hadWaiters = MW::IsContended(w);
        const std::uint64_t desired = MW::Make(type, 0, 0, hadWaiters);
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            if (hadWaiters)
                FutexCore::WakeSingle(WordPtr(slot));
            return kSceOk;
        }
    }
}

}  // namespace

int MutexOperations::Timedlock(PthreadMutex* mutex, const KernelTimespec* abstime) {
    if (!mutex || !abstime)
        return kSceEinval;
    if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000LL)
        return kSceEinval;
    // POSIX absolute timeout is REALTIME; convert to a Deadline.
    const FutexCore::Deadline deadline =
        FutexCore::AbsoluteToDeadline(abstime->tv_sec, abstime->tv_nsec, false);
    return LockInternal(mutex, deadline);
}

extern "C" {

/**
 * @brief scePthreadMutexattrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrInit(PthreadMutexattr* attr) noexcept {
    if (!attr)
        return kSceEinval;
    *attr = new (std::nothrow) PthreadMutexattrPrivate{MutexType::Normal};
    return *attr ? kSceOk : SyncWords::kSceEnomem;
}

/**
 * @brief scePthreadMutexattrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrDestroy(PthreadMutexattr* attr) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    delete *attr;
    *attr = nullptr;
    return kSceOk;
}

/**
 * @brief scePthreadMutexattrSettype implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrSettype(PthreadMutexattr* attr, int type) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    switch (type) {
    case 1: (*attr)->type = MutexType::ErrorCheck; return kSceOk;
    case 2: (*attr)->type = MutexType::Recursive; return kSceOk;
    case 3: (*attr)->type = MutexType::Normal; return kSceOk;
    default: return kSceEinval;
    }
}

/**
 * @brief scePthreadMutexattrSetprotocol implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrSetprotocol(PthreadMutexattr* attr, int protocol) noexcept {
    if (!attr || !*attr)
        return kSceEinval;
    // Only PROTOCOL_NONE (0); inheritance/protection are unsupported but the
    // call itself is meaningful, so EINVAL (not Unsupported).
    return (protocol == 0) ? kSceOk : kSceEinval;
}

/**
 * @brief scePthreadMutexInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr,
                                 const char*) noexcept {
    if (!mutex)
        return kSceEinval;
    if (attr && !*attr)
        return kSceEinval;
    const std::uint32_t type = AttrType(attr);
    // Explicit init writes an unlocked INIT word (re-init after destroy is
    // allowed; re-init of a locked word is POSIX-undefined, so EINVAL when
    // the current word shows a live owner).
    auto ref = WordRef(mutex);
    const std::uint64_t cur = ref.load(std::memory_order_acquire);
    if (MW::IsInit(cur) && !MW::IsDestroyed(cur) && MW::Owner(cur) != 0)
        return kSceEbusy;
    ref.store(MW::Make(type, 0, 0, false), std::memory_order_release);
    return kSceOk;
}

/**
 * @brief scePthreadMutexDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept {
    if (!mutex)
        return kSceEinval;
    auto ref = WordRef(mutex);
    while (true) {
        std::uint64_t w = ref.load(std::memory_order_acquire);
        if (w == 0 || w == 1) {
            // Never-touched static: mark destroyed so later use is EINVAL.
            if (ref.compare_exchange_strong(w, MW::kDestroyedWord, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
                return kSceOk;
            continue;
        }
        if (!MW::IsInit(w) || MW::IsDestroyed(w))
            return kSceEinval;
        if (MW::Owner(w) != 0)
            return kSceEbusy;
        std::uint64_t expected = w;
        if (ref.compare_exchange_strong(expected, MW::kDestroyedWord,
                                        std::memory_order_acq_rel, std::memory_order_acquire))
            return kSceOk;
    }
}

/**
 * @brief scePthreadMutexLock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept {
    return LockInternal(mutex, FutexCore::kInfinite);
}

/**
 * @brief scePthreadMutexUnlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept {
    return UnlockInternal(mutex);
}

/**
 * @brief scePthreadMutexTimedlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexTimedlock(PthreadMutex* mutex, KernelUseconds usec) noexcept {
    if (!mutex)
        return kSceEinval;
    const std::uint64_t deadline = FutexCore::NowNanos() + static_cast<std::uint64_t>(usec) * 1000ULL;
    return LockInternal(mutex, deadline);
}

/**
 * @brief scePthreadMutexTrylock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexTrylock(PthreadMutex* mutex) noexcept {
    if (!mutex)
        return kSceEinval;
    // Sentinel deadline 0 would collide with a valid QPC time; use a flag:
    // attempt exactly one fast acquire, else EBUSY without sleeping.
    const std::uint32_t tid = GuestTid::Ensure();
    if (tid == 0)
        return kSceEagain;
    auto ref = WordRef(mutex);
    std::uint64_t w = ref.load(std::memory_order_acquire);
    if (w == 0 || w == 1) {
        const std::uint32_t type = (w == 0) ? MW::kErrCheck : MW::kNormal;
        const std::uint64_t desired = MW::Make(type, tid, 0, false);
        if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return kSceOk;
        return kSceEbusy;
    }
    if (!MW::IsInit(w) || MW::IsDestroyed(w))
        return kSceEinval;
    const std::uint32_t type = MW::Type(w);
    if (type != MW::kErrCheck && type != MW::kRecursive && type != MW::kNormal)
        return kSceEinval;
    const std::uint32_t owner = MW::Owner(w);
    if (owner == tid) {
        if (type == MW::kRecursive) {
            while (true) {
                const std::uint32_t rec = MW::RecStored(w);
                if (rec == 0xFFFFu)
                    return kSceEagain;
                const std::uint64_t desired = MW::Make(type, tid, rec + 1, MW::IsContended(w));
                if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                                std::memory_order_acquire))
                    return kSceOk;
                if (!MW::IsInit(w) || MW::Type(w) != type || MW::Owner(w) != tid)
                    return kSceEbusy;
            }
        }
        return kSceEbusy;  // trylock on owned non-recursive: EBUSY (not EDEADLK).
    }
    if (owner == 0) {
        const std::uint64_t unlocked = MW::Make(type, 0, 0, false);
        if (w == unlocked && TryFastAcquire(ref, unlocked, tid) == kSceOk)
            return kSceOk;
        // Stale CONTENDED variant of unlocked: steal it.
        const std::uint64_t desired = MW::Make(type, tid, 0, false);
        if (ref.compare_exchange_strong(w, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return kSceOk;
        return kSceEbusy;
    }
    return kSceEbusy;
}

}
