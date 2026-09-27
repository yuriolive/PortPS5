#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libc/include/General.hpp"

// Why scripted-OK, FINISHED in Open: this (non-.native) dialog has no
// UpdateStatus, so unlike .native it must complete in Open to stay
// non-blocking. The first dir name is echoed back via GetResult.
static std::mutex g_dialogMutex;
static int g_status = SAVE_DATA_DIALOG_STATUS_NONE;
static int g_mode = 0;
static int g_result = SAVE_DATA_DIALOG_RESULT_OK;
static int g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
static void* g_user_data = nullptr;
static char g_dir_name[32] = {};

extern "C" {

int APS5_VABI sceSaveDataDialogClose(const void* close_param) noexcept {
 (void)close_param;
 std::lock_guard lock(g_dialogMutex);
 g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogGetResult(void* result) noexcept {
 std::lock_guard lock(g_dialogMutex);
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

int APS5_VABI sceSaveDataDialogIsReadyToDisplay(void) noexcept {
 return 1;
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
   if (name[0] != '\0') {
    std::snprintf(g_dir_name, sizeof(g_dir_name), "%s", name);
    break;
   }
  }
 }
 // Mode 5 (Load) or Mode 8 (List): if no dir was found, script Cancel per spec
 if ((g_mode == 5 || g_mode == 8) && g_dir_name[0] == '\0') {
  g_result = 1; // user cancel
  g_button_id = 2; // cancel
 } else {
  g_result = SAVE_DATA_DIALOG_RESULT_OK;
  g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
 }
 g_status = SAVE_DATA_DIALOG_STATUS_FINISHED;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogProgressBarInc(int target, uint32_t delta) noexcept {
 (void)target;
 (void)delta;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogProgressBarSetValue(int target, uint32_t rate) noexcept {
 (void)target;
 (void)rate;
 return SAVE_DATA_DIALOG_OK;
}

int APS5_VABI sceSaveDataDialogTerminate(void) noexcept {
 std::lock_guard lock(g_dialogMutex);
 g_status = SAVE_DATA_DIALOG_STATUS_NONE;
 g_mode = 0;
 g_result = SAVE_DATA_DIALOG_RESULT_OK;
 g_button_id = SAVE_DATA_DIALOG_BUTTON_ID_OK;
 g_user_data = nullptr;
 g_dir_name[0] = '\0';
 return SAVE_DATA_DIALOG_OK;
}

}
