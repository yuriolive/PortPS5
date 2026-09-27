#include <cstddef>
#include <cstdint>
#include <new>
#include "NpUniversalDataSystem.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why returns, not throws: see EventPropertyObject.cpp.

extern "C" {

int APS5_VABI sceNpUniversalDataSystemCreateEventPropertyArray(NpUniversalDataSystemEventPropertyArray** new_array) noexcept {
 if (new_array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 *new_array = new (std::nothrow) NpUniversalDataSystemEventPropertyArray;
 if (*new_array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemDestroyEventPropertyArray(NpUniversalDataSystemEventPropertyArray* array) noexcept {
 delete array;
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetString(
    NpUniversalDataSystemEventPropertyArray* array, const char* value) noexcept {
 if (array == nullptr || value == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetInt32(
    NpUniversalDataSystemEventPropertyArray* array, int32_t value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetUInt32(
    NpUniversalDataSystemEventPropertyArray* array, uint32_t value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetInt64(
    NpUniversalDataSystemEventPropertyArray* array, int64_t value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetUInt64(
    NpUniversalDataSystemEventPropertyArray* array, uint64_t value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetFloat32(
    NpUniversalDataSystemEventPropertyArray* array, float value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetFloat64(
    NpUniversalDataSystemEventPropertyArray* array, double value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetBool(
    NpUniversalDataSystemEventPropertyArray* array, bool value) noexcept {
 (void)value;
 if (array == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetBinary(
    NpUniversalDataSystemEventPropertyArray* array, const void* value, size_t value_size) noexcept {
 (void)value_size;
 if (array == nullptr || value == nullptr) {
  return SCE_NP_UNIVERSAL_DATA_SYSTEM_ERROR_INVALID_ARGUMENT;
 }
 return SCE_NP_UNIVERSAL_DATA_SYSTEM_OK;
}

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetObject(
    NpUniversalDataSystemEventPropertyArray* array,
    const NpUniversalDataSystemEventPropertyObject* value,
    NpUniversalDataSystemEventPropertyObject** value_ptr) noexcept {
 if (array == nullptr) {
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

int APS5_VABI sceNpUniversalDataSystemEventPropertyArraySetArray(
    NpUniversalDataSystemEventPropertyArray* array,
    const NpUniversalDataSystemEventPropertyArray* value,
    NpUniversalDataSystemEventPropertyArray** value_ptr) noexcept {
 if (array == nullptr) {
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
