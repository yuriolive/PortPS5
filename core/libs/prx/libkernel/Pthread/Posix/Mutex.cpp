// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include "../include/Mutex.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include <cstdint>

extern "C" {

/**
 * @brief pthread_mutex_destroy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_destroy_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexDestroy(mutex));
}

/**
 * @brief pthread_mutex_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_init_nid_postfix(PthreadMutex* mutex,
                                             const PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexInit(mutex, attr, nullptr));
}

/**
 * @brief pthread_mutex_lock_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_lock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexLock(mutex));
}

/**
 * @brief pthread_mutex_timedlock_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_timedlock_nid_postfix(PthreadMutex* mutex,
                                                  const KernelTimespec* abstime) noexcept {
    return SyncWords::ToPosix(MutexOperations::Timedlock(mutex, abstime));
}

/**
 * @brief pthread_mutex_trylock_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_trylock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexTrylock(mutex));
}

/**
 * @brief pthread_mutex_unlock_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutex_unlock_nid_postfix(PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadMutexUnlock(mutex));
}

/**
 * @brief pthread_mutexattr_destroy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutexattr_destroy_nid_postfix(PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrDestroy(attr));
}

/**
 * @brief pthread_mutexattr_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutexattr_init_nid_postfix(PthreadMutexattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrInit(attr));
}

/**
 * @brief pthread_mutexattr_setprotocol_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutexattr_setprotocol_nid_postfix(PthreadMutexattr* attr,
                                                        int protocol) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrSetprotocol(attr, protocol));
}

/**
 * @brief pthread_mutexattr_settype_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_mutexattr_settype_nid_postfix(PthreadMutexattr* attr, int type) noexcept {
    return SyncWords::ToPosix(scePthreadMutexattrSettype(attr, type));
}

}
