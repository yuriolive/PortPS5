// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_MUTEX_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_MUTEX_HPP

#include "SceTypes.hpp"

class MutexOperations {
public:
    static int Timedlock(PthreadMutex* mutex, const KernelTimespec* abstime);
};

extern "C" {

/**
 * @brief scePthreadMutexattrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrInit(PthreadMutexattr* attr) noexcept;
/**
 * @brief scePthreadMutexattrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrDestroy(PthreadMutexattr* attr) noexcept;
/**
 * @brief scePthreadMutexattrSettype implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrSettype(PthreadMutexattr* attr, int type) noexcept;
/**
 * @brief scePthreadMutexattrSetprotocol implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexattrSetprotocol(PthreadMutexattr* attr, int protocol) noexcept;
/**
 * @brief scePthreadMutexInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr,
                                 const char* name) noexcept;
/**
 * @brief scePthreadMutexDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
/**
 * @brief scePthreadMutexLock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
/**
 * @brief scePthreadMutexUnlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
/**
 * @brief scePthreadMutexTimedlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexTimedlock(PthreadMutex* mutex, KernelUseconds usec) noexcept;
/**
 * @brief scePthreadMutexTrylock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadMutexTrylock(PthreadMutex* mutex) noexcept;

}

#endif
