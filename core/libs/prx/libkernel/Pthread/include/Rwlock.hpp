// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_RWLOCK_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_RWLOCK_HPP

#include "SceTypes.hpp"

extern "C" {

/**
 * @brief scePthreadRwlockattrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockattrInit(PthreadRwlockattr* attr) noexcept;
/**
 * @brief scePthreadRwlockattrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockattrDestroy(PthreadRwlockattr* attr) noexcept;
/**
 * @brief scePthreadRwlockattrSettype implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockattrSettype(PthreadRwlockattr* attr, int type) noexcept;
/**
 * @brief scePthreadRwlockInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockInit(PthreadRwlock* rwlock, const PthreadRwlockattr* attr,
                                  const char* name) noexcept;
/**
 * @brief scePthreadRwlockDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockDestroy(PthreadRwlock* rwlock) noexcept;
/**
 * @brief scePthreadRwlockRdlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockRdlock(PthreadRwlock* rwlock) noexcept;
/**
 * @brief scePthreadRwlockTryrdlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockTryrdlock(PthreadRwlock* rwlock) noexcept;
/**
 * @brief scePthreadRwlockWrlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockWrlock(PthreadRwlock* rwlock) noexcept;
/**
 * @brief scePthreadRwlockTrywrlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockTrywrlock(PthreadRwlock* rwlock) noexcept;
/**
 * @brief scePthreadRwlockUnlock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRwlockUnlock(PthreadRwlock* rwlock) noexcept;

}

#endif
