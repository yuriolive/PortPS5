#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why offline signed-out: PSN has no service behind it, so the user is
// reported as signed out and online queries fail with the signed-out error.
// Values mirror pr5-upstream for boot compatibility; callbacks are stored and
// never invoked because no state change ever happens offline.
static constexpr int SCE_NP_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);
static constexpr int SCE_NP_ERROR_SIGNED_OUT = static_cast<int>(0x80550006);
static constexpr std::uint32_t NP_STATE_SIGNED_OUT = 1;
static constexpr std::uint32_t NP_REACHABILITY_UNREACHABLE = 0;
static constexpr int NP_POLL_ASYNC_FINISHED = 0;

static std::atomic<int> g_nextRequest{1};
// Why stored, never called: offline state never changes, so titles that
// register for presence/reachability/premium/state events must not block
// waiting for a callback that will never arrive.
static void* g_presenceCb = nullptr;
static void* g_presenceArg = nullptr;
static void* g_reachCb = nullptr;
static void* g_reachArg = nullptr;
static void* g_plusCb = nullptr;
static void* g_plusArg = nullptr;
static void* g_premiumCb = nullptr;
static void* g_premiumArg = nullptr;
static void* g_stateCb = nullptr;
static void* g_stateArg = nullptr;
static void* g_stateACb = nullptr;
static void* g_stateAArg = nullptr;

extern "C" {

int APS5_VABI sceNpAbortRequest(int req_id) noexcept {
 (void)req_id;
 // Why OK: aborting an offline request that already finished is a no-op.
 return 0;
}

int APS5_VABI sceNpCheckCallback(void) noexcept {
 // Why OK with no work: stored offline callbacks never fire, so there is
 // nothing to dispatch and the title must not block here.
 return 0;
}

int APS5_VABI sceNpCheckNpAvailability(int req_id, const char* user, void* result) noexcept {
 (void)req_id;
 (void)user;
 (void)result;
 // Why signed-out: offline has no PSN availability to report.
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpCheckNpReachability(int req_id, int user_id) noexcept {
 (void)req_id;
 (void)user_id;
 // Why new id: reachability is async on console; the id lets PollAsync finish
 // immediately with signed-out instead of blocking the title.
 return g_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpCheckPremium(int req_id, const NpCheckPremiumParameter* param, NpCheckPremiumResult* result) noexcept {
 (void)req_id;
 (void)param;
 (void)result;
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpCreateAsyncRequest(const NpCreateAsyncRequestParameter* param) noexcept {
 (void)param;
 return g_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpCreateRequest(void) noexcept {
 return g_nextRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpDeleteRequest(int req_id) noexcept {
 (void)req_id;
 return 0;
}

int APS5_VABI sceNpGetAccountAge(int req_id, int user_id, uint8_t* age) noexcept {
 (void)req_id;
 (void)user_id;
 if (age == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetAccountCountryA(int user_id, void* country_code) noexcept {
 (void)user_id;
 if (country_code == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetAccountIdA(int user_id, uint64_t* account_id) noexcept {
 (void)user_id;
 if (account_id == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetNpId(int user_id, NpId* np_id) noexcept {
 (void)user_id;
 if (np_id == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 // Why zeroed: offline has no NP ID to return; zero plus signed-out keeps
 // titles on their offline path instead of blocking on a missing id.
 std::memset(np_id, 0, sizeof(*np_id));
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetNpReachabilityState(int user_id, uint32_t* state) noexcept {
 (void)user_id;
 if (state == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 // Why unreachable, OK: the query itself succeeds offline; the result says
 // the network is unreachable so the title stays offline instead of waiting.
 *state = NP_REACHABILITY_UNREACHABLE;
 return 0;
}

int APS5_VABI sceNpGetOnlineId(int user_id, NpOnlineId* online_id) noexcept {
 (void)user_id;
 if (online_id == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpGetState(int user_id, uint32_t* state) noexcept {
 (void)user_id;
 if (state == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 *state = NP_STATE_SIGNED_OUT;
 return 0;
}

int APS5_VABI sceNpHasSignedUp(int user_id, bool* has_signed_up) noexcept {
 (void)user_id;
 if (has_signed_up == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 // Why false, OK: an offline console has no PSN signup to report.
 *has_signed_up = false;
 return 0;
}

int APS5_VABI sceNpPollAsync(int req_id, int* result) noexcept {
 (void)req_id;
 // Why finished with signed-out: offline async requests complete on the first
 // poll so titles never block waiting for a network round-trip.
 if (result != nullptr) {
  *result = SCE_NP_ERROR_SIGNED_OUT;
 }
 return NP_POLL_ASYNC_FINISHED;
}

void APS5_VABI sceNpRegisterGamePresenceCallback(void* callback, void* userdata) noexcept {
 g_presenceCb = callback;
 g_presenceArg = userdata;
}

int APS5_VABI sceNpRegisterNpReachabilityStateCallback(void* callback, void* userdata) noexcept {
 g_reachCb = callback;
 g_reachArg = userdata;
 return 0;
}

int APS5_VABI sceNpRegisterPlusEventCallback(void* callback, void* userdata) noexcept {
 g_plusCb = callback;
 g_plusArg = userdata;
 return 0;
}

int APS5_VABI sceNpRegisterPremiumEventCallback(void* callback, void* userdata) noexcept {
 g_premiumCb = callback;
 g_premiumArg = userdata;
 return 0;
}

int APS5_VABI sceNpRegisterStateCallback(void* callback, void* userdata) noexcept {
 g_stateCb = callback;
 g_stateArg = userdata;
 return 0;
}

int APS5_VABI sceNpSetContentRestriction(const NpContentRestriction* restriction) noexcept {
 (void)restriction;
 // Why OK: offline accepts and ignores the restriction; there is no store to enforce.
 return 0;
}

int APS5_VABI sceNpSetNpTitleId(const NpTitleId* title_id, const NpTitleSecret* title_secret) noexcept {
 (void)title_id;
 (void)title_secret;
 return 0;
}

int APS5_VABI sceNpUnregisterStateCallback(void) noexcept {
 g_stateCb = nullptr;
 g_stateArg = nullptr;
 return 0;
}

int APS5_VABI sceNpGetAccountLanguage2(int req_id, int user_id, void* language) noexcept {
 (void)req_id;
 (void)user_id;
 if (language == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpNotifyPremiumFeature(const void* param) noexcept {
 (void)param;
 return 0;
}

int APS5_VABI sceNpRegisterStateCallbackA(void* callback, void* userdata) noexcept {
 if (callback == nullptr) {
  return SCE_NP_ERROR_INVALID_ARGUMENT;
 }
 g_stateACb = callback;
 g_stateAArg = userdata;
 // Why 1: console returns a callback id; offline has one slot that never fires.
 return 1;
}

int APS5_VABI sceNpUnregisterStateCallbackA(int callback_id) noexcept {
 (void)callback_id;
 g_stateACb = nullptr;
 g_stateAArg = nullptr;
 return 0;
}

}
