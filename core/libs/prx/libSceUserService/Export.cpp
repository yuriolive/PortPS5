#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceUserService/UserService.hpp"

// Why offline single user: 1.0 has one user (docs/spec/save-data.md), id
// 0x10000000 named "Player". Accessibility and presets report fresh-console
// defaults (all zero) so titles fall back to their own defaults.
static constexpr char kOfflineUserName[] = "Player";

extern "C" {

int APS5_VABI sceUserServiceGetAccessibilityChatTranscription(int user_id, int32_t* chat_transcription) noexcept {
 (void)user_id;
 if (chat_transcription == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *chat_transcription = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetAccessibilityPressAndHoldDelay(int user_id, int32_t* press_and_hold_delay) noexcept {
 (void)user_id;
 if (press_and_hold_delay == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *press_and_hold_delay = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetAccessibilityTriggerEffect(int user_id, int32_t* trigger_effect) noexcept {
 (void)user_id;
 if (trigger_effect == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *trigger_effect = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetAccessibilityVibration(int user_id, int32_t* vibration) noexcept {
 (void)user_id;
 if (vibration == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *vibration = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetAccessibilityZoomEnabled(int user_id, int32_t* zoom_enabled) noexcept {
 (void)user_id;
 if (zoom_enabled == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *zoom_enabled = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetAgeLevel(int user_id, uint32_t* age_level) noexcept {
 (void)user_id;
 if (age_level == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *age_level = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetEvent(SceUserServiceEvent* event) noexcept {
 if (event == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 // Why login once: the initial user logs in at boot; afterwards no user
 // events occur offline, so titles polling see NO_EVENT instead of blocking.
 static std::atomic<bool> loginReported{false};
 if (loginReported.exchange(true, std::memory_order_relaxed)) {
  return USER_SERVICE_ERROR_NO_EVENT;
 }
 constexpr std::uint32_t EventTypeLogin = 0;
 event->event_type = EventTypeLogin;
 event->user_id = USER_SERVICE_INITIAL_USER_ID;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetGamePresets(int user_id, UserServiceGamePresets* presets) noexcept {
 // Why zeros: a fresh console has no profile presets; every field 0 means
 // "not specified" and the title uses its own defaults.
 if (presets == nullptr || user_id != USER_SERVICE_INITIAL_USER_ID) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 presets->difficulty = 0;
 presets->priority = 0;
 presets->invert_vertical_view_for_1st_person_view = 0;
 presets->invert_horizontal_view_for_1st_person_view = 0;
 presets->invert_vertical_view_for_3rd_person_view = 0;
 presets->invert_horizontal_view_for_3rd_person_view = 0;
 presets->display_sub_titles = 0;
 presets->audio_language = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetInitialUser(int* user_id) noexcept {
 if (user_id == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *user_id = USER_SERVICE_INITIAL_USER_ID;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetLoginUserIdList(UserServiceLoginUserIdList* user_id_list) noexcept {
 if (user_id_list == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 user_id_list->user_id[0] = USER_SERVICE_INITIAL_USER_ID;
 user_id_list->user_id[1] = USER_SERVICE_USER_ID_INVALID;
 user_id_list->user_id[2] = USER_SERVICE_USER_ID_INVALID;
 user_id_list->user_id[3] = USER_SERVICE_USER_ID_INVALID;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetUserName(int user_id, char* name, size_t size) noexcept {
 if (name == nullptr || size == 0) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 if (user_id != USER_SERVICE_INITIAL_USER_ID) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 std::strncpy(name, kOfflineUserName, size - 1);
 name[size - 1] = '\0';
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetUserNumber(int user_id, int32_t* number) noexcept {
 if (number == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 if (user_id != USER_SERVICE_INITIAL_USER_ID) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 // Why 0: single offline user is user number 0.
 *number = 0;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceInitialize(const void* params) noexcept {
 (void)params;
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceInitialize2(void) noexcept {
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceTerminate(void) noexcept {
 return USER_SERVICE_OK;
}

int APS5_VABI sceUserServiceGetPlatformPrivacyWs1(int32_t user_id, int32_t* value) noexcept {
 (void)user_id;
 // Why 0, OK: no PSN account exists offline, so the platform privacy setting
 // reports the feature as not permitted without blocking.
 if (value == nullptr) {
  return USER_SERVICE_ERROR_INVALID_ARGUMENT;
 }
 *value = 0;
 return USER_SERVICE_OK;
}

}
