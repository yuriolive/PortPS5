#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "NpUniversalDataSystem.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceNpUniversalDataSystemCreateEvent(
    const char* event_name,
    const NpUniversalDataSystemEventPropertyObject* prop,
    NpUniversalDataSystemEvent** new_event,
    NpUniversalDataSystemEventPropertyObject** prop_ptr) noexcept {
 if (event_name == nullptr || new_event == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 // Why dropped, OK: offline has no upload; the event is accepted so the
 // title never blocks waiting for a trophy/presence round-trip.
 *new_event = new (std::nothrow) NpUniversalDataSystemEvent;
 if (*new_event == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 if (prop_ptr != nullptr) {
  if (prop != nullptr) {
   *prop_ptr = const_cast<NpUniversalDataSystemEventPropertyObject*>(prop);
  } else {
   *prop_ptr = new (std::nothrow) NpUniversalDataSystemEventPropertyObject;
   if (*prop_ptr == nullptr) {
    delete *new_event;
    *new_event = nullptr;
    return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
   }
  }
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemDestroyEvent(NpUniversalDataSystemEvent* event) noexcept {
 delete event;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemPostEvent(int context, int handle, const void* event, uint64_t options) noexcept {
 (void)context;
 (void)handle;
 (void)event;
 (void)options;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventEstimateSize(const NpUniversalDataSystemEvent* event, size_t* size) noexcept {
 if (event == nullptr || size == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 *size = NP_UNIVERSAL_DATA_SYSTEM_EMPTY_EVENT_SIZE;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventToString(
    const NpUniversalDataSystemEvent* event,
    char* buf,
    size_t buf_size,
    size_t* string_size) noexcept {
 if (event == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 const size_t json_len = std::strlen(NP_UNIVERSAL_DATA_SYSTEM_EMPTY_JSON) + 1;
 if (string_size != nullptr) {
  *string_size = json_len;
 }
 if (buf != nullptr && buf_size > 0) {
  std::snprintf(buf, buf_size, "%s", NP_UNIVERSAL_DATA_SYSTEM_EMPTY_JSON);
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

}
