// Offline M1 stubs: signed-out, non-blocking, never throwing (docs/spec/save-data.md,
// docs/spec/input.md M1 rows). Follows Config.cpp pattern: Require-abort in
// main(), one ctest, no game data, no GPU.

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceUserService/UserService.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libSceNpTrophy2/include/NpTrophy2.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libSceCommonDialog/CommonDialog.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
// NpManager (offline signed-out).
int APS5_VABI sceNpAbortRequest(int) noexcept;
// Export under test: sceNpCheckCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCheckCallback(void) noexcept;
// Export under test: sceNpCheckNpAvailability (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCheckNpAvailability(int, const char*, void*) noexcept;
// Export under test: sceNpCheckNpReachability (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCheckNpReachability(int, int) noexcept;
// Export under test: sceNpCheckPremium (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCheckPremium(int, const NpCheckPremiumParameter*, NpCheckPremiumResult*) noexcept;
// Export under test: sceNpCreateAsyncRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCreateAsyncRequest(const NpCreateAsyncRequestParameter*) noexcept;
// Export under test: sceNpCreateRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpCreateRequest(void) noexcept;
// Export under test: sceNpDeleteRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpDeleteRequest(int) noexcept;
// Export under test: sceNpGetAccountAge (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetAccountAge(int, int, uint8_t*) noexcept;
// Export under test: sceNpGetAccountCountryA (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetAccountCountryA(int, void*) noexcept;
// Export under test: sceNpGetAccountIdA (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetAccountIdA(int, uint64_t*) noexcept;
// Export under test: sceNpGetNpId (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetNpId(int, NpId*) noexcept;
// Export under test: sceNpGetNpReachabilityState (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetNpReachabilityState(int, uint32_t*) noexcept;
// Export under test: sceNpGetOnlineId (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetOnlineId(int, NpOnlineId*) noexcept;
// Export under test: sceNpGetState (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetState(int, uint32_t*) noexcept;
// Export under test: sceNpHasSignedUp (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpHasSignedUp(int, bool*) noexcept;
// Export under test: sceNpPollAsync (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpPollAsync(int, int*) noexcept;
// Export under test: sceNpRegisterGamePresenceCallback (linked from its PRX; see the file header for expected codes).
void APS5_VABI sceNpRegisterGamePresenceCallback(void*, void*) noexcept;
// Export under test: sceNpRegisterNpReachabilityStateCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpRegisterNpReachabilityStateCallback(void*, void*) noexcept;
// Export under test: sceNpRegisterPlusEventCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpRegisterPlusEventCallback(void*, void*) noexcept;
// Export under test: sceNpRegisterPremiumEventCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpRegisterPremiumEventCallback(void*, void*) noexcept;
// Export under test: sceNpRegisterStateCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpRegisterStateCallback(void*, void*) noexcept;
// Export under test: sceNpSetContentRestriction (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpSetContentRestriction(const NpContentRestriction*) noexcept;
// Export under test: sceNpSetNpTitleId (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpSetNpTitleId(const NpTitleId*, const NpTitleSecret*) noexcept;
// Export under test: sceNpUnregisterStateCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUnregisterStateCallback(void) noexcept;
// Export under test: sceNpGetAccountLanguage2 (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGetAccountLanguage2(int, int, void*) noexcept;
// Export under test: sceNpNotifyPremiumFeature (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpNotifyPremiumFeature(const void*) noexcept;
// Export under test: sceNpRegisterStateCallbackA (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpRegisterStateCallbackA(void*, void*) noexcept;
// Export under test: sceNpUnregisterStateCallbackA (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUnregisterStateCallbackA(int) noexcept;
// NpAuth.
int APS5_VABI sceNpAuthAbortRequest(int) noexcept;
// Export under test: sceNpAuthCreateAsyncRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthCreateAsyncRequest(const void*) noexcept;
// Export under test: sceNpAuthCreateRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthCreateRequest(void) noexcept;
// Export under test: sceNpAuthDeleteRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthDeleteRequest(int) noexcept;
// Export under test: sceNpAuthGetAuthorizationCodeV3 (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthGetAuthorizationCodeV3(int, const void*, void*, int*) noexcept;
// Export under test: sceNpAuthGetIdTokenV3 (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthGetIdTokenV3(int, const void*, void*) noexcept;
// Export under test: sceNpAuthPollAsync (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthPollAsync(int, int*) noexcept;
// Export under test: sceNpAuthWaitAsync (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpAuthWaitAsync(int, int*) noexcept;
// WebApi2.
int APS5_VABI sceNpWebApi2AbortRequest(int64_t) noexcept;
// Export under test: sceNpWebApi2CreateRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2CreateRequest(int, const char*, const char*, const char*, const void*, int64_t*) noexcept;
// Export under test: sceNpWebApi2CreateUserContext (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2CreateUserContext(int, int) noexcept;
// Export under test: sceNpWebApi2DeleteRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2DeleteRequest(int64_t) noexcept;
// Export under test: sceNpWebApi2GetHttpResponseHeaderValue (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2GetHttpResponseHeaderValue(int64_t, const char*, char*, size_t) noexcept;
// Export under test: sceNpWebApi2SendRequest (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2SendRequest(int64_t, const void*, size_t, NpWebApi2ResponseInformationOption*) noexcept;
// Export under test: sceNpWebApi2Initialize (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2Initialize(int, size_t) noexcept;
// Export under test: sceNpWebApi2Terminate (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpWebApi2Terminate(int) noexcept;
// Commerce / Entitlement / Session / GameIntent.
int APS5_VABI sceNpCommerceDialogUpdateStatus(void) noexcept;
// Export under test: sceNpEntitlementAccessGetAddcontEntitlementInfo (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpEntitlementAccessGetAddcontEntitlementInfo(uint32_t, const NpUnifiedEntitlementLabel*, NpEntitlementAccessAddcontEntitlementInfo*) noexcept;
// Export under test: sceNpEntitlementAccessGetAddcontEntitlementInfoList (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpEntitlementAccessGetAddcontEntitlementInfoList(uint32_t, NpEntitlementAccessAddcontEntitlementInfo*, uint32_t, uint32_t*) noexcept;
// Export under test: sceNpEntitlementAccessGetSkuFlag (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpEntitlementAccessGetSkuFlag(uint32_t*) noexcept;
// Export under test: sceNpEntitlementAccessInitialize (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpEntitlementAccessInitialize(const NpEntitlementAccessInitParam*, NpEntitlementAccessBootParam*) noexcept;
// Export under test: sceNpSessionSignalingInitialize (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpSessionSignalingInitialize(void*) noexcept;
// Export under test: sceNpGameIntentGetPropertyValueString (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGameIntentGetPropertyValueString(const NpGameIntentData*, const char*, char*, size_t) noexcept;
// Export under test: sceNpGameIntentInitialize (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGameIntentInitialize(const void*) noexcept;
// Export under test: sceNpGameIntentReceiveIntent (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGameIntentReceiveIntent(NpGameIntentInfo*) noexcept;
// Export under test: sceNpGameIntentTerminate (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpGameIntentTerminate(void) noexcept;
// Trophy2.
int APS5_VABI sceNpTrophy2CreateContext(int*, int, uint32_t, uint64_t) noexcept;
// Export under test: sceNpTrophy2CreateHandle (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2CreateHandle(int*) noexcept;
// Export under test: sceNpTrophy2GetGameInfo (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetGameInfo(int, int, NpTrophy2GameDetails*, NpTrophy2GameData*) noexcept;
// Export under test: sceNpTrophy2GetGameIcon (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetGameIcon(int, int, void*, size_t*) noexcept;
// Export under test: sceNpTrophy2GetGroupInfo (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetGroupInfo(int, int, int, NpTrophy2GroupDetails*, NpTrophy2GroupData*) noexcept;
// Export under test: sceNpTrophy2GetGroupIcon (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetGroupIcon(int, int, int, void*, size_t*) noexcept;
// Export under test: sceNpTrophy2GetTrophyInfo (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetTrophyInfo(int, int, int, NpTrophy2Details*, NpTrophy2Data*) noexcept;
// Export under test: sceNpTrophy2GetTrophyIcon (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2GetTrophyIcon(int, int, int, void*, size_t*) noexcept;
// Export under test: sceNpTrophy2RegisterUnlockCallback (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpTrophy2RegisterUnlockCallback(void*, void*) noexcept;
// UDS.
int APS5_VABI sceNpUniversalDataSystemInitialize(const NpUniversalDataSystemInitParam*) noexcept;
// Export under test: sceNpUniversalDataSystemCreateContext (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUniversalDataSystemCreateContext(int*, int, uint32_t, uint64_t) noexcept;
// Export under test: sceNpUniversalDataSystemCreateHandle (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUniversalDataSystemCreateHandle(int*) noexcept;
// Export under test: sceNpUniversalDataSystemPostEvent (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUniversalDataSystemPostEvent(int, int, const void*, uint64_t) noexcept;
// Export under test: sceNpUniversalDataSystemTerminate (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceNpUniversalDataSystemTerminate(void) noexcept;
// UserService.
int APS5_VABI sceUserServiceGetAccessibilityVibration(int, int32_t*) noexcept;
// Export under test: sceUserServiceGetAgeLevel (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetAgeLevel(int, uint32_t*) noexcept;
// Export under test: sceUserServiceGetEvent (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetEvent(SceUserServiceEvent*) noexcept;
// Export under test: sceUserServiceGetGamePresets (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetGamePresets(int, UserServiceGamePresets*) noexcept;
// Export under test: sceUserServiceGetInitialUser (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetInitialUser(int*) noexcept;
// Export under test: sceUserServiceGetLoginUserIdList (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetLoginUserIdList(UserServiceLoginUserIdList*) noexcept;
// Export under test: sceUserServiceGetUserName (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetUserName(int, char*, size_t) noexcept;
// Export under test: sceUserServiceGetUserNumber (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetUserNumber(int, int32_t*) noexcept;
// Export under test: sceUserServiceGetPlatformPrivacyWs1 (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceGetPlatformPrivacyWs1(int32_t, int32_t*) noexcept;
// Export under test: sceUserServiceInitialize (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceUserServiceInitialize(const void*) noexcept;
// SystemService.
int APS5_VABI sceSystemServiceGetDisplaySafeAreaInfo(SystemServiceDisplaySafeAreaInfo*) noexcept;
// Export under test: sceSystemServiceGetNoticeScreenSkipFlag (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceGetNoticeScreenSkipFlag(bool*) noexcept;
// Export under test: sceSystemServiceGetStatus (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceGetStatus(SystemServiceStatus*) noexcept;
// Export under test: sceSystemServiceHideSplashScreen (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceHideSplashScreen(void) noexcept;
// Export under test: sceSystemServiceParamGetInt (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceParamGetInt(int, int*) noexcept;
// Export under test: sceSystemServiceParamGetString (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceParamGetString(int, char*, size_t) noexcept;
// Export under test: sceSystemServiceReceiveEvent (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSystemServiceReceiveEvent(SystemServiceEvent*) noexcept;
// SaveDataDialog.native (same names as plain; link picks one — test native path
// via direct statics would collide, so exercise native only when its lib is
// linked; plain lib provides identical names and would duplicate. To avoid
// duplicate symbols, this test links native only; plain behavior mirrors it
// except completing in Open, verified by review plus native sequence below).
int APS5_VABI sceSaveDataDialogInitialize(void) noexcept;
// Export under test: sceSaveDataDialogOpen (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogOpen(const void*) noexcept;
// Export under test: sceSaveDataDialogGetStatus (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogGetStatus(void) noexcept;
// Export under test: sceSaveDataDialogUpdateStatus (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogUpdateStatus(void) noexcept;
// Export under test: sceSaveDataDialogGetResult (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogGetResult(void*) noexcept;
// Export under test: sceSaveDataDialogClose (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogClose(const void*) noexcept;
// Export under test: sceSaveDataDialogTerminate (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogTerminate(void) noexcept;
// Export under test: sceSaveDataDialogIsReadyToDisplay (linked from its PRX; see the file header for expected codes).
int APS5_VABI sceSaveDataDialogIsReadyToDisplay(void) noexcept;
// CommonDialog.
int APS5_VABI sceCommonDialogInitialize(void) noexcept;
// Export under test: sceCommonDialogIsUsed (linked from its PRX; see the file header for expected codes).
bool APS5_VABI sceCommonDialogIsUsed(void) noexcept;
// Pad.
int APS5_VABI scePadInit_nid_postfix(void) noexcept;
// Export under test: scePadOpen_nid_postfix (linked from its PRX; see the file header for expected codes).
int APS5_VABI scePadOpen_nid_postfix(int, int, int, const void*) noexcept;
// Export under test: scePadReadState (linked from its PRX; see the file header for expected codes).
int APS5_VABI scePadReadState(int, PadData*) noexcept;
// Export under test: scePadRead_nid_postfix (linked from its PRX; see the file header for expected codes).
int APS5_VABI scePadRead_nid_postfix(int, PadData*, int) noexcept;
// Export under test: scePadGetControllerInformation (linked from its PRX; see the file header for expected codes).
int APS5_VABI scePadGetControllerInformation(int, PadControllerInformation*) noexcept;
// Export under test: scePadSetMotionSensorState (linked from its PRX; see the file header for expected codes).
int APS5_VABI scePadSetMotionSensorState(int, bool) noexcept;
}

namespace {

constexpr int NP_INVALID_ARG = static_cast<int>(0x80550003);
constexpr int NP_SIGNED_OUT = static_cast<int>(0x80550006);
constexpr int WEBAPI_INVALID_ARG = static_cast<int>(0x80553402);
constexpr int WEBAPI_UNAVAILABLE = static_cast<int>(0x80553406);

void Require(bool value, int line) {
 if (!value) {
  std::printf("OfflineStubs FAIL line %d\n", line);
  std::fflush(stdout);
  std::abort();
 }
 (void)line;
}
#define REQUIRE(cond) Require((cond), __LINE__)

void TestNpManager() {
 uint32_t state = 0;
 REQUIRE(sceNpGetState(0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetState(0, &state) == 0);
 REQUIRE(state == 1);
 int result = 0;
 REQUIRE(sceNpPollAsync(1, &result) == 0);
 REQUIRE(result == NP_SIGNED_OUT);
 REQUIRE(sceNpPollAsync(1, nullptr) == 0);
 REQUIRE(sceNpCheckPremium(1, nullptr, nullptr) == NP_SIGNED_OUT);
 REQUIRE(sceNpGetAccountIdA(0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetAccountIdA(0, reinterpret_cast<uint64_t*>(&result)) == NP_SIGNED_OUT);
 REQUIRE(sceNpGetOnlineId(0, nullptr) == NP_INVALID_ARG);
 NpId id{};
 REQUIRE(sceNpGetNpId(0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetNpId(0, &id) == NP_SIGNED_OUT);
 uint32_t reach = 99;
 REQUIRE(sceNpGetNpReachabilityState(0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetNpReachabilityState(0, &reach) == 0);
 REQUIRE(reach == 0);
 bool signedUp = true;
 REQUIRE(sceNpHasSignedUp(0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpHasSignedUp(0, &signedUp) == 0);
 REQUIRE(!signedUp);
 const int a = sceNpCreateRequest();
 const int b = sceNpCreateRequest();
 REQUIRE(a > 0 && b > 0 && a != b);
 REQUIRE(sceNpCreateAsyncRequest(nullptr) > 0);
 REQUIRE(sceNpDeleteRequest(a) == 0);
 REQUIRE(sceNpAbortRequest(a) == 0);
 REQUIRE(sceNpCheckCallback() == 0);
 REQUIRE(sceNpCheckNpAvailability(1, nullptr, nullptr) == NP_SIGNED_OUT);
 REQUIRE(sceNpCheckNpReachability(1, 0) > 0);
 REQUIRE(sceNpSetNpTitleId(nullptr, nullptr) == 0);
 REQUIRE(sceNpSetContentRestriction(nullptr) == 0);
 REQUIRE(sceNpNotifyPremiumFeature(nullptr) == 0);
 sceNpRegisterGamePresenceCallback(reinterpret_cast<void*>(1), nullptr);
 REQUIRE(sceNpRegisterStateCallback(reinterpret_cast<void*>(1), nullptr) == 0);
 REQUIRE(sceNpRegisterNpReachabilityStateCallback(nullptr, nullptr) == 0);
 REQUIRE(sceNpRegisterPlusEventCallback(nullptr, nullptr) == 0);
 REQUIRE(sceNpRegisterPremiumEventCallback(nullptr, nullptr) == 0);
 REQUIRE(sceNpUnregisterStateCallback() == 0);
 REQUIRE(sceNpRegisterStateCallbackA(nullptr, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpRegisterStateCallbackA(reinterpret_cast<void*>(1), nullptr) == 1);
 REQUIRE(sceNpUnregisterStateCallbackA(1) == 0);
 void* lang = reinterpret_cast<void*>(1);
 REQUIRE(sceNpGetAccountLanguage2(1, 0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetAccountLanguage2(1, 0, lang) == NP_SIGNED_OUT);
 uint8_t age = 99;
 REQUIRE(sceNpGetAccountAge(1, 0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpGetAccountAge(1, 0, &age) == NP_SIGNED_OUT);
}

void TestNpAuth() {
 const int a = sceNpAuthCreateRequest();
 REQUIRE(a > 0);
 REQUIRE(sceNpAuthCreateAsyncRequest(nullptr) > 0);
 REQUIRE(sceNpAuthDeleteRequest(a) == 0);
 REQUIRE(sceNpAuthAbortRequest(a) == 0);
 char code[16]{};
 int issuer = 0;
 REQUIRE(sceNpAuthGetAuthorizationCodeV3(1, nullptr, nullptr, &issuer) == NP_INVALID_ARG);
 REQUIRE(sceNpAuthGetAuthorizationCodeV3(1, nullptr, code, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpAuthGetAuthorizationCodeV3(1, nullptr, code, &issuer) == NP_SIGNED_OUT);
 REQUIRE(sceNpAuthGetIdTokenV3(1, nullptr, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpAuthGetIdTokenV3(1, nullptr, code) == NP_SIGNED_OUT);
 int result = 0;
 REQUIRE(sceNpAuthPollAsync(1, &result) == 0);
 REQUIRE(result == NP_SIGNED_OUT);
 // Why Wait behaves like Poll: offline never blocks on auth.
 REQUIRE(sceNpAuthWaitAsync(1, &result) == 0);
 REQUIRE(result == NP_SIGNED_OUT);
}

void TestWebApi() {
 int64_t req = 0;
 REQUIRE(sceNpWebApi2CreateRequest(1, nullptr, nullptr, nullptr, nullptr, nullptr) == WEBAPI_INVALID_ARG);
 REQUIRE(sceNpWebApi2CreateRequest(1, nullptr, nullptr, nullptr, nullptr, &req) == 0);
 REQUIRE(req > 0);
 REQUIRE(sceNpWebApi2CreateUserContext(1, 0) > 0);
 REQUIRE(sceNpWebApi2Initialize(1, 0) > 0);
 REQUIRE(sceNpWebApi2SendRequest(req, nullptr, 0, nullptr) == WEBAPI_UNAVAILABLE);
 REQUIRE(sceNpWebApi2GetHttpResponseHeaderValue(req, nullptr, nullptr, 0) == WEBAPI_UNAVAILABLE);
 REQUIRE(sceNpWebApi2AbortRequest(req) == 0);
 REQUIRE(sceNpWebApi2DeleteRequest(req) == 0);
 REQUIRE(sceNpWebApi2Terminate(1) == 0);
}

void TestNpSmall() {
 REQUIRE(sceNpCommerceDialogUpdateStatus() == 0);
 REQUIRE(sceNpSessionSignalingInitialize(nullptr) == 0);
 NpEntitlementAccessInitParam init{};
 NpEntitlementAccessBootParam boot{};
 REQUIRE(sceNpEntitlementAccessInitialize(nullptr, &boot) == NP_INVALID_ARG);
 REQUIRE(sceNpEntitlementAccessInitialize(&init, &boot) == 0);
 uint32_t sku = 99;
 REQUIRE(sceNpEntitlementAccessGetSkuFlag(nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpEntitlementAccessGetSkuFlag(&sku) == 0);
 REQUIRE(sku == 0);
 NpUnifiedEntitlementLabel label{};
 NpEntitlementAccessAddcontEntitlementInfo info{};
 REQUIRE(sceNpEntitlementAccessGetAddcontEntitlementInfo(0, nullptr, &info) == NP_INVALID_ARG);
 REQUIRE(sceNpEntitlementAccessGetAddcontEntitlementInfo(0, &label, &info) == 0);
 uint32_t hit = 99;
 REQUIRE(sceNpEntitlementAccessGetAddcontEntitlementInfoList(0, nullptr, 0, nullptr) == NP_INVALID_ARG);
 REQUIRE(sceNpEntitlementAccessGetAddcontEntitlementInfoList(0, nullptr, 0, &hit) == 0);
 REQUIRE(hit == 0);
 REQUIRE(sceNpGameIntentInitialize(nullptr) == 0);
 REQUIRE(sceNpGameIntentTerminate() == 0);
 NpGameIntentInfo intent{};
 REQUIRE(sceNpGameIntentReceiveIntent(nullptr) == -2141898748);
 REQUIRE(sceNpGameIntentReceiveIntent(&intent) == -2141898746);
 NpGameIntentData data{};
 char buf[16]{};
 REQUIRE(sceNpGameIntentGetPropertyValueString(nullptr, "k", buf, sizeof(buf)) == -2141898748);
 REQUIRE(sceNpGameIntentGetPropertyValueString(&data, "k", buf, sizeof(buf)) == -2141898745);
}

void TestTrophy() {
 int ctx = 0;
 REQUIRE(sceNpTrophy2CreateContext(nullptr, 0, 0, 0) == SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceNpTrophy2CreateContext(&ctx, 0, 0, 0) == SCE_NP_TROPHY2_OK);
 REQUIRE(ctx == NP_TROPHY2_CONTEXT_DEFAULT);
 int handle = 0;
 REQUIRE(sceNpTrophy2CreateHandle(nullptr) == SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceNpTrophy2CreateHandle(&handle) == SCE_NP_TROPHY2_OK);
 NpTrophy2GameDetails details{};
 NpTrophy2GameData gdata{};
 REQUIRE(sceNpTrophy2GetGameInfo(ctx, handle, nullptr, &gdata) == SCE_NP_TROPHY2_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceNpTrophy2GetGameInfo(ctx, handle, &details, &gdata) == SCE_NP_TROPHY2_OK);
 REQUIRE(details.num_trophies == NP_TROPHY2_NUM_TROPHIES);
 size_t size = 99;
 // Why not-found, not throw: missing icons are real console errors.
 REQUIRE(sceNpTrophy2GetGameIcon(ctx, handle, nullptr, &size) == SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND);
 REQUIRE(size == NP_TROPHY2_ICON_SIZE_NONE);
 NpTrophy2GroupDetails gdet{};
 NpTrophy2GroupData gdat{};
 REQUIRE(sceNpTrophy2GetGroupInfo(ctx, handle, 0, &gdet, &gdat) == SCE_NP_TROPHY2_OK);
 REQUIRE(sceNpTrophy2GetGroupIcon(ctx, handle, 0, nullptr, &size) == SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND);
 NpTrophy2Details tdet{};
 NpTrophy2Data tdat{};
 REQUIRE(sceNpTrophy2GetTrophyInfo(ctx, handle, 0, &tdet, &tdat) == SCE_NP_TROPHY2_OK);
 REQUIRE(!tdat.unlocked);
 REQUIRE(sceNpTrophy2GetTrophyIcon(ctx, handle, 0, nullptr, &size) == SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND);
 REQUIRE(sceNpTrophy2RegisterUnlockCallback(nullptr, nullptr) == SCE_NP_TROPHY2_OK);
}

void TestUds() {
 NpUniversalDataSystemInitParam param{sizeof(param), 4096};
 REQUIRE(sceNpUniversalDataSystemInitialize(nullptr) == -2141900542);
 REQUIRE(sceNpUniversalDataSystemInitialize(&param) == 0);
 int ctx = 0;
 REQUIRE(sceNpUniversalDataSystemCreateContext(nullptr, 0, 0, 0) == -2141900542);
 REQUIRE(sceNpUniversalDataSystemCreateContext(&ctx, 0, 0, 0) == 0);
 int handle = 0;
 REQUIRE(sceNpUniversalDataSystemCreateHandle(nullptr) == -2141900542);
 REQUIRE(sceNpUniversalDataSystemCreateHandle(&handle) == 0);
 // Why dropped, OK: offline accepts trophy-like events without uploading.
 REQUIRE(sceNpUniversalDataSystemPostEvent(ctx, handle, nullptr, 0) == 0);
 REQUIRE(sceNpUniversalDataSystemTerminate() == 0);
}

void TestUserService() {
 REQUIRE(sceUserServiceInitialize(nullptr) == USER_SERVICE_OK);
 int user = 0;
 REQUIRE(sceUserServiceGetInitialUser(nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetInitialUser(&user) == USER_SERVICE_OK);
 REQUIRE(user == USER_SERVICE_INITIAL_USER_ID);
 UserServiceLoginUserIdList list{};
 REQUIRE(sceUserServiceGetLoginUserIdList(nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetLoginUserIdList(&list) == USER_SERVICE_OK);
 REQUIRE(list.user_id[0] == USER_SERVICE_INITIAL_USER_ID);
 char name[64]{};
 REQUIRE(sceUserServiceGetUserName(user, nullptr, sizeof(name)) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetUserName(1, name, sizeof(name)) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetUserName(user, name, sizeof(name)) == USER_SERVICE_OK);
 REQUIRE(std::strcmp(name, "Player") == 0);
 int32_t number = -9;
 REQUIRE(sceUserServiceGetUserNumber(user, nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetUserNumber(user, &number) == USER_SERVICE_OK);
 REQUIRE(number == 0);
 SceUserServiceEvent ev{};
 REQUIRE(sceUserServiceGetEvent(nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetEvent(&ev) == USER_SERVICE_OK);
 REQUIRE(sceUserServiceGetEvent(&ev) == USER_SERVICE_ERROR_NO_EVENT);
 UserServiceGamePresets presets{};
 presets.this_size = sizeof(presets);
 REQUIRE(sceUserServiceGetGamePresets(1, &presets) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetGamePresets(user, &presets) == USER_SERVICE_OK);
 REQUIRE(presets.difficulty == 0);
 int32_t vib = -1;
 REQUIRE(sceUserServiceGetAccessibilityVibration(user, &vib) == USER_SERVICE_OK);
 REQUIRE(vib == 0);
 uint32_t age = 99;
 REQUIRE(sceUserServiceGetAgeLevel(user, &age) == USER_SERVICE_OK);
 REQUIRE(age == 0);
 int32_t priv = -1;
 REQUIRE(sceUserServiceGetPlatformPrivacyWs1(user, nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
 REQUIRE(sceUserServiceGetPlatformPrivacyWs1(user, &priv) == USER_SERVICE_OK);
 REQUIRE(priv == 0);
}

void TestSystemService() {
 int value = -1;
 REQUIRE(sceSystemServiceParamGetInt(SYSTEM_SERVICE_PARAM_ID_LANG, nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceParamGetInt(SYSTEM_SERVICE_PARAM_ID_LANG, &value) == SYSTEM_SERVICE_OK);
 REQUIRE(value == SYSTEM_SERVICE_PARAM_LANG_ENGLISH_US);
 REQUIRE(sceSystemServiceParamGetInt(9999, &value) == SYSTEM_SERVICE_OK);
 REQUIRE(value == 0);
 char buf[16];
 std::memset(buf, 0xCC, sizeof(buf));
 REQUIRE(sceSystemServiceParamGetString(0, nullptr, sizeof(buf)) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceParamGetString(0, buf, 0) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceParamGetString(0, buf, sizeof(buf)) == SYSTEM_SERVICE_OK);
 REQUIRE(buf[0] == '\0');
 SystemServiceDisplaySafeAreaInfo safe{};
 REQUIRE(sceSystemServiceGetDisplaySafeAreaInfo(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceGetDisplaySafeAreaInfo(&safe) == SYSTEM_SERVICE_OK);
 REQUIRE(safe.ratio == 1.0f);
 bool skip = true;
 REQUIRE(sceSystemServiceGetNoticeScreenSkipFlag(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceGetNoticeScreenSkipFlag(&skip) == SYSTEM_SERVICE_OK);
 REQUIRE(!skip);
 SystemServiceStatus status{};
 REQUIRE(sceSystemServiceGetStatus(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceGetStatus(&status) == SYSTEM_SERVICE_OK);
 REQUIRE(sceSystemServiceHideSplashScreen() == SYSTEM_SERVICE_OK);
 SystemServiceEvent event{};
 REQUIRE(sceSystemServiceReceiveEvent(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
 REQUIRE(sceSystemServiceReceiveEvent(&event) == SYSTEM_SERVICE_ERROR_NO_EVENT);
}

void TestSaveDataDialogNative() {
 REQUIRE(sceSaveDataDialogInitialize() == SAVE_DATA_DIALOG_OK);
 SaveDataDirName dir{};
 std::strncpy(dir.data, "SAVE00", sizeof(dir.data) - 1);
 SaveDataDialogItems items{};
 items.dir_names = &dir;
 items.dir_names_num = 1;
 SaveDataDialogParam param{};
 param.mode = 1;
 param.items = &items;
 REQUIRE(sceSaveDataDialogOpen(nullptr) == SAVE_DATA_DIALOG_ERROR_ARG_NULL);
 REQUIRE(sceSaveDataDialogOpen(&param) == SAVE_DATA_DIALOG_OK);
 // Why RUNNING here, FINISHED after UpdateStatus (M1 scripted-dialog rule).
 REQUIRE(sceSaveDataDialogGetStatus() == SAVE_DATA_DIALOG_STATUS_RUNNING);
 REQUIRE(sceSaveDataDialogUpdateStatus() == SAVE_DATA_DIALOG_STATUS_FINISHED);
 REQUIRE(sceSaveDataDialogIsReadyToDisplay() == 1);
 SaveDataDialogResult result{};
 SaveDataDirName outDir{};
 result.dir_name = &outDir;
 REQUIRE(sceSaveDataDialogGetResult(nullptr) == SAVE_DATA_DIALOG_ERROR_ARG_NULL);
 REQUIRE(sceSaveDataDialogGetResult(&result) == SAVE_DATA_DIALOG_OK);
 REQUIRE(result.result == SAVE_DATA_DIALOG_RESULT_OK);
 REQUIRE(std::strcmp(outDir.data, "SAVE00") == 0);
 REQUIRE(sceSaveDataDialogClose(nullptr) == SAVE_DATA_DIALOG_OK);
 REQUIRE(sceSaveDataDialogTerminate() == SAVE_DATA_DIALOG_OK);
}

void TestCommonDialog() {
 REQUIRE(sceCommonDialogInitialize() == COMMON_DIALOG_OK);
 // Why false: M1 has no cross-DLL open tracking; false stays non-blocking.
 REQUIRE(!sceCommonDialogIsUsed());
}

void TestPad() {
 REQUIRE(scePadInit_nid_postfix() == PAD_OK);
 const int handle = scePadOpen_nid_postfix(0, PAD_PORT_TYPE_STANDARD, 0, nullptr);
 REQUIRE(handle == PAD_HANDLE);
 PadControllerInformation info{};
 REQUIRE(scePadGetControllerInformation(0, &info) == PAD_ERROR_INVALID_HANDLE);
 REQUIRE(scePadGetControllerInformation(handle, nullptr) == PAD_ERROR_INVALID_ARG);
 REQUIRE(scePadGetControllerInformation(handle, &info) == PAD_OK);
 REQUIRE(info.connected);
 PadData data{};
 REQUIRE(scePadReadState(0, &data) == PAD_ERROR_INVALID_HANDLE);
 REQUIRE(scePadReadState(handle, nullptr) == PAD_ERROR_INVALID_ARG);
 REQUIRE(scePadReadState(handle, &data) == PAD_OK);
 REQUIRE(data.connected);
 REQUIRE(scePadRead_nid_postfix(handle, nullptr, 1) == PAD_ERROR_INVALID_ARG);
 REQUIRE(scePadRead_nid_postfix(handle, &data, 0) == PAD_ERROR_INVALID_ARG);
 // Why 1: PR #5 reports one current state per call; the title drains it.
 REQUIRE(scePadRead_nid_postfix(handle, &data, 1) == 1);
 REQUIRE(scePadSetMotionSensorState(handle, false) == PAD_OK);
}

}  // namespace

int main() {
 TestNpManager();
 TestNpAuth();
 TestWebApi();
 TestNpSmall();
 TestTrophy();
 TestUds();
 TestUserService();
 TestSystemService();
 TestSaveDataDialogNative();
 TestCommonDialog();
 TestPad();
 return 0;
}
