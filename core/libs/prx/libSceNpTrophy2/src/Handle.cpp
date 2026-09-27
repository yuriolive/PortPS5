#include "prx/libSceNpTrophy2/include/NpTrophy2.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpTrophy2CreateHandle(int* handle) noexcept {
 if (handle == nullptr) {
  return SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT;
 }
 *handle = NP_TROPHY2_HANDLE_DEFAULT;
 return SCE_NP_TROPHY2_OK;
}

int APS5_VABI sceNpTrophy2DestroyHandle(int handle) noexcept {
 (void)handle;
 return SCE_NP_TROPHY2_OK;
}

int APS5_VABI sceNpTrophy2AbortHandle(int handle) noexcept {
 (void)handle;
 return SCE_NP_TROPHY2_OK;
}

}
