#include <cstddef>
#include <cstdint>
#include <new>
#include "NpUniversalDataSystem.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why returns, not throws: null outputs are real console errors; noexcept
// forbids throwing across the guest boundary. new (nothrow) keeps OOM from
// unwinding; OOM returns invalid-arg so boot fails loudly, never blocks.

extern "C" {

int APS5_VABI sceNpUniversalDataSystemCreateEventPropertyObject(NpUniversalDataSystemEventPropertyObject** new_object) noexcept {
 if (new_object == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 *new_object = new (std::nothrow) NpUniversalDataSystemEventPropertyObject;
 if (*new_object == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemDestroyEventPropertyObject(NpUniversalDataSystemEventPropertyObject* object) noexcept {
 delete object;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetString(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, const char* value) noexcept {
 if (object == nullptr || key == nullptr || value == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetInt32(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, int32_t value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetUInt32(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, uint32_t value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetInt64(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, int64_t value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetUInt64(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, uint64_t value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetFloat32(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, float value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetFloat64(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, double value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetBool(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, bool value) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetBinary(
    NpUniversalDataSystemEventPropertyObject* object, const char* key, const void* value, size_t value_size) noexcept {
 (void)value_size;
 if (object == nullptr || key == nullptr || value == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetObject(
    NpUniversalDataSystemEventPropertyObject* object,
    const char* key,
    const NpUniversalDataSystemEventPropertyObject* value,
    NpUniversalDataSystemEventPropertyObject** value_ptr) noexcept {
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 if (value_ptr != nullptr) {
  if (value != nullptr) {
   *value_ptr = const_cast<NpUniversalDataSystemEventPropertyObject*>(value);
  } else {
   *value_ptr = new (std::nothrow) NpUniversalDataSystemEventPropertyObject;
   if (*value_ptr == nullptr) {
    return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
   }
  }
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyObjectSetArray(
    NpUniversalDataSystemEventPropertyObject* object,
    const char* key,
    const NpUniversalDataSystemEventPropertyArray* value,
    NpUniversalDataSystemEventPropertyArray** value_ptr) noexcept {
 (void)value;
 if (object == nullptr || key == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 if (value_ptr != nullptr) {
  if (value != nullptr) {
   *value_ptr = const_cast<NpUniversalDataSystemEventPropertyArray*>(value);
  } else {
   *value_ptr = new (std::nothrow) NpUniversalDataSystemEventPropertyArray;
   if (*value_ptr == nullptr) {
    return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
   }
  }
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

}
