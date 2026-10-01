// PortPS5 libkernel counting semaphore (sceKernelSema*) interface.
//
// Subsystem: libkernel Semaphore. The guest handle (KernelSema) is a host
// pointer to KernelSemaPrivate, which today is a std::mutex + condition
// variable object; docs/spec/threading.md schedules the conversion to futex
// words for M4. Every export uses APS5_VABI (System V ABI). Errors are SCE
// kernel codes (0x80020000 | errno) aliased from KernelErrors.hpp.
#ifndef CORE_LIBS_PRX_LIBKERNEL_SEMAPHORE_SEMAPHORE_HPP
#define CORE_LIBS_PRX_LIBKERNEL_SEMAPHORE_SEMAPHORE_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/KernelErrors.hpp"

// Semaphore-local names alias the shared SCE codes (KernelErrors.hpp) so a
// value can only be wrong in one place.
constexpr int KERNEL_SEMA_OK = 0;
constexpr int KERNEL_SEMA_ERROR_EINVAL = SCE_KERNEL_ERROR_EINVAL;
// Returned to a waiter whose semaphore is deleted while it sleeps.
constexpr int KERNEL_SEMA_ERROR_EACCES = SCE_KERNEL_ERROR_EACCES;
constexpr int KERNEL_SEMA_ERROR_EBUSY = SCE_KERNEL_ERROR_EBUSY;
constexpr int KERNEL_SEMA_ERROR_ETIMEDOUT = SCE_KERNEL_ERROR_ETIMEDOUT;

struct KernelSemaPrivate {
    KernelSemaPrivate(std::int32_t initCount, std::int32_t maxCount, std::string name, bool isFifo);

    std::mutex mutex;
    std::condition_variable condition;
    std::string name;
    std::int32_t tokenCount;
    std::int32_t maxCount;
    bool isFifo;
    // Delete handshake (AnyPS5 be127fd0): Delete sets `deleted`, wakes every
    // waiter and blocks until `waiterCount` drops to zero before freeing the
    // object, so a sleeping waiter never touches freed memory. Both fields are
    // guarded by `mutex`.
    bool deleted = false;
    std::int32_t waiterCount = 0;
};

extern "C" {

/**
 * Create a semaphore. @param attr 0/2 = priority order, 1 = FIFO.
 * @return KERNEL_SEMA_OK. Invalid arguments (null out/name, attr > 2, negative
 *         init, non-positive max, init > max) throw std::invalid_argument
 *         (pre-existing behaviour, tracked in docs/spec/threading.md).
 */
int APS5_VABI sceKernelCreateSema(KernelSema* sem, const char* name, uint32_t attr, int init, int max, void* opt);
/**
 * Delete a semaphore: wake all waiters with KERNEL_SEMA_ERROR_EACCES, wait for
 * them to leave, then free it. @return KERNEL_SEMA_OK or KERNEL_SEMA_ERROR_EINVAL (null handle).
 */
int APS5_VABI sceKernelDeleteSema(KernelSema sem);
/** Non-blocking take of `need` tokens. @return KERNEL_SEMA_OK or KERNEL_SEMA_ERROR_EBUSY when fewer are available. */
int APS5_VABI sceKernelPollSema(KernelSema sem, int need);
/** Add `count` tokens. @return KERNEL_SEMA_OK or KERNEL_SEMA_ERROR_EINVAL when it would exceed the maximum. */
int APS5_VABI sceKernelSignalSema(KernelSema sem, int count);
/**
 * Take `need` tokens, blocking. @param time null = forever, else microseconds.
 * @return KERNEL_SEMA_OK, KERNEL_SEMA_ERROR_ETIMEDOUT, or
 *         KERNEL_SEMA_ERROR_EACCES when the semaphore is deleted while waiting.
 */
int APS5_VABI sceKernelWaitSema(KernelSema sem, int need, KernelUseconds* time);

}

#endif
