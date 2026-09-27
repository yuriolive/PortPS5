#include <atomic>
#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why offline unavailable: the PSN web API has no service behind it, so
// handles can be created but every network round-trip fails fast instead of
// blocking. Values mirror pr5-upstream for boot compatibility.
static constexpr int SCE_NP_WEBAPI2_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80553402);
static constexpr int SCE_NP_WEBAPI2_ERROR_UNAVAILABLE = static_cast<int>(0x80553406);
static std::atomic<int> g_nextHandle{1};

extern "C" {

int APS5_VABI sceNpWebApi2AbortRequest(int64_t request_id) noexcept {
 (void)request_id;
 return 0;
}

int APS5_VABI sceNpWebApi2AddHttpRequestHeader(int64_t request_id, const char* field_name, const char* value) noexcept {
 (void)request_id;
 (void)field_name;
 (void)value;
 return 0;
}

void APS5_VABI sceNpWebApi2CheckTimeout(void) noexcept {
}

int APS5_VABI sceNpWebApi2CreateRequest(int user_context_id, const char* api_group, const char* path, const char* method, const void* content_parameter, int64_t* request_id) noexcept {
 (void)user_context_id;
 (void)api_group;
 (void)path;
 (void)method;
 (void)content_parameter;
 if (request_id == nullptr) {
  return SCE_NP_WEBAPI2_ERROR_INVALID_ARGUMENT;
 }
 *request_id = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
 return 0;
}

int APS5_VABI sceNpWebApi2CreateUserContext(int lib_ctx_id, int user_id) noexcept {
 (void)lib_ctx_id;
 (void)user_id;
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2DeleteRequest(int64_t request_id) noexcept {
 (void)request_id;
 return 0;
}

int APS5_VABI sceNpWebApi2DeleteUserContext(int user_context_id) noexcept {
 (void)user_context_id;
 return 0;
}

int APS5_VABI sceNpWebApi2GetHttpResponseHeaderValue(int64_t request_id, const char* field_name, char* value, size_t value_size) noexcept {
 (void)request_id;
 (void)field_name;
 (void)value;
 (void)value_size;
 return SCE_NP_WEBAPI2_ERROR_UNAVAILABLE;
}

int APS5_VABI sceNpWebApi2GetHttpResponseHeaderValueLength(int64_t request_id, const char* field_name, size_t* value_length) noexcept {
 (void)request_id;
 (void)field_name;
 (void)value_length;
 return SCE_NP_WEBAPI2_ERROR_UNAVAILABLE;
}

int APS5_VABI sceNpWebApi2Initialize(int lib_http_ctx_id, size_t pool_size) noexcept {
 (void)lib_http_ctx_id;
 (void)pool_size;
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2PushEventCreateFilter(int lib_ctx_id, int handle_id, const char* np_service_name, uint32_t np_service_label, const void* filter_param, size_t filter_param_num) noexcept {
 (void)lib_ctx_id;
 (void)handle_id;
 (void)np_service_name;
 (void)np_service_label;
 (void)filter_param;
 (void)filter_param_num;
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2PushEventCreateHandle(int lib_ctx_id) noexcept {
 (void)lib_ctx_id;
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2PushEventDeleteHandle(int lib_ctx_id, int handle_id) noexcept {
 (void)lib_ctx_id;
 (void)handle_id;
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventDeletePushContext(int user_context_id, const void* push_context_id) noexcept {
 (void)user_context_id;
 (void)push_context_id;
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventRegisterCallback(int user_context_id, int filter_id, void* callback, void* user_arg) noexcept {
 (void)user_context_id;
 (void)filter_id;
 (void)callback;
 (void)user_arg;
 // Why stored handle, never called: no push service exists offline.
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2ReadData(int64_t request_id, void* data, size_t size) noexcept {
 (void)request_id;
 (void)data;
 (void)size;
 return SCE_NP_WEBAPI2_ERROR_UNAVAILABLE;
}

int APS5_VABI sceNpWebApi2SendRequest(int64_t request_id, const void* data, size_t data_size, NpWebApi2ResponseInformationOption* response_info_option) noexcept {
 (void)request_id;
 (void)data;
 (void)data_size;
 (void)response_info_option;
 return SCE_NP_WEBAPI2_ERROR_UNAVAILABLE;
}

int APS5_VABI sceNpWebApi2Terminate(int lib_ctx_id) noexcept {
 (void)lib_ctx_id;
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventCreatePushContext(void) noexcept {
 return SCE_NP_WEBAPI2_ERROR_UNAVAILABLE;
}

int APS5_VABI sceNpWebApi2PushEventDeleteFilter(void) noexcept {
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventRegisterPushContextCallback(void) noexcept {
 return g_nextHandle.fetch_add(1, std::memory_order_relaxed);
}

int APS5_VABI sceNpWebApi2PushEventStartPushContextCallback(void) noexcept {
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventUnregisterCallback(void) noexcept {
 return 0;
}

int APS5_VABI sceNpWebApi2PushEventUnregisterPushContextCallback(void) noexcept {
 return 0;
}

}
