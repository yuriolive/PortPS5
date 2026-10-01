// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "../include/ThreadLifecycle.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"

/**
 * @brief scePthreadCreate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr,
                                          PthreadEntry entry, void* arg, const char* name) noexcept;
/**
 * @brief scePthreadJoin implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadJoin(Pthread thread, void** value) noexcept;
/**
 * @brief scePthreadDetach implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadDetach(Pthread thread) noexcept;
/**
 * @brief scePthreadSelf implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" Pthread APS5_VABI scePthreadSelf() noexcept;
/**
 * @brief scePthreadYield implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" void APS5_VABI scePthreadYield() noexcept;
/**
 * @brief scePthreadEqual implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadEqual(Pthread thread1, Pthread thread2) noexcept;
/**
 * @brief scePthreadRename implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadRename(Pthread thread, const char* name) noexcept;
/**
 * @brief scePthreadGetprio implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadGetprio(Pthread thread, int* prio) noexcept;
/**
 * @brief scePthreadSetprio implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadSetprio(Pthread thread, int prio) noexcept;
/**
 * @brief scePthreadGetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadGetaffinity(Pthread thread, KernelCpumask* mask) noexcept;
/**
 * @brief scePthreadSetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
extern "C" int APS5_VABI scePthreadSetaffinity(Pthread thread, KernelCpumask mask) noexcept;

extern "C" {

/**
 * @brief pthread_create_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_create_nid_postfix(Pthread* thread, const PthreadAttr* attr,
                                         pthread_entry_func_t entry, void* arg) noexcept {
    if (!thread || !entry)
        return SyncWords::kPosixEinval;
    // pthread_entry_func_t is SysV-compatible with PthreadEntry on both
    // ABIs here (both plain function pointers); forward directly.
    const int rc = scePthreadCreate(thread, attr, reinterpret_cast<PthreadEntry>(entry), arg,
                                    nullptr);
    return SyncWords::ToPosix(rc);
}

/**
 * @brief pthread_create_name_np_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_create_name_np_nid_postfix(Pthread* thread, const PthreadAttr* attr,
                                                 pthread_entry_func_t entry, void* arg,
                                                 const char* name) noexcept {
    if (!thread || !entry)
        return SyncWords::kPosixEinval;
    const int rc = scePthreadCreate(thread, attr, reinterpret_cast<PthreadEntry>(entry), arg,
                                    name);
    if (rc != 0)
        return SyncWords::ToPosix(rc);
    if (name && *thread)
        scePthreadRename(*thread, name);
    return 0;
}

/**
 * @brief pthread_detach_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_detach_nid_postfix(Pthread thread) noexcept {
    return SyncWords::ToPosix(scePthreadDetach(thread));
}

/**
 * @brief pthread_exit_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
void APS5_VABI pthread_exit_nid_postfix(void* value) noexcept {
    scePthreadExit(value);
}

/**
 * @brief pthread_getschedparam_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_getschedparam_nid_postfix(Pthread thread, int* policy,
                                                KernelSchedParam* param) noexcept {
    if (!policy || !param)
        return SyncWords::kPosixEinval;
    int prio = 0;
    const int rc = scePthreadGetprio(thread, &prio);
    if (rc != 0)
        return SyncWords::ToPosix(rc);
    *policy = 1;  // SCHED_FIFO_PS5.
    param->sched_priority = prio;
    return 0;
}

/**
 * @brief pthread_join_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_join_nid_postfix(Pthread thread, void** value) noexcept {
    return SyncWords::ToPosix(scePthreadJoin(thread, value));
}

/**
 * @brief pthread_rename_np_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_rename_np_nid_postfix(Pthread thread, const char* name) noexcept {
    return SyncWords::ToPosix(scePthreadRename(thread, name));
}

/**
 * @brief pthread_self_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
Pthread APS5_VABI pthread_self_nid_postfix(void) noexcept {
    return scePthreadSelf();
}

/**
 * @brief pthread_equal_nid_postfix implementation (AnyPS5 5e708c16).
 * Invoked by guest code using System V ABI calling convention.
 * @return Non-zero when both handles name the same thread, 0 otherwise. A
 *         handle is its identity, so pointer equality is the whole test; two
 *         null handles compare equal like POSIX pthread_equal on equal values.
 */
int APS5_VABI pthread_equal_nid_postfix(Pthread first, Pthread second) noexcept {
    return first == second ? 1 : 0;
}

/**
 * @brief sched_yield_nid_postfix implementation (AnyPS5 5e708c16).
 * Invoked by guest code using System V ABI calling convention.
 * Shares scePthreadYield so both yield paths use SwitchToThread (spec:
 * threading.md "sceKernelUsleep(0) and scePthreadYield").
 * @return Always 0.
 */
int APS5_VABI sched_yield_nid_postfix(void) noexcept {
    scePthreadYield();
    return 0;
}

/**
 * @brief pthread_setcancelstate_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_setcancelstate_nid_postfix(int state, int* old_state) noexcept {
    (void)state;
    (void)old_state;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief pthread_setprio_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_setprio_nid_postfix(Pthread thread, int prio) noexcept {
    return SyncWords::ToPosix(scePthreadSetprio(thread, prio));
}

/**
 * @brief pthread_setschedparam_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI pthread_setschedparam_nid_postfix(Pthread thread, int policy,
                                                const KernelSchedParam* param) noexcept {
    (void)policy;
    if (!param)
        return SyncWords::kPosixEinval;
    return SyncWords::ToPosix(scePthreadSetprio(thread, param->sched_priority));
}

/**
 * @brief pthread_yield_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
void APS5_VABI pthread_yield_nid_postfix(void) noexcept {
    scePthreadYield();
}

}
