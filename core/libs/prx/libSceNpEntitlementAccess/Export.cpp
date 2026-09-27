#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why offline empty: no PSN entitlements exist, so titles see the base game
// only. Empty results keep boot on the offline path instead of waiting.
static constexpr int SCE_NP_ENTITLEMENT_OK = 0;
static constexpr int SCE_NP_ENTITLEMENT_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80550003);

extern "C" {

int APS5_VABI sceNpEntitlementAccessGetAddcontEntitlementInfo(uint32_t service_label, const NpUnifiedEntitlementLabel* entitlement_label, NpEntitlementAccessAddcontEntitlementInfo* info) noexcept {
 (void)service_label;
 if (entitlement_label == nullptr || info == nullptr) {
  return SCE_NP_ENTITLEMENT_ERROR_INVALID_ARGUMENT;
 }
 std::memset(info, 0, sizeof(*info));
 return SCE_NP_ENTITLEMENT_OK;
}

int APS5_VABI sceNpEntitlementAccessGetAddcontEntitlementInfoList(uint32_t service_label, NpEntitlementAccessAddcontEntitlementInfo* list, uint32_t list_num, uint32_t* hit_num) noexcept {
 (void)service_label;
 if (hit_num == nullptr) {
  return SCE_NP_ENTITLEMENT_ERROR_INVALID_ARGUMENT;
 }
 if (list != nullptr && list_num != 0) {
  std::memset(list, 0, sizeof(*list) * list_num);
 }
 *hit_num = 0;
 return SCE_NP_ENTITLEMENT_OK;
}

int APS5_VABI sceNpEntitlementAccessGetSkuFlag(uint32_t* sku_flag) noexcept {
 if (sku_flag == nullptr) {
  return SCE_NP_ENTITLEMENT_ERROR_INVALID_ARGUMENT;
 }
 *sku_flag = 0;
 return SCE_NP_ENTITLEMENT_OK;
}

int APS5_VABI sceNpEntitlementAccessInitialize(const NpEntitlementAccessInitParam* init_param, NpEntitlementAccessBootParam* boot_param) noexcept {
 if (init_param == nullptr) {
  return SCE_NP_ENTITLEMENT_ERROR_INVALID_ARGUMENT;
 }
 if (boot_param != nullptr) {
  std::memset(boot_param, 0, sizeof(*boot_param));
 }
 return SCE_NP_ENTITLEMENT_OK;
}

}
