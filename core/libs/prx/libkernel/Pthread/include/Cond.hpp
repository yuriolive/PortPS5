// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_COND_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_COND_HPP

#include "SceTypes.hpp"

class CondOperations {
public:
    static int AbsoluteTimedwait(PthreadCond* cond, PthreadMutex* mutex, const KernelTimespec* abstime);
};

extern "C" {

/**
 * @brief scePthreadCondattrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondattrInit(PthreadCondattr* attr) noexcept;
/**
 * @brief scePthreadCondattrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondattrDestroy(PthreadCondattr* attr) noexcept;
/**
 * @brief scePthreadCondattrSetclock implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondattrSetclock(PthreadCondattr* attr, KernelClockid clockId) noexcept;
/**
 * @brief scePthreadCondInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr,
                                const char* name) noexcept;
/**
 * @brief scePthreadCondDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond) noexcept;
/**
 * @brief scePthreadCondSignal implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondSignal(PthreadCond* cond) noexcept;
/**
 * @brief scePthreadCondBroadcast implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondBroadcast(PthreadCond* cond) noexcept;
/**
 * @brief scePthreadCondSignalto implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondSignalto(PthreadCond* cond, Pthread thread) noexcept;
/**
 * @brief scePthreadCondWait implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondWait(PthreadCond* cond, PthreadMutex* mutex) noexcept;
/**
 * @brief scePthreadCondTimedwait implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex,
                                      KernelUseconds usec) noexcept;

}

#endif
