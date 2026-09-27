#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why Unsupported, not offline codes: M1 inventory shows no gate title
// imports libSceKeyboard at boot (docs/spec/input.md). Guest keyboard stays
// on the host pad bindings; a direct Keyboard import is a loud gap.

extern "C" {

int APS5_VABI sceKeyboardClose(int32_t handle) noexcept {
 (void)handle;
 Unsupported(__func__);
}

int APS5_VABI sceKeyboardGetKey2Char(int32_t handle, int32_t arrange, uint32_t led, uint32_t modifier_key, uint16_t key_code, KeyboardCharData* char_data) noexcept {
 (void)handle;
 (void)arrange;
 (void)led;
 (void)modifier_key;
 (void)key_code;
 (void)char_data;
 Unsupported(__func__);
}

int APS5_VABI sceKeyboardInit(void) noexcept {
 Unsupported(__func__);
}

int APS5_VABI sceKeyboardOpen(int user_id, int32_t type, int32_t index, const void* param) noexcept {
 (void)user_id;
 (void)type;
 (void)index;
 (void)param;
 Unsupported(__func__);
}

int APS5_VABI sceKeyboardRead(int32_t handle, KeyboardData* data, int32_t num) noexcept {
 (void)handle;
 (void)data;
 (void)num;
 Unsupported(__func__);
}

int APS5_VABI sceKeyboardReadState(int32_t handle, KeyboardData* data) noexcept {
 (void)handle;
 (void)data;
 Unsupported(__func__);
}

}
