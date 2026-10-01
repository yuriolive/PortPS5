// tests/modules/SysmoduleTests.cpp
// GoogleTest suite for libSceSysmodule load/unload bookkeeping. Pins the policy that ids unknown
// to the module table are accepted on load and are a logged no-op on unload (an uncaught exception
// across the APS5_VABI boundary would abort a title that loads and later unloads such a module),
// and that reference counting of known ids keeps returning its error codes.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceSysmodule/ModuleTable.hpp"

#include <cstdint>

extern "C" {
int APS5_VABI sceSysmoduleIsLoaded(std::uint16_t id);
int APS5_VABI sceSysmoduleLoadModule(std::uint16_t id);
int APS5_VABI sceSysmoduleUnloadModule(std::uint16_t id);
}

namespace {

constexpr int kNotLoaded = static_cast<int>(0x80A90002u);
constexpr int kUnloadNotLoaded = static_cast<int>(0x80A90003u);
constexpr std::uint16_t kKnownId = 0x0001;    // libSceAudioOut in the module table.
constexpr std::uint16_t kUnknownId = 0xFFF0;  // Not a module the project knows.

// An id missing from the module table: Load accepts it, and Unload must be a no-op returning 0
// (it used to throw std::runtime_error, which aborted titles that unload what they loaded).
TEST(SysmoduleTests, UnloadOfUnknownIdIsNoOp) {
    ASSERT_FALSE(kModuleTable.contains(kUnknownId)) << "test id became a real module; pick another";
    EXPECT_EQ(sceSysmoduleLoadModule(kUnknownId), 0);
    EXPECT_NO_THROW({ EXPECT_EQ(sceSysmoduleUnloadModule(kUnknownId), 0); });
    // Unloading again, or without ever loading, stays a no-op.
    EXPECT_NO_THROW({ EXPECT_EQ(sceSysmoduleUnloadModule(kUnknownId), 0); });
}

// A known id is reference counted: IsLoaded reports not-loaded before the first load, loaded
// until the last unload, and an extra unload reports 0x80A90003.
TEST(SysmoduleTests, KnownIdIsReferenceCounted) {
    ASSERT_TRUE(kModuleTable.contains(kKnownId));
    EXPECT_EQ(sceSysmoduleIsLoaded(kKnownId), kNotLoaded);
    EXPECT_EQ(sceSysmoduleUnloadModule(kKnownId), kUnloadNotLoaded);
    EXPECT_EQ(sceSysmoduleLoadModule(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleLoadModule(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleIsLoaded(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleUnloadModule(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleIsLoaded(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleUnloadModule(kKnownId), 0);
    EXPECT_EQ(sceSysmoduleIsLoaded(kKnownId), kNotLoaded);
    EXPECT_EQ(sceSysmoduleUnloadModule(kKnownId), kUnloadNotLoaded);
}

}  // namespace
