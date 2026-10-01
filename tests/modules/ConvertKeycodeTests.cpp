// tests/modules/ConvertKeycodeTests.cpp
// GoogleTest suite for libSceConvertKeycode: the keyboard-type query (argument order of the checks
// follows the KytyPS5 oracle) and the death path of the unaudited sceConvertKeycodeGetVirtualKeycode,
// which must abort loudly instead of guessing a pointer layout.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstdint>

extern "C" {
int APS5_VABI sceConvertKeycodeGetImeKeyboardType(std::int32_t user_id, std::uint32_t* type);
int APS5_VABI sceConvertKeycodeGetVirtualKeycode(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
}

namespace {

constexpr int kInvalidAddress = static_cast<int>(0x80BC0031u);
constexpr int kInvalidUserId = static_cast<int>(0x80BC0010u);

// A valid user id reports keyboard type 0 and writes through the pointer; id 0 is valid.
TEST(ConvertKeycodeTests, KeyboardTypeForValidUser) {
    std::uint32_t type = 0xDEADBEEF;
    EXPECT_EQ(sceConvertKeycodeGetImeKeyboardType(1, &type), 0);
    EXPECT_EQ(type, 0u);
    type = 0xDEADBEEF;
    EXPECT_EQ(sceConvertKeycodeGetImeKeyboardType(0, &type), 0);
    EXPECT_EQ(type, 0u);
}

// Error codes and their precedence: a null out pointer is INVALID_ADDRESS even for a bad user id,
// a negative user id is INVALID_USER_ID and leaves the output untouched.
TEST(ConvertKeycodeTests, ErrorCodesAndPrecedence) {
    EXPECT_EQ(sceConvertKeycodeGetImeKeyboardType(1, nullptr), kInvalidAddress);
    EXPECT_EQ(sceConvertKeycodeGetImeKeyboardType(-1, nullptr), kInvalidAddress);
    std::uint32_t type = 0xDEADBEEF;
    EXPECT_EQ(sceConvertKeycodeGetImeKeyboardType(-1, &type), kInvalidUserId);
    EXPECT_EQ(type, 0xDEADBEEFu);
}

// The virtual-keycode call has no audited signature, so it must abort (Unsupported) rather than
// return a fabricated result. Expected failure mode: process death.
TEST(ConvertKeycodeTests, VirtualKeycodeAbortsLoudly) {
    EXPECT_DEATH(sceConvertKeycodeGetVirtualKeycode(1, 2, 3, 4), ".*");
}

}  // namespace
