#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why scripted RUNNING->FINISHED: offline has no UI, so user-message dialogs
// report the default button (Yes/OK) and progress bars finish immediately.
// Open sets RUNNING; the title polls UpdateStatus to see FINISHED, so it
// observes the same sequence as on console instead of blocking in Open.
static constexpr int MSG_DIALOG_OK = 0;
static constexpr int MSG_DIALOG_STATUS_NONE = 0;
static constexpr int MSG_DIALOG_STATUS_INITIALIZED = 1;
static constexpr int MSG_DIALOG_STATUS_RUNNING = 2;
static constexpr int MSG_DIALOG_STATUS_FINISHED = 3;
static constexpr int MSG_DIALOG_ERROR_NOT_INITIALIZED = static_cast<int>(0x80B80003);
static constexpr int MSG_DIALOG_ERROR_ALREADY_INITIALIZED = static_cast<int>(0x80B80004);
static constexpr int MSG_DIALOG_ERROR_INVALID_STATE = static_cast<int>(0x80B80006);
static constexpr int MSG_DIALOG_ERROR_ARG_NULL = static_cast<int>(0x80B8000D);

static bool g_initialized = false;
static int g_status = MSG_DIALOG_STATUS_NONE;

extern "C" {

int APS5_VABI sceMsgDialogClose(void) noexcept {
 if (!g_initialized) {
  return MSG_DIALOG_ERROR_NOT_INITIALIZED;
 }
 g_status = MSG_DIALOG_STATUS_FINISHED;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogGetResult(void* result) noexcept {
 if (!g_initialized) {
  return MSG_DIALOG_ERROR_NOT_INITIALIZED;
 }
 if (result == nullptr) {
  return MSG_DIALOG_ERROR_ARG_NULL;
 }
 if (g_status != MSG_DIALOG_STATUS_FINISHED) {
  return MSG_DIALOG_ERROR_INVALID_STATE;
 }
 // Why untouched: MsgDialog result layout is opaque here (void*); the return
 // code OK plus FINISHED status carries the scripted Yes/OK for boot.
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogGetStatus(void) noexcept {
 return g_status;
}

int APS5_VABI sceMsgDialogInitialize(void) noexcept {
 if (g_initialized) {
  return MSG_DIALOG_ERROR_ALREADY_INITIALIZED;
 }
 g_initialized = true;
 g_status = MSG_DIALOG_STATUS_INITIALIZED;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogOpen(const void* param) noexcept {
 if (!g_initialized) {
  return MSG_DIALOG_ERROR_NOT_INITIALIZED;
 }
 if (g_status == MSG_DIALOG_STATUS_RUNNING) {
  return MSG_DIALOG_ERROR_INVALID_STATE;
 }
 if (param == nullptr) {
  return MSG_DIALOG_ERROR_ARG_NULL;
 }
 g_status = MSG_DIALOG_STATUS_RUNNING;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogProgressBarInc(int target, uint32_t delta) noexcept {
 (void)target;
 (void)delta;
 // Why OK: progress bars finish immediately offline.
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogProgressBarSetMsg(int target, const char* msg) noexcept {
 (void)target;
 (void)msg;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogProgressBarSetValue(int target, uint32_t rate) noexcept {
 (void)target;
 (void)rate;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogTerminate(void) noexcept {
 g_initialized = false;
 g_status = MSG_DIALOG_STATUS_NONE;
 return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogUpdateStatus(void) noexcept {
 if (!g_initialized) {
  return MSG_DIALOG_ERROR_NOT_INITIALIZED;
 }
 if (g_status == MSG_DIALOG_STATUS_RUNNING) {
  g_status = MSG_DIALOG_STATUS_FINISHED;
 }
 return g_status;
}

}
