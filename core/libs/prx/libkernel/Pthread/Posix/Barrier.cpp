// PortPS5 libkernel: POSIX pthread_barrier_* (AnyPS5 5e708c16, adapted).
//
// Subsystem: libkernel threading (docs/spec/threading.md, "Handle objects").
// The guest slot (pthread_barrier_t, 8 bytes) holds a host pointer to an object
// made of futex words only; there is no heap std::mutex / condition variable.
// Upstream used both, and aborted (a throwing NotImplemented) on any non-null
// attr; here the attr is accepted and ignored because the only attribute is
// pshared, which has no host meaning.
//
// Algorithm: one 64-bit `state` word packs `generation << 32 | arrived`. An
// arriving thread CASes arrived+1 into it. The thread that makes arrived reach
// `count` publishes (generation+1, 0) in the same CAS and wakes everyone; every
// other thread sleeps on `state` until the generation half changes. Packing
// both halves in one word is what makes a (count+1)-th caller in the same round
// land in the NEXT round instead of being lost or released early (a separate
// arrived counter cannot reset atomically with the generation bump).
//
// WaitOnAddress only compares at sleep entry and is otherwise woken explicitly,
// so the waiter reloads the word and re-sleeps on the value it just read; the
// release-publish of (generation+1) before WakeAll prevents a lost wakeup.
//
// `active` counts threads still inside a wait call. Destroy returns EBUSY while
// it is non-zero. The releaser decrements it last, after its WakeAll, so a
// destroyer that sees 0 can never race a touch of the freed object.
//
// Return codes are raw POSIX errno values (22 EINVAL, 12 ENOMEM, 16 EBUSY) like
// the other pthread_* wrappers; PTHREAD_BARRIER_SERIAL_THREAD is -1 on FreeBSD.

#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/SemBarrierTypes.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"

#include <atomic>
#include <cstdint>
#include <new>

namespace {

constexpr int kSerialThread = -1;  // PTHREAD_BARRIER_SERIAL_THREAD (FreeBSD).
constexpr std::uint64_t kArrivedMask = 0xFFFFFFFFULL;

inline std::uint32_t Arrived(std::uint64_t s) noexcept {
    return static_cast<std::uint32_t>(s & kArrivedMask);
}
inline std::uint32_t Generation(std::uint64_t s) noexcept {
    return static_cast<std::uint32_t>(s >> 32);
}

inline PthreadBarrierPrivate* Resolve(PthreadBarrierPrivate** slot) noexcept {
    if (!slot)
        return nullptr;
    return std::atomic_ref<PthreadBarrierPrivate*>(*slot).load(std::memory_order_acquire);
}

}  // namespace

extern "C" {

/**
 * @brief pthread_barrier_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @param barrier guest slot receiving the handle.
 * @param attr    ignored (pshared only).
 * @param count   number of threads that must arrive; must be non-zero.
 * @return 0, EINVAL (null slot or zero count) or ENOMEM.
 */
int APS5_VABI pthread_barrier_init_nid_postfix(PthreadBarrierPrivate** barrier, const void* attr,
                                               unsigned count) noexcept {
    (void)attr;
    if (!barrier || count == 0)
        return SyncWords::kPosixEinval;
    auto* object = new (std::nothrow) PthreadBarrierPrivate(count);
    if (!object)
        return SyncWords::kPosixEnomem;
    std::atomic_ref<PthreadBarrierPrivate*>(*barrier).store(object, std::memory_order_release);
    return 0;
}

/**
 * @brief pthread_barrier_wait_nid_postfix implementation.
 * Blocks until `count` threads have arrived in the current round.
 * @return PTHREAD_BARRIER_SERIAL_THREAD (-1) for exactly one thread per
 *         round, 0 for the others, EINVAL for a null/uninitialised/destroyed
 *         barrier.
 */
int APS5_VABI pthread_barrier_wait_nid_postfix(PthreadBarrierPrivate** barrier) noexcept {
    PthreadBarrierPrivate* b = Resolve(barrier);
    if (!b)
        return SyncWords::kPosixEinval;
    b->active.fetch_add(1, std::memory_order_acq_rel);

    std::uint64_t s = b->state.load(std::memory_order_acquire);
    for (;;) {
        const std::uint32_t generation = Generation(s);
        const std::uint32_t arrived = Arrived(s) + 1;
        if (arrived == b->count) {
            // Last arrival: open the next generation with zero arrivals in the
            // same CAS, then wake the sleepers. release pairs with the
            // waiters' acquire load of `state`.
            const std::uint64_t next = static_cast<std::uint64_t>(generation + 1u) << 32;
            if (b->state.compare_exchange_weak(s, next, std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
                FutexCore::WakeAll(&b->state);
                b->active.fetch_sub(1, std::memory_order_release);
                return kSerialThread;
            }
            continue;
        }
        const std::uint64_t joined = (static_cast<std::uint64_t>(generation) << 32) | arrived;
        if (!b->state.compare_exchange_weak(s, joined, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            continue;
        // Sleep until the generation moves on. Re-sleep on the freshest value
        // because other arrivals rewrite `state` without waking us.
        for (;;) {
            s = b->state.load(std::memory_order_acquire);
            if (Generation(s) != generation)
                break;
            FutexCore::WaitU64(reinterpret_cast<volatile std::uint64_t*>(&b->state), s,
                               FutexCore::Deadline{});
        }
        b->active.fetch_sub(1, std::memory_order_release);
        return 0;
    }
}

/**
 * @brief pthread_barrier_destroy_nid_postfix implementation.
 * @return 0, EINVAL (null/uninitialised/already destroyed) or EBUSY while a
 *         thread is still inside pthread_barrier_wait.
 */
int APS5_VABI pthread_barrier_destroy_nid_postfix(PthreadBarrierPrivate** barrier) noexcept {
    PthreadBarrierPrivate* b = Resolve(barrier);
    if (!b)
        return SyncWords::kPosixEinval;
    if (b->active.load(std::memory_order_acquire) != 0)
        return SyncWords::kPosixEbusy;
    // CAS so two racing destroys free the object once; the slot reads as
    // uninitialised (EINVAL) afterwards.
    PthreadBarrierPrivate* expected = b;
    if (!std::atomic_ref<PthreadBarrierPrivate*>(*barrier).compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire))
        return SyncWords::kPosixEinval;
    delete b;
    return 0;
}

}
