#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>

#include "SceTypes.hpp"
#include "prx//libc/include/General.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadState.hpp"

extern "C" {

int APS5_VABI scePadClose_nid_postfix(int handle) noexcept {
 if (handle != PAD_HANDLE) {
  return PAD_ERROR_INVALID_HANDLE;
 }
 return PAD_OK;
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
 if (handle != PAD_HANDLE) {
  return PAD_ERROR_INVALID_HANDLE;
 }
 if (info == nullptr) {
  return PAD_ERROR_INVALID_ARG;
 }
 std::memset(info, 0, sizeof(*info));
 info->touchPadInfo.pixelDensity = 44.86f;
 info->touchPadInfo.resolution.x = 1920;
 info->touchPadInfo.resolution.y = 943;
 info->stickInfo.deadZoneLeft = 2;
 info->stickInfo.deadZoneRight = 2;
 info->connectionType = PAD_CONNECTION_TYPE_LOCAL;
 info->connectedCount = 1;
 info->connected = true;
 info->deviceClass = PAD_DEVICE_CLASS_STANDARD;
 return PAD_OK;
}

int APS5_VABI scePadGetHandle(int user_id, int type, int index) noexcept {
 (void)user_id;
 (void)type;
 (void)index;
 Unsupported(__func__);
}

int APS5_VABI scePadGetTriggerEffectState(int handle, PadTriggerEffectStateInformation* info) noexcept {
 (void)handle;
 (void)info;
 Unsupported(__func__);
}

int APS5_VABI scePadInit_nid_postfix(void) noexcept {
 try {
  Pad::Initialize();
 } catch (const std::exception&) {
  // Why abort: input failure means the VideoOut driver failed; no offline
  // default exists, so the gap stays loud instead of returning wrong input.
  Unsupported("scePadInit: input failure");
 }
 return PAD_OK;
}

int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept {
 (void)param;
 if (index != 0) {
  return PAD_ERROR_INVALID_ARG;
 }
 const bool personalPort = (type == PAD_PORT_TYPE_STANDARD || type == PAD_PORT_TYPE_SPECIAL);
 const bool systemRemote = (userId == PAD_USER_ID_SYSTEM && type == PAD_PORT_TYPE_REMOTE);
 if (!personalPort && !systemRemote) {
  return PAD_ERROR_INVALID_ARG;
 }
 return PAD_HANDLE;
}

int APS5_VABI scePadReadState(int handle, PadData* data) noexcept;

int APS5_VABI scePadRead_nid_postfix(int handle, PadData* data, int num) noexcept {
 // Why one state per call: the title drains queued states; PR #5 reports one
 // current state per call so boot never blocks waiting for input.
 if (data == nullptr || num <= 0) {
  return PAD_ERROR_INVALID_ARG;
 }
 const int result = scePadReadState(handle, data);
 if (result != 0) {
  return result;
 }
 return 1;
}

int APS5_VABI scePadReadState(int handle, PadData* data) noexcept {
 if (handle != PAD_HANDLE) {
  return PAD_ERROR_INVALID_HANDLE;
 }
 if (data == nullptr) {
  return PAD_ERROR_INVALID_ARG;
 }
 try {
  *data = Pad::ReadState();
 } catch (const std::exception&) {
  Unsupported("scePadReadState: input failure");
 }
 return PAD_OK;
}

int APS5_VABI scePadResetLightBar(int handle) noexcept {
 (void)handle;
 Unsupported(__func__);
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
 (void)handle;
 (void)param;
 Unsupported(__func__);
}

int APS5_VABI scePadSetMotionSensorState(int handle, bool enable) noexcept {
 (void)handle;
 (void)enable;
 return PAD_OK;
}

int APS5_VABI scePadSetTiltCorrectionState(int handle, bool enabled) noexcept {
 (void)handle;
 (void)enabled;
 Unsupported(__func__);
}

int APS5_VABI scePadSetTriggerEffect(int handle, const void* param) noexcept {
 (void)handle;
 (void)param;
 Unsupported(__func__);
}

int APS5_VABI scePadSetVibration(int handle, const PadVibrationParam* param) noexcept {
 (void)handle;
 (void)param;
 Unsupported(__func__);
}

int APS5_VABI scePadSetVibrationMode(int handle, int mode) noexcept {
 (void)handle;
 (void)mode;
 Unsupported(__func__);
}

int APS5_VABI scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse(bool enabled) noexcept {
 (void)enabled;
 Unsupported(__func__);
}

}
