#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libc/include/General.hpp"

// Why scripted-OK with RUNNING->FINISHED: Open records the first dir name and
// reports RUNNING; the next UpdateStatus completes to FINISHED so titles that
// poll see the console sequence instead of blocking inside Open.
static std::mutex g_dialogMutex;
static int g_status = SAVE_DATA_DIALOG_STATUS_NONE;
static int g_mode = 0;
static int g_result = SAVE_DATA_DIALOG_RESULT_OK;
static int g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
static void* g_user_data = nullptr;
static char g_dir_name[32] = {};

static bool FindExistingSaveDir(char* outName, size_t outSize) {
    std::error_code ec;
    std::filesystem::path root("_sd");
    if (!std::filesystem::is_directory(root, ec)) return false;
    std::filesystem::file_time_type newestTime{};
    std::string newestName;
    std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(root, ec); !ec && it != end;) {
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            if (ec) return false;
            auto name = entry.path().filename().string();
            if (!name.empty() && name[0] != '.' && name[0] != '_' && name.size() < outSize) {
                auto time = entry.last_write_time(ec);
                if (ec) return false;
                if (newestName.empty() || time > newestTime) {
                    newestTime = time;
                    newestName = std::move(name);
                }
            }
        }
        it.increment(ec);
    }
    if (ec) return false;
    if (!newestName.empty()) {
        std::snprintf(outName, outSize, "%s", newestName.c_str());
        return true;
    }
    return false;
}

extern "C" {

int APS5_VABI sceSaveDataDialogInitialize(void) noexcept {
 std::lock_guard lock(g_dialogMutex);
 if (g_status != SAVE_DATA_DIALOG_STATUS_NONE) {
  return SAVE_DATA_DIALOG_ERROR_ALREADY_INITIALIZED;
 }
 g_status = SAVE_DATA_DIALOG_STATUS_INITIALIZED;
 g_mode = 0;
 g_result = SAVE_DATA_DIALOG_RESULT_OK;
 g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
 g_user_data = nullptr;
 g_dir_name[0] = '\0';
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogGetStatus(void) noexcept {
 std::lock_guard lock(g_dialogMutex);
 return g_status;
}

int APS5_VABI sceSaveDataDialogUpdateStatus(void) noexcept {
 std::lock_guard lock(g_dialogMutex);
 if (g_status == SAVE_DATA_DIALOG_STATUS_RUNNING) {
  g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
 }
 return g_status;
}

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
 // Mode 5 (Load) or Mode 8 (List): find newest existing dir, or cancel if none (docs/spec/save-data.md)
 if (g_mode == 5 || g_mode == 8) {
  const bool requestedNameValid = g_dir_name[0] != '\0' &&
   std::strcmp(g_dir_name, ".") != 0 &&
   std::strcmp(g_dir_name, "..") != 0 &&
   std::strpbrk(g_dir_name, "/\\") == nullptr;
  bool found = false;
  if (g_dir_name[0] != '\0') {
   std::error_code ec;
   if (requestedNameValid && std::filesystem::is_directory(std::filesystem::path("_sd") / g_dir_name, ec) && !ec) {
    found = true;
   }
  }
  if (!found && (g_dir_name[0] == '\0' || requestedNameValid)) {
   found = FindExistingSaveDir(g_dir_name, sizeof(g_dir_name));
  }
  if (!found) {
   g_dir_name[0] = '\0';
   g_result = 1; // user cancel
   g_button_id = 2; // cancel
  } else {
   g_result = SAVE_DATA_DIALOG_RESULT_OK;
   g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
  }
 } else {
  g_result = SAVE_DATA_DIALOG_RESULT_OK;
  g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
 }
 g_status = SAVE_DATA_DIALOG_STATUS_RUNNING;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogClose(const void* closeParam) noexcept {
 (void)closeParam;
 std::lock_guard lock(g_dialogMutex);
 if (g_status == SAVE_DATA_DIALOG_STATUS_NONE) {
  return SAVE_DATA_DIALOG_OK;
 }
 g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogIsReadyToDisplay(void) noexcept {
 return 1;
}

int APS5_VABI sceSaveDataDialogTerminate(void) noexcept {
 std::lock_guard lock(g_dialogMutex);
 g_status = SAVE_DATA_DIALOG_STATUS_NONE;
 g_mode = 0;
 g_user_data = nullptr;
 g_dir_name[0] = '\0';
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogProgressBarInc(int target, std::uint32_t delta) noexcept {
 (void)target;
 (void)delta;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogProgressBarSetValue(int target, std::uint32_t rate) noexcept {
 (void)target;
 (void)rate;
 return SAVE_DATA_DIALOG_OK;
}

}
