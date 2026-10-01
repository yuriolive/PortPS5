// core/libs/prx/libSceMsgDialog/Export.cpp
// Headless libSceMsgDialog: NONE -> INITIALIZED -> RUNNING -> FINISHED state machine with no UI.
// A dialog that has buttons is answered automatically after two status polls so a title that waits
// for the user never blocks; dialogs the title ends itself (progress bar, no buttons, wait) stay
// RUNNING until sceMsgDialogClose. All state lives behind one mutex, and every export is APS5_VABI.
//
// Guest struct layouts (SceMsgDialogParam, user message, result) and the SCE_COMMON_DIALOG error
// table follow shadPS4 src/core/libraries/system/msgdialog*.h (GPL-2.0-or-later), which is the
// public oracle for them; see docs/spec/ for the audit note. No SDK header was consulted.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr int kStatusNone = 0;
constexpr int kStatusInitialized = 1;
constexpr int kStatusRunning = 2;
constexpr int kStatusFinished = 3;

constexpr int kOk = 0;
constexpr int kErrNotInitialized = static_cast<int>(0x80B80003u);
constexpr int kErrAlreadyInitialized = static_cast<int>(0x80B80004u);
constexpr int kErrNotFinished = static_cast<int>(0x80B80005u);
constexpr int kErrInvalidState = static_cast<int>(0x80B80006u);
constexpr int kErrParamInvalid = static_cast<int>(0x80B8000Au);
constexpr int kErrNotRunning = static_cast<int>(0x80B8000Bu);
constexpr int kErrArgNull = static_cast<int>(0x80B8000Du);
constexpr int kErrNotSupported = static_cast<int>(0x80B8000Fu);

constexpr std::int32_t kModeUserMsg = 1;
constexpr std::int32_t kModeProgressBar = 2;
constexpr std::int32_t kModeSystemMsg = 3;

// SceMsgDialogButtonType values (shadPS4 msgdialog_ui.h). 4 is not assigned, which is why the
// numeric values must not be guessed from the declaration order.
constexpr std::uint32_t kButtonNone = 2;
constexpr std::uint32_t kButtonWait = 5;
constexpr std::uint32_t kButtonWaitCancel = 6;
constexpr std::uint32_t kButtonYesNoFocusNo = 7;
constexpr std::uint32_t kButtonOkCancelFocusCancel = 8;
constexpr std::uint32_t kButtonTwoButtons = 9;

constexpr std::int32_t kButtonIdInvalid = 0;
constexpr std::int32_t kButtonIdFirst = 1;   // OK, YES and BUTTON1 share the value 1.
constexpr std::int32_t kButtonIdSecond = 2;  // NO, CANCEL and BUTTON2 share the value 2.
constexpr std::int32_t kResultOk = 0;
constexpr std::int32_t kResultUserCanceled = 1;

// Polls (GetStatus or UpdateStatus) an auto-answered dialog stays RUNNING for. Two means a title
// observes RUNNING at least once, as on a console where the user needs a moment to answer.
constexpr int kPollsBeforeFinish = 2;

// SceMsgDialogParam, 0x88 bytes. Pointers are kept as uint64_t so the layout is explicit-width.
struct MsgDialogParam {
    std::uint8_t base[0x30];  // SceCommonDialogBaseParam, opaque here.
    std::uint64_t size;
    std::int32_t mode;
    std::int32_t pad0;
    std::uint64_t userMsgParam;
    std::uint64_t progressBarParam;
    std::uint64_t systemMsgParam;
    std::int32_t userId;
    std::uint8_t reserved[44];
};
static_assert(sizeof(MsgDialogParam) == 0x88, "SceMsgDialogParam layout");
static_assert(offsetof(MsgDialogParam, mode) == 0x38, "SceMsgDialogParam.mode offset");
static_assert(offsetof(MsgDialogParam, userMsgParam) == 0x40, "SceMsgDialogParam.userMsgParam offset");
static_assert(offsetof(MsgDialogParam, progressBarParam) == 0x48, "SceMsgDialogParam.progressBarParam offset");
static_assert(offsetof(MsgDialogParam, systemMsgParam) == 0x50, "SceMsgDialogParam.systemMsgParam offset");

// SceMsgDialogUserMessageParam, 0x30 bytes.
struct UserMessageParam {
    std::uint32_t buttonType;
    std::int32_t pad0;
    std::uint64_t msg;
    std::uint64_t buttonsParam;
    std::uint8_t reserved[24];
};
static_assert(sizeof(UserMessageParam) == 0x30, "SceMsgDialogUserMessageParam layout");
static_assert(offsetof(UserMessageParam, msg) == 8, "SceMsgDialogUserMessageParam.msg offset");

// SceMsgDialogResult, 44 bytes: the dialog mode, the SceCommonDialogResult and the pressed button.
struct MsgDialogResult {
    std::int32_t mode;
    std::int32_t result;
    std::int32_t buttonId;
    std::uint8_t reserved[32];
};
static_assert(sizeof(MsgDialogResult) == 44, "SceMsgDialogResult layout");
static_assert(offsetof(MsgDialogResult, buttonId) == 8, "SceMsgDialogResult.buttonId offset");

std::mutex g_lock;
bool g_initialized = false;
int g_status = kStatusNone;
std::int32_t g_mode = kModeUserMsg;
int g_pollsLeft = 0;
bool g_selfClosing = false;  // The title ends this dialog itself (sceMsgDialogClose).
MsgDialogResult g_result = {};
std::int32_t g_answerResult = kResultOk;  // Result the auto-answer reports.
std::int32_t g_answerButton = kButtonIdFirst;  // Button the auto-answer reports.

/**
 * Best-effort check that a guest range can be read or written.
 *
 * There is no guest-memory range API for PRXs yet (bean portps5-8l0d), so on Windows this asks the
 * VM manager whether the range is committed with the right protection. Elsewhere only null is
 * rejected. Replace with the guest-memory API when it lands.
 */
bool GuestRangeUsable(const void* pointer, std::size_t bytes, bool write) {
    if (pointer == nullptr) {
        return false;
    }
#ifdef _WIN32
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    auto cursor = begin;
    const auto end = begin + bytes;
    if (end < begin) {
        return false;
    }
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
            return false;
        }
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((info.Protect & PAGE_GUARD) != 0 || (info.Protect & (write ? kWritable : kReadable)) == 0) {
            return false;
        }
        cursor = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
#else
    (void)bytes;
    (void)write;
    return true;
#endif
}

/** Copies a bounded, printable prefix of a guest C string into `out` for the one-line log. */
void CopyLogText(std::uint64_t guestString, char (&out)[97]) {
    out[0] = '\0';
    const auto* text = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(guestString));
    std::size_t length = 0;
    // Probe byte by byte: the string length is unknown and the page after a short string may be unmapped.
    while (length < sizeof(out) - 1 && GuestRangeUsable(text + length, 1, false) && text[length] != '\0') {
        const char c = text[length];
        out[length] = (c >= 0x20 && c < 0x7F) ? c : '?';
        ++length;
    }
    out[length] = '\0';
}

/** Finishes the running dialog with `result` / `buttonId` and releases the common-dialog slot. */
void Finish(std::int32_t result, std::int32_t buttonId) {
    g_result = {};
    g_result.mode = g_mode;
    g_result.result = result;
    g_result.buttonId = buttonId;
    g_status = kStatusFinished;
    RegisterCommonDialogActive_nid_no_patch(false);
}

/**
 * Decodes the guest parameter block and decides how the dialog ends.
 *
 * Returns kOk and fills the mode, auto-answer and self-closing state, or kErrParamInvalid when a
 * structure the mode needs is unreadable. Called with g_lock held.
 */
int Inspect(const void* param, std::int32_t& answerResult, std::int32_t& answerButton) {
    if (!GuestRangeUsable(param, sizeof(MsgDialogParam), false)) {
        return kErrParamInvalid;
    }
    MsgDialogParam p;
    std::memcpy(&p, param, sizeof(p));
    if (p.mode < kModeUserMsg || p.mode > kModeSystemMsg) {
        return kErrParamInvalid;
    }
    g_mode = p.mode;
    g_selfClosing = false;
    answerResult = kResultOk;
    answerButton = kButtonIdFirst;

    if (p.mode == kModeProgressBar) {
        // A progress bar is ended by the title through sceMsgDialogClose, never by an answer.
        g_selfClosing = true;
        return kOk;
    }
    if (p.mode == kModeSystemMsg) {
        return kOk;  // A system message has a single OK button.
    }

    const auto* user = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(p.userMsgParam));
    if (!GuestRangeUsable(user, sizeof(UserMessageParam), false)) {
        return kErrParamInvalid;
    }
    UserMessageParam u;
    std::memcpy(&u, user, sizeof(u));
    switch (u.buttonType) {
        case kButtonNone:
        case kButtonWait:
        case kButtonWaitCancel:
            // No button the user could press: the title closes these once its work is done.
            g_selfClosing = true;
            break;
        case kButtonYesNoFocusNo:
            answerButton = kButtonIdSecond;  // The focused button is No; the safe auto-answer.
            break;
        case kButtonOkCancelFocusCancel:
            answerResult = kResultUserCanceled;
            answerButton = kButtonIdSecond;
            break;
        case kButtonTwoButtons:
            if (!GuestRangeUsable(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(u.buttonsParam)), 16, false)) {
                return kErrParamInvalid;
            }
            break;
        default:
            break;  // OK, YES/NO and OK/CANCEL: the first button is the affirmative one.
    }
    char text[97];
    CopyLogText(u.msg, text);
    APS5_LOG_OUT("[MSGDIALOG] open: buttons=%u text=\"%s\" -> %s", static_cast<unsigned>(u.buttonType), text,
                 g_selfClosing ? "waits for sceMsgDialogClose" : "auto answer");
    return kOk;
}

/** One status poll: advances an auto-answered dialog towards FINISHED. Called with g_lock held. */
int Poll() {
    if (g_status == kStatusRunning && !g_selfClosing && --g_pollsLeft <= 0) {
        Finish(g_answerResult, g_answerButton);
    }
    return g_status;
}

/** Shared precondition of the three progress-bar calls: running, progress-bar mode, DEFAULT target. */
int CheckProgressBar(int target) {
    if (g_status != kStatusRunning) {
        return kErrNotRunning;
    }
    if (g_mode != kModeProgressBar) {
        return kErrNotSupported;
    }
    if (target != 0) {
        return kErrParamInvalid;
    }
    return kOk;
}

}  // namespace

extern "C" {

/**
 * Initializes the message dialog library.
 * Returns kOk, or SCE_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED when already initialized.
 */
int APS5_VABI sceMsgDialogInitialize(void) noexcept {
    std::lock_guard lock(g_lock);
    if (g_initialized) {
        return kErrAlreadyInitialized;
    }
    g_initialized = true;
    g_status = kStatusInitialized;
    return kOk;
}

/**
 * Shuts the library down, closing a running dialog first.
 * Returns kOk, or SCE_COMMON_DIALOG_ERROR_NOT_INITIALIZED when it was not initialized.
 */
int APS5_VABI sceMsgDialogTerminate(void) noexcept {
    std::lock_guard lock(g_lock);
    if (!g_initialized) {
        return kErrNotInitialized;
    }
    if (g_status == kStatusRunning) {
        Finish(kResultOk, kButtonIdInvalid);
    }
    g_initialized = false;
    g_status = kStatusNone;
    return kOk;
}

/**
 * Opens a dialog from a guest SceMsgDialogParam.
 * Returns kOk; NOT_INITIALIZED before sceMsgDialogInitialize; INVALID_STATE while a dialog runs;
 * ARG_NULL for a null parameter; PARAM_INVALID for an unreadable block or an unknown mode.
 */
int APS5_VABI sceMsgDialogOpen(const void* param) noexcept {
    std::lock_guard lock(g_lock);
    if (!g_initialized) {
        return kErrNotInitialized;
    }
    if (g_status == kStatusRunning) {
        return kErrInvalidState;
    }
    if (param == nullptr) {
        return kErrArgNull;
    }
    std::int32_t answerResult = kResultOk;
    std::int32_t answerButton = kButtonIdFirst;
    if (const int error = Inspect(param, answerResult, answerButton)) {
        return error;
    }
    g_answerResult = answerResult;
    g_answerButton = answerButton;
    g_pollsLeft = kPollsBeforeFinish;
    g_result = {};
    g_status = kStatusRunning;
    RegisterCommonDialogActive_nid_no_patch(true);
    return kOk;
}

/**
 * Returns the current status without requiring the title to pump sceMsgDialogUpdateStatus.
 * A GetStatus-only poller counts as a poll, so it cannot wait on a dialog nobody can answer.
 */
int APS5_VABI sceMsgDialogGetStatus(void) noexcept {
    std::lock_guard lock(g_lock);
    return Poll();
}

/** Per-frame status pump; returns the (possibly advanced) status. */
int APS5_VABI sceMsgDialogUpdateStatus(void) noexcept {
    std::lock_guard lock(g_lock);
    return Poll();
}

/**
 * Copies the 44-byte SceMsgDialogResult of the finished dialog.
 * Returns kOk; NOT_INITIALIZED; ARG_NULL; NOT_FINISHED while the dialog runs; PARAM_INVALID when
 * the guest buffer cannot be written.
 */
int APS5_VABI sceMsgDialogGetResult(void* result) noexcept {
    std::lock_guard lock(g_lock);
    if (!g_initialized) {
        return kErrNotInitialized;
    }
    if (result == nullptr) {
        return kErrArgNull;
    }
    if (g_status != kStatusFinished) {
        return kErrNotFinished;
    }
    if (!GuestRangeUsable(result, sizeof(MsgDialogResult), true)) {
        return kErrParamInvalid;
    }
    std::memcpy(result, &g_result, sizeof(g_result));
    return kOk;
}

/**
 * Ends a running dialog on behalf of the title; the result carries no button.
 * Returns kOk, NOT_INITIALIZED, or NOT_RUNNING when no dialog is running.
 */
int APS5_VABI sceMsgDialogClose(void) noexcept {
    std::lock_guard lock(g_lock);
    if (!g_initialized) {
        return kErrNotInitialized;
    }
    if (g_status != kStatusRunning) {
        return kErrNotRunning;
    }
    Finish(kResultOk, kButtonIdInvalid);
    return kOk;
}

/** Adds `delta` percent to the progress bar. Returns kOk, NOT_RUNNING, NOT_SUPPORTED or PARAM_INVALID. */
int APS5_VABI sceMsgDialogProgressBarInc(int target, std::uint32_t delta) noexcept {
    (void)delta;  // No UI, so there is no bar to draw.
    std::lock_guard lock(g_lock);
    return CheckProgressBar(target);
}

/** Replaces the progress-bar message. Returns kOk, NOT_RUNNING, NOT_SUPPORTED or PARAM_INVALID. */
int APS5_VABI sceMsgDialogProgressBarSetMsg(int target, const char* msg) noexcept {
    (void)msg;
    std::lock_guard lock(g_lock);
    return CheckProgressBar(target);
}

/** Sets the progress-bar value. Returns kOk, NOT_RUNNING, NOT_SUPPORTED or PARAM_INVALID. */
int APS5_VABI sceMsgDialogProgressBarSetValue(int target, std::uint32_t rate) noexcept {
    (void)rate;
    std::lock_guard lock(g_lock);
    return CheckProgressBar(target);
}

}  // extern "C"
