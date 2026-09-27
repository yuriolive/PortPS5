#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libSceCommonDialog/CommonDialog.hpp"
#include "prx/libc/include/General.hpp"

static bool g_initialized = false;

extern "C" {

int APS5_VABI sceCommonDialogInitialize(void) noexcept {
 if (g_initialized) {
  return COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
 }
 g_initialized = true;
 return COMMON_DIALOG_OK;
}

bool APS5_VABI sceCommonDialogIsUsed(void) noexcept {
 // Why always false in M1: each .prx has its own statics, so CommonDialog
 // cannot see Msg/SaveData RUNNING state without a shared (libc) flag, which
 // is M2 work. False is non-blocking and safe for boot; titles that poll see
 // no dialog open and continue offline.
 (void)g_initialized;
 return false;
}

}
