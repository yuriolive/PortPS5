// core/libs/prx/libSceCommonDialog/Export.cpp
// Implementation of libSceCommonDialog system services.
// Tracks common dialog initialization and global active state across dialog types.

#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libSceCommonDialog/CommonDialog.hpp"
#include "prx/libc/include/General.hpp"

static bool g_initialized = false;

extern "C" {

// Initializes common dialog subsystem.
// Returns COMMON_DIALOG_OK on success or COMMON_DIALOG_ERROR_ALREADY_INITIALIZED if initialized.
int APS5_VABI sceCommonDialogInitialize(void) noexcept {
	if (g_initialized) {
		return COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
	}
	g_initialized = true;
	return COMMON_DIALOG_OK;
}

// Queries whether any common dialog (MsgDialog, SaveDataDialog, etc.) is currently active.
// Returns true if any common dialog is active, false otherwise.
bool APS5_VABI sceCommonDialogIsUsed(void) noexcept {
    // Reflects whether any system dialog (SaveDataDialog, MsgDialog, etc.) is active.
    (void)g_initialized;
    return IsAnyCommonDialogActive_nid_no_patch();
}

}
