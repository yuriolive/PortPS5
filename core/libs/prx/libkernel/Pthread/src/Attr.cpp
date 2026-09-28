// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include "prx/libkernel/Pthread/include/Pthread.hpp"
#include "prx/libc/include/General.hpp"
#include <new>

static constexpr int SCE_OK = 0;
static constexpr int SCE_KERNEL_ERROR_EINVAL = static_cast<int>(0x80020016u);
static constexpr int SCE_KERNEL_ERROR_ENOMEM = static_cast<int>(0x8002000Cu);

static constexpr std::size_t DEFAULT_STACK_SIZE = 1u << 20;
static constexpr int DETACH_JOINABLE = 0;
static constexpr int DETACH_DETACHED = 1;
static constexpr int SCHED_FIFO_PS5 = 1;

#ifdef _WIN32
#include <windows.h>
#include <limits>
#endif

extern "C" {

/**
 * @brief scePthreadAttrInit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrInit(PthreadAttr* attr) noexcept {
    if (!attr)
        return SCE_KERNEL_ERROR_EINVAL;
    auto* p = new (std::nothrow) PthreadAttrPrivate{};
    if (!p)
        return SCE_KERNEL_ERROR_ENOMEM;
    p->_stacksize = DEFAULT_STACK_SIZE;
    p->_detachstate = DETACH_JOINABLE;
    p->_schedpriority = 700;
    p->_schedpolicy = SCHED_FIFO_PS5;
    p->_inheritsched = 4;
    p->_affinity = 0;
    p->_guardsize = 4096;
    p->stackAddress = nullptr;
    *attr = p;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrDestroy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrDestroy(PthreadAttr* attr) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    delete *attr;
    *attr = nullptr;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetdetachstate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetdetachstate(PthreadAttr* attr, int detachstate) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    if (detachstate != DETACH_JOINABLE && detachstate != DETACH_DETACHED)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_detachstate = detachstate;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetschedparam implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetschedparam(PthreadAttr* attr, const KernelSchedParam* param) noexcept {
    if (!attr || !*attr || !param)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_schedpriority = param->sched_priority;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetstacksize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetstacksize(PthreadAttr* attr, std::size_t stacksize) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    if (stacksize < 16384)
        return SCE_KERNEL_ERROR_EINVAL;
#ifdef _WIN32
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (stacksize % system.dwPageSize != 0 || stacksize > std::numeric_limits<unsigned>::max())
        return SCE_KERNEL_ERROR_EINVAL;
#endif
    (*attr)->_stacksize = stacksize;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetstack implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetstack(const PthreadAttr* attr, void** stackaddr,
                                     std::size_t* stacksize) noexcept {
    if (!attr || !*attr || !stackaddr || !stacksize)
        return SCE_KERNEL_ERROR_EINVAL;
    *stackaddr = (*attr)->stackAddress;
    *stacksize = (*attr)->_stacksize;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGet implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGet(Pthread thread, PthreadAttr* attr) noexcept {
    if (!thread || !attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_stacksize = thread->stackSize;
    (*attr)->stackAddress = thread->stackAddress;
    (*attr)->_detachstate = thread->_detached ? DETACH_DETACHED : DETACH_JOINABLE;
    (*attr)->_schedpriority = 700;
    (*attr)->_schedpolicy = SCHED_FIFO_PS5;
    (*attr)->_inheritsched = 4;
    (*attr)->_affinity = thread->affinityMask;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetaffinity(const PthreadAttr* attr, KernelCpumask* mask) noexcept {
    // Recorded, not applied (threading.md: guest masks name console cores).
    if (!attr || !*attr || !mask)
        return SCE_KERNEL_ERROR_EINVAL;
    *mask = (*attr)->_affinity;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetdetachstate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetdetachstate(const PthreadAttr* attr, int* state) noexcept {
    if (!attr || !*attr || !state)
        return SCE_KERNEL_ERROR_EINVAL;
    *state = (*attr)->_detachstate;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetguardsize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetguardsize(const PthreadAttr* attr, size_t* guard_size) noexcept {
    if (!attr || !*attr || !guard_size)
        return SCE_KERNEL_ERROR_EINVAL;
    *guard_size = (*attr)->_guardsize;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetschedparam implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetschedparam(const PthreadAttr* attr, KernelSchedParam* param) noexcept {
    if (!attr || !*attr || !param)
        return SCE_KERNEL_ERROR_EINVAL;
    param->sched_priority = (*attr)->_schedpriority;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetsolosched implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetsolosched(const PthreadAttr* attr, int* solosched) noexcept {
    (void)attr;
    (void)solosched;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief scePthreadAttrGetstackaddr implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetstackaddr(const PthreadAttr* attr, void** stack_addr) noexcept {
    if (!attr || !*attr || !stack_addr)
        return SCE_KERNEL_ERROR_EINVAL;
    *stack_addr = (*attr)->stackAddress;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrGetstacksize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrGetstacksize(const PthreadAttr* attr, size_t* stack_size) noexcept {
    if (!attr || !*attr || !stack_size)
        return SCE_KERNEL_ERROR_EINVAL;
    *stack_size = (*attr)->_stacksize;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetaffinity(PthreadAttr* attr, KernelCpumask mask) noexcept {
    // Recorded, not applied (see above).
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_affinity = mask;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetguardsize implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetguardsize(PthreadAttr* attr, size_t guard_size) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_guardsize = guard_size;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetinheritsched implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetinheritsched(PthreadAttr* attr, int inherit_sched) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_inheritsched = inherit_sched;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetschedpolicy implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetschedpolicy(PthreadAttr* attr, int policy) noexcept {
    if (!attr || !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->_schedpolicy = policy;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetsolosched implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetsolosched(PthreadAttr* attr, int solosched) noexcept {
    (void)attr;
    (void)solosched;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief scePthreadAttrSetstack implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetstack(PthreadAttr* attr, void* addr, size_t size) noexcept {
    if (!attr || !*attr || !addr)
        return SCE_KERNEL_ERROR_EINVAL;
    if (size < 16384)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->stackAddress = addr;
    (*attr)->_stacksize = size;
    return SCE_OK;
}

/**
 * @brief scePthreadAttrSetstackaddr implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadAttrSetstackaddr(PthreadAttr* attr, void* addr) noexcept {
    if (!attr || !*attr || !addr)
        return SCE_KERNEL_ERROR_EINVAL;
    (*attr)->stackAddress = addr;
    return SCE_OK;
}

}
