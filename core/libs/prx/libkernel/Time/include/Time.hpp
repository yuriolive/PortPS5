#ifndef CORE_LIBS_PRX_LIBKERNEL_TIME_INCLUDE_TIME_HPP
#define CORE_LIBS_PRX_LIBKERNEL_TIME_INCLUDE_TIME_HPP

#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {

    std::uint64_t APS5_VABI sceKernelGetProcessTime() noexcept;
    std::uint64_t APS5_VABI sceKernelGetProcessTimeCounter() noexcept;
    std::uint64_t APS5_VABI sceKernelGetProcessTimeCounterFrequency() noexcept;
    int APS5_VABI sceKernelUsleep_nid_postfix(KernelUseconds microseconds) noexcept;
    int APS5_VABI sceKernelNanosleep(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept;
    int APS5_VABI nanosleep_nid_postfix(const KernelTimespec* rqtp, KernelTimespec* rmtp) noexcept;
    int APS5_VABI clock_gettime_nid_postfix(int clockId, KernelTimespec* tp) noexcept;
    int APS5_VABI clock_getres_nid_postfix(int clockId, KernelTimespec* res) noexcept;
    int APS5_VABI gettimeofday_nid_postfix(KernelTimeval* tv, KernelTimezone* tz) noexcept;

}

#endif
