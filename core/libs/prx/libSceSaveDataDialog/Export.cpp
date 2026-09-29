// core/libs/prx/libSceSaveDataDialog/Export.cpp
// Implementation of non-native libSceSaveDataDialog exports.
// Provides scripted save dialog completion and structured logging for compatibility.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include "SceTypes.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libSceSaveData.native/SaveData.hpp"
#include "prx/libc/include/General.hpp"

// Non-native SaveDataDialog: Open logs dialog.open structured log and completes to FINISHED.
static std::mutex g_dialogMutex;
static int g_status = SAVE_DATA_DIALOG_STATUS_NONE;
static int g_mode = 0;
static int g_disp_type = 0;
static int g_result = SAVE_DATA_DIALOG_RESULT_OK;
static int g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
static void* g_user_data = nullptr;
static char g_dir_name[32] = {};

static bool FindNewestSaveDir(char* outName, std::size_t outSize) {
    std::error_code ec;
    std::filesystem::path root = GetSaveDataBaseDir();
    if (!std::filesystem::is_directory(root, ec) || ec) return false;

    std::filesystem::file_time_type newestTime{};
    std::string newestName;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) {
            auto name = entry.path().filename().string();
            if (!name.empty() && name[0] != '.' && name != "_memory" &&
                (name.size() <= 14 || name.compare(name.size() - 14, 14, ".portps5-prev") != 0) &&
                name.size() < outSize) {
                auto time = entry.last_write_time(ec);
                if (!ec) {
                    if (newestName.empty() || time > newestTime) {
                        newestTime = time;
                        newestName = std::move(name);
                    }
                }
            }
        }
    }
    if (!newestName.empty()) {
        std::snprintf(outName, outSize, "%s", newestName.c_str());
        return true;
    }
    return false;
}

extern "C" {

// Closes an active save data dialog and resets active state.
// Returns SAVE_DATA_DIALOG_OK on success.
int APS5_VABI sceSaveDataDialogClose(const void* close_param) noexcept {
    (void)close_param;
    std::lock_guard lock(g_dialogMutex);
    if (g_status == SAVE_DATA_DIALOG_STATUS_NONE) {
        return SAVE_DATA_DIALOG_OK;
    }
    g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
    RegisterCommonDialogActive_nid_no_patch(false);
    return SAVE_DATA_DIALOG_OK;
}

// Retrieves the result and button selection of the completed dialog.
// Returns SAVE_DATA_DIALOG_OK on success or error code on failure.
int APS5_VABI sceSaveDataDialogGetResult(void* result) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (g_status != SAVE_DATA_DIALOG_STATUS_FINISHED) {
        return SAVE_DATA_DIALOG_ERROR_INVALID_STATE;
    }
    if (result == nullptr) {
        return SAVE_DATA_DIALOG_ERROR_ARG_NULL;
    }
    auto* r = static_cast<SaveDataDialogResult*>(result);
    r->mode = g_mode;
    r->result = g_result;
    r->button_id = g_button_id;
    r->user_data = g_user_data;
    if (r->dir_name != nullptr && g_dir_name[0] != '\0') {
        std::snprintf(static_cast<SaveDataDirName*>(r->dir_name)->data,
                      sizeof(SaveDataDirName::data), "%s", g_dir_name);
    }
    return SAVE_DATA_DIALOG_OK;
}

// Queries current status of save data dialog (NONE, INITIALIZED, RUNNING, FINISHED).
// Returns status integer value.
int APS5_VABI sceSaveDataDialogGetStatus(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    return g_status;
}

// Initializes the save data dialog subsystem.
// Returns SAVE_DATA_DIALOG_OK on success or error code on failure.
int APS5_VABI sceSaveDataDialogInitialize(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (g_status != SAVE_DATA_DIALOG_STATUS_NONE) {
        return SAVE_DATA_DIALOG_ERROR_ALREADY_INITIALIZED;
    }
    g_status = SAVE_DATA_DIALOG_STATUS_INITIALIZED;
    g_mode = 0;
    g_disp_type = 0;
    g_result = SAVE_DATA_DIALOG_RESULT_OK;
    g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
    g_user_data = nullptr;
    g_dir_name[0] = '\0';
    return SAVE_DATA_DIALOG_OK;
}

// Checks if the save data dialog is ready to display visually.
// Returns 1 if ready, 0 otherwise.
int APS5_VABI sceSaveDataDialogIsReadyToDisplay(void) noexcept {
    return 1;
}

// Opens a save data dialog with the requested mode and parameters.
// Returns SAVE_DATA_DIALOG_OK on success or error code on failure.
int APS5_VABI sceSaveDataDialogOpen(const void* param) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (g_status != SAVE_DATA_DIALOG_STATUS_INITIALIZED && g_status != SAVE_DATA_DIALOG_STATUS_FINISHED) {
        return SAVE_DATA_DIALOG_ERROR_INVALID_STATE;
    }
    if (param == nullptr) {
        return SAVE_DATA_DIALOG_ERROR_ARG_NULL;
    }

    const auto* p = static_cast<const SaveDataDialogParam*>(param);
    g_mode = p->mode;
    g_disp_type = p->disp_type;
    g_user_data = p->user_data;
    g_dir_name[0] = '\0';

    const auto* items = static_cast<const SaveDataDialogItems*>(p->items);
    if (items != nullptr && items->dir_names != nullptr) {
        for (std::uint32_t i = 0; i < items->dir_names_num; i++) {
            const char* name = items->dir_names[i].data;
            if (std::memchr(name, '\0', sizeof(SaveDataDirName::data)) != nullptr && name[0] != '\0') {
                std::snprintf(g_dir_name, sizeof(g_dir_name), "%s", name);
                break;
            }
        }
    }

    if (g_mode == 5 || g_mode == 8) {
        const bool requestedNameValid = g_dir_name[0] != '\0' &&
            std::strcmp(g_dir_name, ".") != 0 &&
            std::strcmp(g_dir_name, "..") != 0 &&
            std::strpbrk(g_dir_name, "/\\") == nullptr;

        bool found = false;
        if (g_dir_name[0] != '\0' && requestedNameValid) {
            std::error_code ec;
            if (std::filesystem::is_directory(GetSaveDataBaseDir() / g_dir_name, ec) && !ec) {
                found = true;
            }
        }
        if (!found) {
            found = FindNewestSaveDir(g_dir_name, sizeof(g_dir_name));
        }

        if (!found) {
            g_dir_name[0] = '\0';
            g_result = 1; // user cancel
            g_button_id = 2; // cancel
        } else {
            g_result = SAVE_DATA_DIALOG_RESULT_OK;
            g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
        }
    } else if (g_mode == 1 || g_mode == 2 || g_mode == 0) {
        g_result = SAVE_DATA_DIALOG_RESULT_OK;
        g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
    } else if (g_mode == 3 || g_mode == 4) {
        APS5_LOG_WARN("SaveDataDialog notice/warning dialog shown (mode=%d, disp_type=%d)", g_mode, g_disp_type);
        g_result = SAVE_DATA_DIALOG_RESULT_OK;
        g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
    } else {
        APS5_LOG_ERR("SaveDataDialog: unsupported dialog mode %d requested; returning Cancel", g_mode);
        g_result = 1; // cancel
        g_button_id = 2; // cancel
    }

    APS5_LOG_INFO("dialog.open {lib: \"savedata\", mode: %d, type: %d, result: %d}",
                  g_mode, g_disp_type, g_result);

    g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
    return SAVE_DATA_DIALOG_OK;
}

// Increments progress bar delta for long-running save operations.
// Returns SAVE_DATA_DIALOG_OK on success.
int APS5_VABI sceSaveDataDialogProgressBarInc(int target, uint32_t delta) noexcept {
    (void)target;
    (void)delta;
    return SAVE_DATA_DIALOG_OK;
}

// Sets progress bar absolute rate value for save operations.
// Returns SAVE_DATA_DIALOG_OK on success.
int APS5_VABI sceSaveDataDialogProgressBarSetValue(int target, uint32_t rate) noexcept {
    (void)target;
    (void)rate;
    return SAVE_DATA_DIALOG_OK;
}

// Terminates the save data dialog subsystem and resets internal state.
// Returns SAVE_DATA_DIALOG_OK on success.
int APS5_VABI sceSaveDataDialogTerminate(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    g_status = SAVE_DATA_DIALOG_STATUS_NONE;
    g_mode = 0;
    g_disp_type = 0;
    g_result = SAVE_DATA_DIALOG_RESULT_OK;
    g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
    g_user_data = nullptr;
    g_dir_name[0] = '\0';
    return SAVE_DATA_DIALOG_OK;
}

}
