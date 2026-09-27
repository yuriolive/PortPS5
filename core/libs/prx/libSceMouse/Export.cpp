#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Why Unsupported, not offline codes: M1 inventory shows no gate title
// imports libSceMouse at boot (docs/spec/input.md). Guest keyboard/mouse stay
// on the host pad path; a direct Mouse import is a loud gap, not silent OK.

extern "C" {

int APS5_VABI sceMouseClose(int32_t handle) noexcept {
 (void)handle;
 Unsupported(__func__);
}

int APS5_VABI sceMouseInit(void) noexcept {
 Unsupported(__func__);
}

int APS5_VABI sceMouseOpen(int user_id, int32_t type, int32_t index, const void* param) noexcept {
 (void)user_id;
 (void)type;
 (void)index;
 (void)param;
 Unsupported(__func__);
}

int APS5_VABI sceMouseRead(int32_t handle, MouseData* data, int32_t num) noexcept {
 (void)handle;
 (void)data;
 (void)num;
 Unsupported(__func__);
}

}
