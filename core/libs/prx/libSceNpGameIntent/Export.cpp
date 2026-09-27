#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr int NpGameIntentErrorInvalidArgument = -2141898748;
constexpr int NpGameIntentErrorIntentNotFound = -2141898746;
constexpr int NpGameIntentErrorValueNotFound = -2141898745;
constexpr int NpGameIntentUserIdInvalid = -1;

}

extern "C" {

int APS5_VABI sceNpGameIntentGetPropertyValueString(const NpGameIntentData* intentData, const char* key, char* valueBuf, size_t bufSize) noexcept {
 if (intentData == nullptr || key == nullptr || valueBuf == nullptr || bufSize == 0) {
  // Why return, not throw: invalid args are real console errors; noexcept
  // forbids throwing across the guest boundary.
  return NpGameIntentErrorInvalidArgument;
 }

 valueBuf[0] = '\0';
 return NpGameIntentErrorValueNotFound;
}

int APS5_VABI sceNpGameIntentInitialize(const void* initParam) noexcept {
 (void)initParam;
 return 0;
}

int APS5_VABI sceNpGameIntentReceiveIntent(NpGameIntentInfo* intentInfo) noexcept {
 if (intentInfo == nullptr) {
  return NpGameIntentErrorInvalidArgument;
 }

 intentInfo->user_id = NpGameIntentUserIdInvalid;
 std::memset(intentInfo->intent_type, 0, sizeof(intentInfo->intent_type));
 std::memset(&intentInfo->intent_data, 0, sizeof(intentInfo->intent_data));

 return NpGameIntentErrorIntentNotFound;
}

int APS5_VABI sceNpGameIntentTerminate(void) noexcept {
 return 0;
}

}
