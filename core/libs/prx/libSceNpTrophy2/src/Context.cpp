#include <cstdint>

#include "prx/libSceNpTrophy2/include/NpTrophy2.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpTrophy2CreateContext(int* context, int user_id, uint32_t service_label, uint64_t options) noexcept {
 if (context == nullptr) {
  return SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT;
 }
 (void)user_id;
 (void)service_label;
 (void)options;
 *context = NP_TROPHY2_CONTEXT_DEFAULT;
 return SCE_NP_TROPHY2_OK;
}

int APS5_VABI sceNpTrophy2DestroyContext(int context) noexcept {
 (void)context;
 return SCE_NP_TROPHY2_OK;
}

int APS5_VABI sceNpTrophy2RegisterContext(int context, int handle, uint64_t options) noexcept {
 (void)context;
 (void)handle;
 (void)options;
 return SCE_NP_TROPHY2_OK;
}

}
