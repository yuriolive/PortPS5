// libScePad guest exports: scePad* NIDs backed by Pad::PadManager.
// Subsystem: input (docs/spec/input.md). Every export uses APS5_VABI, returns SCE
// codes instead of throwing, and treats guest pointers as untrusted (null-checked
// here; slot and handle checks live in PadManager). Output calls (vibration,
// light bar, trigger effects) only record the request; the VideoOut window thread
// mirrors it onto the host controller.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadOutputMapping.hpp"
#include "prx/libScePad/include/PadState.hpp"
#include "src/PadInternal.hpp"

extern "C" {

/**
 * Closes a pad handle (slot + 1). Resets the slot's rumble, light bar and
 * trigger effect to defaults. Returns PAD_ERROR_INVALID_HANDLE for an out-of-
 * range or not-open handle.
 */
int APS5_VABI scePadClose_nid_postfix(int handle) noexcept {
    return Pad::PadManager::Get().Close(handle);
}

/**
 * Extended device-class information for a pad handle.
 *
 * Every host controller (XInput, DualSense, keyboard/mouse) is presented as a
 * standard digital pad, so only the class field is set and every class-specific
 * member (wheel, guitar, drum) stays zero. Returns PAD_ERROR_INVALID_ARG for a
 * null `info` and PAD_ERROR_INVALID_HANDLE for a closed or out-of-range handle.
 */
int APS5_VABI scePadDeviceClassGetExtendedInformation(int handle, PadDeviceClassExtendedInformation* info) noexcept {
    const int check = Pad::PadManager::Get().CheckOpenHandle(handle);
    if (check != PAD_OK) return check;
    if (info == nullptr) return PAD_ERROR_INVALID_ARG;
    std::memset(info, 0, sizeof(*info));
    info->deviceClass = PAD_DEVICE_CLASS_STANDARD;
    return PAD_OK;
}

/**
 * Parses class-specific data out of a PadData sample.
 *
 * A standard pad has no steering/guitar/drum payload, so the output is zeroed
 * with deviceClass = standard and dataValid mirroring the sample's connected
 * flag. Null pointers return PAD_ERROR_INVALID_ARG; a bad handle returns
 * PAD_ERROR_INVALID_HANDLE.
 */
int APS5_VABI scePadDeviceClassParseData(int handle, const PadData* data, PadDeviceClassData* class_data) noexcept {
    const int check = Pad::PadManager::Get().CheckOpenHandle(handle);
    if (check != PAD_OK) return check;
    if (data == nullptr || class_data == nullptr) return PAD_ERROR_INVALID_ARG;
    std::memset(class_data, 0, sizeof(*class_data));
    class_data->deviceClass = PAD_DEVICE_CLASS_STANDARD;
    class_data->dataValid = data->connected;
    return PAD_OK;
}

/**
 * Fills touchpad, stick dead zone, connection and connected-count information
 * for an open handle. Returns PAD_ERROR_INVALID_ARG for null `info` and
 * PAD_ERROR_INVALID_HANDLE for a bad handle.
 */
int APS5_VABI scePadGetControllerInformation(int handle, PadControllerInformation* info) noexcept {
    return Pad::PadManager::Get().GetControllerInformation(handle, info);
}

/**
 * Opens or looks up the pad for (user_id, type, index) and returns its handle,
 * or the same SCE error codes as scePadOpen (invalid argument, resource
 * allocation failed when the slot has no controller).
 */
int APS5_VABI scePadGetHandle(int user_id, int type, int index) noexcept {
    int handle = 0;
    const int res = Pad::PadManager::Get().Open(user_id, type, index, handle);
    if (res != PAD_OK) {
        return res;
    }
    return handle;
}

/**
 * Reports the adaptive trigger state of a pad.
 *
 * The host controller offers no trigger readback, so both triggers report the
 * neutral zero state. Null `info` returns PAD_ERROR_INVALID_ARG; a bad handle
 * returns PAD_ERROR_INVALID_HANDLE.
 */
int APS5_VABI scePadGetTriggerEffectState(int handle, PadTriggerEffectStateInformation* info) noexcept {
    const int check = Pad::PadManager::Get().CheckOpenHandle(handle);
    if (check != PAD_OK) return check;
    if (info == nullptr) return PAD_ERROR_INVALID_ARG;
    std::memset(info, 0, sizeof(*info));
    return PAD_OK;
}

/**
 * Initialises the pad library. Returns PAD_OK; a prior input failure from the
 * VideoOut driver takes the logging abort path because no offline default
 * exists.
 */
int APS5_VABI scePadInit_nid_postfix(void) noexcept {
    try {
        Pad::PadManager::Get().Initialize();
    } catch (const std::exception&) {
        // Why abort: input failure means the VideoOut driver failed; no offline
        // default exists, so the gap stays loud instead of returning wrong input.
        Unsupported("scePadInit: input failure");
    }
    return PAD_OK;
}

/**
 * Opens the pad for `index` (0..3) and returns handle index + 1. Slot 0 is
 * always backed by keyboard/mouse; higher slots only while a controller
 * occupies them. Returns PAD_ERROR_RESOURCE_ALLOCATION_FAILED for an unwired
 * slot and PAD_ERROR_INVALID_ARG for an unsupported user/port type. `param` is
 * unused.
 */
int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept {
    (void)param;
    int handle = 0;
    const int res = Pad::PadManager::Get().Open(userId, type, index, handle);
    if (res != PAD_OK) {
        return res;
    }
    return handle;
}

/**
 * Reads the current state of an open pad into `data` (one sample per call) and
 * returns 1. Returns PAD_ERROR_INVALID_ARG for null `data` or num <= 0,
 * PAD_ERROR_INVALID_HANDLE or PAD_ERROR_NOT_INITIALIZED otherwise.
 */
int APS5_VABI scePadRead_nid_postfix(int handle, PadData* data, int num) noexcept {
    return Pad::PadManager::Get().Read(handle, data, num);
}

/**
 * Reads the current state of an open pad into `data`. Returns PAD_OK, or
 * PAD_ERROR_INVALID_ARG, PAD_ERROR_INVALID_HANDLE or
 * PAD_ERROR_NOT_INITIALIZED.
 */
int APS5_VABI scePadReadState(int handle, PadData* data) noexcept {
    return Pad::PadManager::Get().ReadState(handle, data);
}

/**
 * Resets the light bar to the controller default colour. Returns
 * PAD_ERROR_INVALID_HANDLE for a bad handle.
 */
int APS5_VABI scePadResetLightBar(int handle) noexcept {
    return Pad::PadManager::Get().ResetLightBar(handle);
}

/**
 * Resets the orientation estimate of a pad to the identity (flat, face up).
 * Returns PAD_ERROR_INVALID_HANDLE for a closed or out-of-range handle.
 */
int APS5_VABI scePadResetOrientation(int handle) noexcept {
    return Pad::PadManager::Get().ResetOrientation(handle);
}

/**
 * Not implemented: no gate title needs it, so the call takes the logging abort
 * path instead of silently stubbing.
 */
int APS5_VABI scePadSetAngularVelocityDeadbandState(int handle, bool enable) noexcept {
    (void)handle;
    (void)enable;
    Unsupported(__func__);
}

/**
 * Sets the light bar colour (u8 r, g, b). Mirrored onto a DualSense by the
 * window thread; other controllers ignore it. Returns PAD_ERROR_INVALID_ARG
 * for null `param` and PAD_ERROR_INVALID_HANDLE for a bad handle.
 */
int APS5_VABI scePadSetLightBar(int handle, const PadLightBarParam* param) noexcept {
    return Pad::PadManager::Get().SetLightBar(handle, param);
}

/**
 * Enables or disables the motion sensors for the pad. Disabled sensors report
 * the rest pose and the host sensors are switched off. Returns
 * PAD_ERROR_INVALID_HANDLE for a bad handle.
 */
int APS5_VABI scePadSetMotionSensorState(int handle, bool enable) noexcept {
    return Pad::PadManager::Get().SetMotionSensorState(handle, enable);
}

/**
 * Not implemented: no gate title needs it, so the call takes the logging abort
 * path instead of silently stubbing.
 */
int APS5_VABI scePadSetTiltCorrectionState(int handle, bool enabled) noexcept {
    (void)handle;
    (void)enabled;
    Unsupported(__func__);
}

/**
 * Requests an adaptive trigger effect (post-1.0 feature, best effort).
 *
 * `param` is a guest ScePadTriggerEffectParam: u8 mask (bit 0 = L2, bit 1 = R2),
 * 7 padding bytes, then two 56-byte commands. Only the selected commands are
 * decoded (PadOutputMapping.hpp) and forwarded to the host DualSense by the
 * window thread. Returns PAD_ERROR_INVALID_HANDLE for a bad handle,
 * PAD_ERROR_INVALID_ARG for null `param`, mask bits above 1 or a mode above 6.
 * Invariant: the guest pointer is only null-checked, because libScePad has no
 * guest-memory range API yet; exactly kTriggerEffectParamSize bytes are read.
 */
int APS5_VABI scePadSetTriggerEffect(int handle, const void* param) noexcept {
    const int check = Pad::PadManager::Get().CheckOpenHandle(handle);
    if (check != PAD_OK) return check;
    Pad::TriggerEffectUpdate update;
    if (!Pad::ParseTriggerEffectParam(static_cast<const std::uint8_t*>(param), Pad::kTriggerEffectParamSize, update)) {
        return PAD_ERROR_INVALID_ARG;
    }
    return Pad::PadManager::Get().SetTriggerEffect(handle, update);
}

/**
 * Sets the large (low-frequency) and small (high-frequency) rumble motor
 * amplitudes, 0..255. Forwarded to SDL rumble by the window thread. Returns
 * PAD_ERROR_INVALID_ARG for null `param` and PAD_ERROR_INVALID_HANDLE for a
 * bad handle.
 */
int APS5_VABI scePadSetVibration(int handle, const PadVibrationParam* param) noexcept {
    return Pad::PadManager::Get().SetVibration(handle, param);
}

/**
 * Selects the vibration layout: 0 = desktop (USB), 1 = embedded controller.
 * Both drive the same host rumble motors. Any other mode returns
 * PAD_ERROR_INVALID_ARG; a bad handle returns PAD_ERROR_INVALID_HANDLE.
 */
int APS5_VABI scePadSetVibrationMode(int handle, int mode) noexcept {
    return Pad::PadManager::Get().SetVibrationMode(handle, mode);
}

/**
 * Accepts and ignores the embedded-microphone trigger weakening flag (no host
 * equivalent); always returns PAD_OK.
 */
int APS5_VABI scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse(bool enabled) noexcept {
    (void)enabled;
    return PAD_OK;
}

}
