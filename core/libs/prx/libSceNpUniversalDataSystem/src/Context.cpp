#include <cstdint>

#include "NpUniversalDataSystem.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpUniversalDataSystemCreateContext(int* context, int user_id, uint32_t service_label, uint64_t options) noexcept {
 if (context == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 (void)user_id;
 (void)service_label;
 (void)options;
 *context = NP_UNIVERSAL_DATA_SYSTEM_CONTEXT_DEFAULT;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemDestroyContext(int context) noexcept {
 (void)context;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemRegisterContext(int context, int handle, uint64_t options) noexcept {
 (void)context;
 (void)handle;
 (void)options;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

}
