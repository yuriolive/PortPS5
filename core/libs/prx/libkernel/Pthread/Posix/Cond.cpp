// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include "prx/libkernel/Pthread/include/Cond.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include <cstdint>

extern "C" {

/**
 * @brief pthread_cond_broadcast_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_cond_broadcast_nid_postfix(PthreadCond* cond) noexcept {
    return SyncWords::ToPosix(scePthreadCondBroadcast(cond));
}

/**
 * @brief pthread_cond_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_cond_init_nid_postfix(PthreadCond* cond,
                                            const PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondInit(cond, attr, nullptr));
}

/**
 * @brief pthread_cond_signal_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_cond_signal_nid_postfix(PthreadCond* cond) noexcept {
    return SyncWords::ToPosix(scePthreadCondSignal(cond));
}

/**
 * @brief pthread_cond_timedwait_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_cond_timedwait_nid_postfix(PthreadCond* cond, PthreadMutex* mutex,
                                                 const KernelTimespec* abstime) noexcept {
    return SyncWords::ToPosix(CondOperations::AbsoluteTimedwait(cond, mutex, abstime));
}

/**
 * @brief pthread_cond_wait_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_cond_wait_nid_postfix(PthreadCond* cond, PthreadMutex* mutex) noexcept {
    return SyncWords::ToPosix(scePthreadCondWait(cond, mutex));
}

/**
 * @brief pthread_condattr_destroy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_condattr_destroy_nid_postfix(PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrDestroy(attr));
}

/**
 * @brief pthread_condattr_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_condattr_init_nid_postfix(PthreadCondattr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrInit(attr));
}

/**
 * @brief pthread_condattr_setclock_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_condattr_setclock_nid_postfix(PthreadCondattr* attr,
                                                    KernelClockid clock_id) noexcept {
    return SyncWords::ToPosix(scePthreadCondattrSetclock(attr, clock_id));
}

}
