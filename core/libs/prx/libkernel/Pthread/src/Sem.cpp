// PortPS5 libkernel: scePthreadSem* (POSIX-style counting semaphore handle).
//
// Subsystem: libkernel threading (docs/spec/threading.md, "Handle objects").
// The guest slot (PthreadSem*, 8 bytes) holds a host pointer to a small object
// whose only state is two futex words, so nothing here uses a heap std::mutex
// or condition variable. Waiters sleep on the `count` word with WaitOnAddress
// (FutexCore) and are woken by post; `waiters` exists only to skip the wake
// syscall when nobody sleeps.
//
// Lifecycle: Init allocates, Destroy frees and leaves the slot at the
// "destroyed" marker (pointer value 2, never a valid heap address) so use
// after destroy returns EINVAL instead of dereferencing freed memory. Using a
// semaphore concurrently with its Destroy is undefined, as on hardware; Destroy
// only rejects the detectable case of sleeping waiters with EBUSY.
//
// Threading: every entry point is lock-free. Ordering: post publishes the new
// count with a seq_cst RMW before reading `waiters`; a waiter bumps `waiters`
// (seq_cst) before it re-reads `count`, so either the waiter sees the token or
// the poster sees the waiter (no lost wakeup). WaitOnAddress re-checks the word
// atomically, which covers the remaining window between check and sleep.
//
// Ported from AnyPS5 upstream/main 064006b6 and adapted: the upstream version
// used std::mutex + TimedWait::Condition and threw std::invalid_argument /
// runtime_error across the APS5_VABI boundary.

#include "../include/Pthread.hpp"
#include "../include/FutexCore.hpp"
#include "../include/SemBarrierTypes.hpp"
#include "../include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"

#include <atomic>
#include <cstdint>
#include <new>

namespace {

using SyncWords::kSceEagain;
using SyncWords::kSceEbusy;
using SyncWords::kSceEinval;
using SyncWords::kSceOk;
using SyncWords::kSceTimedOut;

// FreeBSD EOVERFLOW (84) under the SCE carrier: post would exceed SEM_VALUE_MAX.
constexpr int kSceEoverflow = static_cast<int>(0x80020054u);
// SEM_VALUE_MAX on FreeBSD (INT_MAX); also the largest Init value.
constexpr std::uint32_t kSemValueMax = 0x7FFFFFFFu;

}  // namespace

namespace {

// Slot value written by Destroy. Real heap pointers are never 2.
inline PthreadSem DestroyedSem() noexcept {
    return reinterpret_cast<PthreadSem>(std::uintptr_t{2});
}

// Resolve a guest slot to its live object, or nullptr when the slot is null,
// uninitialised (0) or destroyed. Acquire pairs with Init's release store.
inline PthreadSemPrivate* Resolve(PthreadSem* slot) noexcept {
    if (!slot)
        return nullptr;
    PthreadSem current = std::atomic_ref<PthreadSem>(*slot).load(std::memory_order_acquire);
    if (!current || current == DestroyedSem())
        return nullptr;
    return current;
}

// Take one token without blocking. Returns true on success. CAS loop because
// several waiters race for the same token; acq_rel orders the guest's
// post-wait accesses after the poster's pre-post writes.
inline bool TryTake(PthreadSemPrivate* s) noexcept {
    std::uint32_t c = s->count.load(std::memory_order_acquire);
    while (c > 0) {
        if (s->count.compare_exchange_weak(c, c - 1, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
            return true;
    }
    return false;
}

// Blocking/timed acquire shared by Wait and Timedwait. Returns kSceOk or
// kSceTimedOut. The waiter count brackets the whole sleep so Destroy can see it.
int AcquireUntil(PthreadSemPrivate* s, FutexCore::Deadline deadline) noexcept {
    if (TryTake(s))
        return kSceOk;
    s->waiters.fetch_add(1, std::memory_order_seq_cst);
    int result = kSceOk;
    for (;;) {
        if (TryTake(s))
            break;
        // Sleep only while the word is still 0; a post changes it first.
        if (!FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(&s->count), 0,
                                deadline)) {
            // Deadline expired; one last look so a token posted at the very
            // end of the window is not reported as a timeout.
            result = TryTake(s) ? kSceOk : kSceTimedOut;
            break;
        }
    }
    s->waiters.fetch_sub(1, std::memory_order_release);
    return result;
}

}  // namespace

extern "C" {

/**
 * @brief scePthreadSemInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @param sem   guest slot that receives the handle.
 * @param flag  must be 0 (private semaphore). Non-zero is rejected with EINVAL,
 *              as shadPS4 and SharpEmu do; upstream AnyPS5 ignored it.
 * @param value initial token count, at most SEM_VALUE_MAX.
 * @param name  ignored (debug name only).
 * @return kSceOk, SCE_KERNEL_ERROR_EINVAL (null slot, non-zero flag or value too large) or
 *         SCE_KERNEL_ERROR_ENOMEM. Re-initialising a live slot leaks the old
 *         object, which matches a guest that never destroyed it.
 */
int APS5_VABI scePthreadSemInit(PthreadSem* sem, int flag, unsigned int value,
                                const char* name) noexcept {
    (void)name;
    if (!sem || flag != 0 || value > kSemValueMax)
        return kSceEinval;
    auto* object = new (std::nothrow) PthreadSemPrivate(value);
    if (!object)
        return SyncWords::kSceEnomem;
    std::atomic_ref<PthreadSem>(*sem).store(object, std::memory_order_release);
    return kSceOk;
}

/**
 * @brief scePthreadSemDestroy implementation.
 * @return kSceOk, SCE_KERNEL_ERROR_EINVAL (null, uninitialised or already
 *         destroyed) or SCE_KERNEL_ERROR_EBUSY while a thread is blocked in a
 *         wait on this semaphore.
 */
int APS5_VABI scePthreadSemDestroy(PthreadSem* sem) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object)
        return kSceEinval;
    if (object->waiters.load(std::memory_order_acquire) != 0)
        return kSceEbusy;
    // CAS so two racing destroys free the object exactly once.
    PthreadSem expected = object;
    if (!std::atomic_ref<PthreadSem>(*sem).compare_exchange_strong(
            expected, DestroyedSem(), std::memory_order_acq_rel, std::memory_order_acquire))
        return kSceEinval;
    delete object;
    return kSceOk;
}

/**
 * @brief scePthreadSemPost implementation.
 * @return kSceOk, SCE_KERNEL_ERROR_EINVAL (bad handle) or
 *         SCE_KERNEL_ERROR_EOVERFLOW when the count is at SEM_VALUE_MAX.
 */
int APS5_VABI scePthreadSemPost(PthreadSem* sem) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object)
        return kSceEinval;
    std::uint32_t c = object->count.load(std::memory_order_relaxed);
    do {
        if (c >= kSemValueMax)
            return kSceEoverflow;
    } while (!object->count.compare_exchange_weak(c, c + 1, std::memory_order_seq_cst,
                                                  std::memory_order_relaxed));
    // Publish-before-wake: the count already changed, so a waiter that is
    // about to sleep fails WaitOnAddress's value check instead of missing us.
    if (object->waiters.load(std::memory_order_seq_cst) != 0)
        FutexCore::WakeSingle(&object->count);
    return kSceOk;
}

/**
 * @brief scePthreadSemWait implementation (blocks until a token is taken).
 * @return kSceOk or SCE_KERNEL_ERROR_EINVAL (bad handle).
 */
int APS5_VABI scePthreadSemWait(PthreadSem* sem) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object)
        return kSceEinval;
    return AcquireUntil(object, FutexCore::Deadline{});
}

/**
 * @brief scePthreadSemTrywait implementation.
 * @return kSceOk, SCE_KERNEL_ERROR_EAGAIN when no token is available (POSIX
 *         sem_trywait and FreeBSD report EAGAIN; shadPS4 and SharpEmu agree.
 *         Upstream AnyPS5 064006b6 returned EBUSY, which is what
 *         sceKernelPollSema uses and is wrong for the pthread flavour) or
 *         SCE_KERNEL_ERROR_EINVAL (bad handle).
 */
int APS5_VABI scePthreadSemTrywait(PthreadSem* sem) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object)
        return kSceEinval;
    return TryTake(object) ? kSceOk : kSceEagain;
}

/**
 * @brief scePthreadSemTimedwait implementation.
 * @param usec relative timeout in microseconds; 0 behaves like a poll that
 *             reports SCE_KERNEL_ERROR_ETIMEDOUT instead of EBUSY.
 * @return kSceOk, SCE_KERNEL_ERROR_ETIMEDOUT or SCE_KERNEL_ERROR_EINVAL.
 */
int APS5_VABI scePthreadSemTimedwait(PthreadSem* sem, KernelUseconds usec) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object)
        return kSceEinval;
    const std::uint64_t target =
        FutexCore::NowNanos() + static_cast<std::uint64_t>(usec) * 1000ULL;
    return AcquireUntil(object, FutexCore::Deadline{target});
}

/**
 * @brief scePthreadSemGetvalue implementation.
 * @return kSceOk and the current token count in *value, or
 *         SCE_KERNEL_ERROR_EINVAL (bad handle or null value pointer).
 */
int APS5_VABI scePthreadSemGetvalue(PthreadSem* sem, int* value) noexcept {
    PthreadSemPrivate* object = Resolve(sem);
    if (!object || !value)
        return kSceEinval;
    *value = static_cast<int>(object->count.load(std::memory_order_acquire));
    return kSceOk;
}

}
