#include <atomic>
#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why offline signed-out: Auth has no service behind it, so token and code
// queries fail and async requests finish immediately instead of blocking.
static constexpr int SCE_NP_AUTH_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);
static constexpr int SCE_NP_AUTH_ERROR_SIGNED_OUT = static_cast<int>(0x80550006);
static constexpr int NP_AUTH_POLL_FINISHED = 0;

static std::atomic<int> g_nextAuthRequest{1};

extern "C" {

int APS5_VABI sceNpAuthAbortRequest(int req_id) noexcept {
 (void)req_id;
 return 0;
}

int APS5_VABI sceNpAuthCreateAsyncRequest(const void* param) noexcept {
 (void)param;
 return g_nextAuthRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpAuthCreateRequest(void) noexcept {
 return g_nextAuthRequest.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpAuthDeleteRequest(int req_id) noexcept {
 (void)req_id;
 return 0;
}

int APS5_VABI sceNpAuthGetAuthorizationCodeV3(int req_id, const void* param, void* auth_code, int* issuer_id) noexcept {
 (void)req_id;
 (void)param;
 if (auth_code == nullptr || issuer_id == nullptr) {
  return SCE_NP_AUTH_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_AUTH_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpAuthGetIdTokenV3(int req_id, const void* param, void* id_token) noexcept {
 (void)req_id;
 (void)param;
 if (id_token == nullptr) {
  return SCE_NP_AUTH_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_AUTH_ERROR_SIGNED_OUT;
}

int APS5_VABI sceNpAuthPollAsync(int req_id, int* result) noexcept {
 (void)req_id;
 if (result != nullptr) {
  *result = SCE_NP_AUTH_ERROR_SIGNED_OUT;
 }
 return NP_AUTH_POLL_FINISHED;
}

int APS5_VABI sceNpAuthWaitAsync(int req_id, int* result) noexcept {
 (void)req_id;
 // Why no block: console Wait would sleep until the network replies; offline
 // completes at once with signed-out so boot never stalls on auth.
 if (result != nullptr) {
  *result = SCE_NP_AUTH_ERROR_SIGNED_OUT;
 }
 return NP_AUTH_POLL_FINISHED;
}

}
