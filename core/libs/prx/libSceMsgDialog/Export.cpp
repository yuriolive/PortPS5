#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
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

struct MsgDialogResultLayout {
    std::int32_t mode = 0;
    std::int32_t result = 0;
    std::int32_t button_id = 1; // YES / OK
    std::uint32_t pad0 = 0;
    void* user_data = nullptr;
    char reserved[32] = {};
};

static std::mutex g_dialogMutex;
static bool g_initialized = false;
static int g_status = MSG_DIALOG_STATUS_NONE;
static int g_mode = 0;
static void* g_userData = nullptr;

extern "C" {

int APS5_VABI sceMsgDialogClose(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (!g_initialized) {
        return MSG_DIALOG_ERROR_NOT_INITIALIZED;
    }
    g_status = MSG_DIALOG_STATUS_FINISHED;
    return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogGetResult(void* result) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (!g_initialized) {
        return MSG_DIALOG_ERROR_NOT_INITIALIZED;
    }
    if (result == nullptr) {
        return MSG_DIALOG_ERROR_ARG_NULL;
    }
    if (g_status != MSG_DIALOG_STATUS_FINISHED) {
        return MSG_DIALOG_ERROR_INVALID_STATE;
    }
    auto* r = static_cast<MsgDialogResultLayout*>(result);
    r->mode = g_mode;
    r->result = 0;
    r->button_id = 1; // Yes / OK default button
    r->user_data = g_userData;
    return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogGetStatus(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    return g_status;
}

int APS5_VABI sceMsgDialogInitialize(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (g_initialized) {
        return MSG_DIALOG_ERROR_ALREADY_INITIALIZED;
    }
    g_initialized = true;
    g_status = MSG_DIALOG_STATUS_INITIALIZED;
    g_mode = 0;
    g_userData = nullptr;
    return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogOpen(const void* param) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (!g_initialized) {
        return MSG_DIALOG_ERROR_NOT_INITIALIZED;
    }
    if (g_status == MSG_DIALOG_STATUS_RUNNING) {
        return MSG_DIALOG_ERROR_INVALID_STATE;
    }
    if (param == nullptr) {
        return MSG_DIALOG_ERROR_ARG_NULL;
    }
    // param base layout starts with mode / user_data pointer
    g_mode = *static_cast<const int*>(param);
    g_status = MSG_DIALOG_STATUS_RUNNING;
    return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogProgressBarInc(int target, uint32_t delta) noexcept {
    (void)target;
    (void)delta;
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
    std::lock_guard lock(g_dialogMutex);
    g_initialized = false;
    g_status = MSG_DIALOG_STATUS_NONE;
    g_mode = 0;
    g_userData = nullptr;
    return MSG_DIALOG_OK;
}

int APS5_VABI sceMsgDialogUpdateStatus(void) noexcept {
    std::lock_guard lock(g_dialogMutex);
    if (!g_initialized) {
        return MSG_DIALOG_ERROR_NOT_INITIALIZED;
    }
    if (g_status == MSG_DIALOG_STATUS_RUNNING) {
        g_status = MSG_DIALOG_STATUS_FINISHED;
    }
    return g_status;
}

}
