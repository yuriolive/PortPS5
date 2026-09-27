#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "prx/libc/include/Shutdown.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceSystemService/SystemService.hpp"

extern "C" {

int APS5_VABI sceSystemServiceLoadExec(const char* path, const char* const* arguments) noexcept {
 if (path == nullptr || *path == '\0') {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 if (std::strcmp(path, "exit") != 0) {
  // Why abort: executable replacement is genuinely unsupported in 1.0; the
  // log names the path so the gap is visible instead of silently wrong.
  Unsupported("sceSystemServiceLoadExec: executable replacement");
 }
 (void)arguments;
 LibcRunShutdown_nid_postfix();
 std::exit(0);
}

int APS5_VABI sceSystemServiceDisableNoticeScreenSkipFlagAutoSet(void) noexcept {
 Unsupported(__func__);
}

int APS5_VABI sceSystemServiceGetDisplaySafeAreaInfo(SystemServiceDisplaySafeAreaInfo* info) noexcept {
 if (info == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 // Why ratio 1.0: offline reports a full-bleed safe area so titles lay out
 // full-screen instead of waiting on a system query.
 *info = SystemServiceDisplaySafeAreaInfo{};
 info->ratio = 1.0f;
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceGetHdrToneMapLuminance(SystemServiceHdrToneMapLuminance* luminance) noexcept {
 if (luminance == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 // Why zeros: offline has no HDR metadata; zeros keep titles on SDR.
 *luminance = SystemServiceHdrToneMapLuminance{};
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceGetNoticeScreenSkipFlag(bool* value) noexcept {
 if (value == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 *value = false;
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceGetStatus(SystemServiceStatus* status) noexcept {
 if (status == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 *status = SystemServiceStatus{};
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceHideSplashScreen(void) noexcept {
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceParamGetInt(int paramId, int* value) noexcept {
 if (value == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 switch (paramId) {
  case SYSTEM_SERVICE_PARAM_ID_LANG: *value = SYSTEM_SERVICE_PARAM_LANG_ENGLISH_US; break;
  case SYSTEM_SERVICE_PARAM_ID_DATE_FORMAT: *value = SYSTEM_SERVICE_PARAM_DATE_FORMAT_DDMMYYYY; break;
  case SYSTEM_SERVICE_PARAM_ID_TIME_FORMAT: *value = SYSTEM_SERVICE_PARAM_TIME_FORMAT_24HOUR; break;
  case SYSTEM_SERVICE_PARAM_ID_TIME_ZONE: *value = 0; break;
  case SYSTEM_SERVICE_PARAM_ID_SUMMERTIME: *value = 0; break;
  case SYSTEM_SERVICE_PARAM_ID_GAME_PARENTAL_LEVEL: *value = SYSTEM_SERVICE_PARAM_GAME_PARENTAL_OFF; break;
  case SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN: *value = SYSTEM_SERVICE_PARAM_ENTER_BUTTON_CROSS; break;
  default: *value = 0; break;
 }
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServiceParamGetString(int param_id, char* buf, size_t buf_size) noexcept {
 (void)param_id;
 // Why empty, OK: M1 has one user "Player"; string params return empty until
 // a gate title needs a specific key, keeping boot non-blocking.
 if (buf == nullptr || buf_size == 0) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 buf[0] = '\0';
 return SYSTEM_SERVICE_OK;
}

int APS5_VABI sceSystemServicePowerTick(void) noexcept {
 Unsupported(__func__);
}

int APS5_VABI sceSystemServiceReceiveEvent(SystemServiceEvent* event) noexcept {
 if (event == nullptr) {
  return SYSTEM_SERVICE_ERROR_PARAMETER;
 }
 event->event_type = -1;
 std::memset(event->data, 0, sizeof(event->data));
 return SYSTEM_SERVICE_ERROR_NO_EVENT;
}

int APS5_VABI sceSystemServiceReportAbnormalTermination(const void* info) noexcept {
 (void)info;
 Unsupported(__func__);
}

int APS5_VABI sceSystemServiceSetNoticeScreenSkipFlag(void) noexcept {
 Unsupported(__func__);
}

}
