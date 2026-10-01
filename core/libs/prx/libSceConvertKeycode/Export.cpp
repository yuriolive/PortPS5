// core/libs/prx/libSceConvertKeycode/Export.cpp
// libSceConvertKeycode: keyboard-type query for the IME. Only sceConvertKeycodeGetImeKeyboardType has a
// known signature (KytyPS5 src/libs/libConvertKeycode.cpp, GPL-2.0, is the oracle for it and for the
// error codes); sceConvertKeycodeGetVirtualKeycode is exported so the import resolves, but its
// signature is unverified, so it fails loudly through Unsupported() instead of guessing a layout.
// Both exports are APS5_VABI + noexcept; there is no state.

#include <cstdint>

#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {
constexpr int kErrInvalidAddress = static_cast<int>(0x80BC0031u);
constexpr int kErrInvalidUserId = static_cast<int>(0x80BC0010u);
}  // namespace

extern "C" {

/**
 * Reports the IME keyboard type of a user; always type 0 (the default layout) offline.
 * Returns 0; INVALID_ADDRESS for a null `type`; INVALID_USER_ID for a negative user id. The null
 * check comes first, as in the oracle.
 */
int APS5_VABI sceConvertKeycodeGetImeKeyboardType(std::int32_t user_id, std::uint32_t* type) noexcept {
    if (type == nullptr) {
        return kErrInvalidAddress;
    }
    if (user_id < 0) {
        return kErrInvalidUserId;
    }
    *type = 0;
    return 0;
}

/**
 * Unaudited: the argument layout is unknown, so the call logs and aborts through Unsupported()
 * rather than writing through a guessed pointer or returning a made-up result.
 */
int APS5_VABI sceConvertKeycodeGetVirtualKeycode(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept {
    Unsupported("sceConvertKeycodeGetVirtualKeycode: signature not audited");
}

}  // extern "C"
