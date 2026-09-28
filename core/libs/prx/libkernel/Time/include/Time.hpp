// PortPS5 libkernel Time subsystem.
// Implements guest time, clock, and sleep primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_TIME_INCLUDE_TIME_HPP
#define CORE_LIBS_PRX_LIBKERNEL_TIME_INCLUDE_TIME_HPP

#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {

/**
 * @brief sceKernelGetProcessTime implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    std::uint64_t APS5_VABI sceKernelGetProcessTime() noexcept;
/**
 * @brief sceKernelGetProcessTimeCounter implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    std::uint64_t APS5_VABI sceKernelGetProcessTimeCounter() noexcept;
/**
 * @brief sceKernelGetProcessTimeCounterFrequency implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    std::uint64_t APS5_VABI sceKernelGetProcessTimeCounterFrequency() noexcept;
/**
 * @brief sceKernelUsleep_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI sceKernelUsleep_nid_postfix(KernelUseconds microseconds) noexcept;
/**
 * @brief sceKernelNanosleep implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI sceKernelNanosleep(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept;
/**
 * @brief nanosleep_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI nanosleep_nid_postfix(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept;
/**
 * @brief clock_gettime_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI clock_gettime_nid_postfix(int clockId, KernelTimespec* tp) noexcept;
/**
 * @brief clock_getres_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI clock_getres_nid_postfix(int clockId, KernelTimespec* res) noexcept;
/**
 * @brief gettimeofday_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
    int APS5_VABI gettimeofday_nid_postfix(KernelTimeval* tv, KernelTimezone* tz) noexcept;

}

#endif
