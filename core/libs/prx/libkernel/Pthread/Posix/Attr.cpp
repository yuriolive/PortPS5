// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libkernel/Pthread/include/Pthread.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"

/**
 * @brief scePthreadAttrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrInit(PthreadAttr* attr) noexcept;
/**
 * @brief scePthreadAttrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrDestroy(PthreadAttr* attr) noexcept;
/**
 * @brief scePthreadAttrGet implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGet(Pthread thread, PthreadAttr* attr) noexcept;
/**
 * @brief scePthreadAttrGetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetaffinity(const PthreadAttr* attr,
                                                   KernelCpumask* mask) noexcept;
/**
 * @brief scePthreadAttrGetdetachstate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetdetachstate(const PthreadAttr* attr, int* state) noexcept;
/**
 * @brief scePthreadAttrGetguardsize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetguardsize(const PthreadAttr* attr,
                                                    size_t* guard_size) noexcept;
/**
 * @brief scePthreadAttrGetschedparam implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetschedparam(const PthreadAttr* attr,
                                                     KernelSchedParam* param) noexcept;
/**
 * @brief scePthreadAttrGetstackaddr implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetstackaddr(const PthreadAttr* attr,
                                                    void** stack_addr) noexcept;
/**
 * @brief scePthreadAttrGetstacksize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrGetstacksize(const PthreadAttr* attr,
                                                    size_t* stack_size) noexcept;
/**
 * @brief scePthreadAttrSetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetaffinity(PthreadAttr* attr, KernelCpumask mask) noexcept;
/**
 * @brief scePthreadAttrSetdetachstate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetdetachstate(PthreadAttr* attr, int state) noexcept;
/**
 * @brief scePthreadAttrSetguardsize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetguardsize(PthreadAttr* attr, size_t guard_size) noexcept;
/**
 * @brief scePthreadAttrSetinheritsched implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetinheritsched(PthreadAttr* attr, int inherit_sched) noexcept;
/**
 * @brief scePthreadAttrSetschedparam implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetschedparam(PthreadAttr* attr,
                                                     const KernelSchedParam* param) noexcept;
/**
 * @brief scePthreadAttrSetschedpolicy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetschedpolicy(PthreadAttr* attr, int policy) noexcept;
/**
 * @brief scePthreadAttrSetstacksize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadAttrSetstacksize(PthreadAttr* attr, size_t stack_size) noexcept;

extern "C" {

/**
 * @brief pthread_attr_destroy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_destroy_nid_postfix(PthreadAttr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadAttrDestroy(attr));
}

/**
 * @brief pthread_attr_get_np_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_get_np_nid_postfix(Pthread thread, PthreadAttr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadAttrGet(thread, attr));
}

/**
 * @brief pthread_attr_getdetachstate_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getdetachstate_nid_postfix(const PthreadAttr* attr, int* state) noexcept {
    return SyncWords::ToPosix(scePthreadAttrGetdetachstate(attr, state));
}

/**
 * @brief pthread_attr_getguardsize_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getguardsize_nid_postfix(const PthreadAttr* attr,
                                                    size_t* guard_size) noexcept {
    return SyncWords::ToPosix(scePthreadAttrGetguardsize(attr, guard_size));
}

/**
 * @brief pthread_attr_getschedparam_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getschedparam_nid_postfix(const PthreadAttr* attr,
                                                     KernelSchedParam* param) noexcept {
    return SyncWords::ToPosix(scePthreadAttrGetschedparam(attr, param));
}

/**
 * @brief pthread_attr_getschedpolicy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getschedpolicy_nid_postfix(const PthreadAttr* attr, int* policy) noexcept {
    if (!attr || !*attr || !policy)
        return SyncWords::kPosixEinval;
    // sce layer stores policy in the attr; read directly (cold path).
    *policy = (*attr)->_schedpolicy;
    return 0;
}

/**
 * @brief pthread_attr_getstack_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getstack_nid_postfix(const PthreadAttr* __restrict attr,
                                                void** __restrict stack_addr,
                                                size_t* __restrict stack_size) noexcept {
    if (!attr || !*attr || !stack_addr || !stack_size)
        return SyncWords::kPosixEinval;
    *stack_addr = (*attr)->stackAddress;
    *stack_size = (*attr)->_stacksize;
    return 0;
}

/**
 * @brief pthread_attr_getstacksize_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_getstacksize_nid_postfix(const PthreadAttr* attr,
                                                    size_t* stack_size) noexcept {
    return SyncWords::ToPosix(scePthreadAttrGetstacksize(attr, stack_size));
}

/**
 * @brief pthread_attr_init_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_init_nid_postfix(PthreadAttr* attr) noexcept {
    return SyncWords::ToPosix(scePthreadAttrInit(attr));
}

/**
 * @brief pthread_attr_setdetachstate_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setdetachstate_nid_postfix(PthreadAttr* attr, int state) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetdetachstate(attr, state));
}

/**
 * @brief pthread_attr_setguardsize_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setguardsize_nid_postfix(PthreadAttr* attr, size_t guard_size) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetguardsize(attr, guard_size));
}

/**
 * @brief pthread_attr_setinheritsched_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setinheritsched_nid_postfix(PthreadAttr* attr, int inherit_sched) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetinheritsched(attr, inherit_sched));
}

/**
 * @brief pthread_attr_setschedparam_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setschedparam_nid_postfix(PthreadAttr* attr,
                                                     const KernelSchedParam* param) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetschedparam(attr, param));
}

/**
 * @brief pthread_attr_setschedpolicy_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setschedpolicy_nid_postfix(PthreadAttr* attr, int policy) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetschedpolicy(attr, policy));
}

/**
 * @brief pthread_attr_setstacksize_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_attr_setstacksize_nid_postfix(PthreadAttr* attr, size_t stack_size) noexcept {
    return SyncWords::ToPosix(scePthreadAttrSetstacksize(attr, stack_size));
}

}
