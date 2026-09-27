#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpSessionSignalingInitialize(void* param) noexcept {
 (void)param;
 // Why OK: offline has no session service; init succeeds so boot continues.
 return 0;
}

}
