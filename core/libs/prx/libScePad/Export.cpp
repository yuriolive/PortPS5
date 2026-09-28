#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadState.hpp"
#include "src/PadInternal.hpp"

extern "C" {

int APS5_VABI scePadClose_nid_postfix(int handle) noexcept {
    return Pad::PadManager::Get().Close(handle);
}

int APS5_VABI scePadDeviceClassGetExtendedInformation(int handle, PadDeviceClassExtendedInformation* info) noexcept {
    (void)handle;
    (void)info;
    // Why abort: device-class extended info needs the M2 InputHub (XInput /
    // DualSense); no offline default exists, so the gap stays loud.
    Unsupported(__func__);
}

int APS5_VABI scePadDeviceClassParseData(int handle, const PadData* data, PadDeviceClassData* class_data) noexcept {
    (void)handle;
    (void)data;
    (void)class_data;
    Unsupported(__func__);
}

int APS5_VABI scePadGetControllerInformation(int handle, PadControllerInformation* info) noexcept {
    return Pad::PadManager::Get().GetControllerInformation(handle, info);
}

int APS5_VABI scePadGetHandle(int user_id, int type, int index) noexcept {
    int handle = 0;
    const int res = Pad::PadManager::Get().Open(user_id, type, index, handle);
    if (res != PAD_OK) {
        return res;
    }
    return handle;
}

int APS5_VABI scePadGetTriggerEffectState(int handle, PadTriggerEffectStateInformation* info) noexcept {
    (void)handle;
    (void)info;
    Unsupported(__func__);
}

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

int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept {
    (void)param;
    int handle = 0;
    const int res = Pad::PadManager::Get().Open(userId, type, index, handle);
    if (res != PAD_OK) {
        return res;
    }
    return handle;
}

int APS5_VABI scePadRead_nid_postfix(int handle, PadData* data, int num) noexcept {
    return Pad::PadManager::Get().Read(handle, data, num);
}

int APS5_VABI scePadReadState(int handle, PadData* data) noexcept {
    return Pad::PadManager::Get().ReadState(handle, data);
}

int APS5_VABI scePadResetLightBar(int handle) noexcept {
    return Pad::PadManager::Get().ResetLightBar(handle);
}

int APS5_VABI scePadResetOrientation(int handle) noexcept {
    (void)handle;
    Unsupported(__func__);
}

int APS5_VABI scePadSetAngularVelocityDeadbandState(int handle, bool enable) noexcept {
    (void)handle;
    (void)enable;
    Unsupported(__func__);
}

int APS5_VABI scePadSetLightBar(int handle, const PadLightBarParam* param) noexcept {
    return Pad::PadManager::Get().SetLightBar(handle, param);
}

int APS5_VABI scePadSetMotionSensorState(int handle, bool enable) noexcept {
    return Pad::PadManager::Get().SetMotionSensorState(handle, enable);
}

int APS5_VABI scePadSetTiltCorrectionState(int handle, bool enabled) noexcept {
    (void)handle;
    (void)enabled;
    Unsupported(__func__);
}

int APS5_VABI scePadSetTriggerEffect(int handle, const void* param) noexcept {
    (void)handle;
    (void)param;
    // Why no-op: adaptive trigger effects are post-1.0; returning OK prevents games from wedging.
    return PAD_OK;
}

int APS5_VABI scePadSetVibration(int handle, const PadVibrationParam* param) noexcept {
    return Pad::PadManager::Get().SetVibration(handle, param);
}

int APS5_VABI scePadSetVibrationMode(int handle, int mode) noexcept {
    (void)handle;
    (void)mode;
    return PAD_OK;
}

int APS5_VABI scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse(bool enabled) noexcept {
    (void)enabled;
    return PAD_OK;
}

}
