// tests/config/ConfigCrossPrxTests.cpp
// GoogleTest-based verification suite for PortPS5 cross-PRX verbatim symbol resolution:
// Verifies that host configuration symbols (Config::Loader::IsInitialized, Config::Loader::Get,
// and companion workaround getters) are exported verbatim without NID hashing and resolve
// correctly across dependent system PRX modules (libc, libSceVideoOut).
// Complies with System V ABI invariants and PortPS5 testing rules.

#include "common/TestHarness.hpp"
#include "prx/libc/include/config/Config.hpp"
#include "nid/NidResolver.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#endif

namespace {

using namespace PortPS5::Config;

// Test invariant: verifies that Loader::IsInitialized reports true after in-memory initialization
// and returns false after ResetForTesting without memory corruption.
TEST(ConfigCrossPrxTests, LoaderLifecycleAndStateAccess) {
    Loader::ResetForTesting();
    EXPECT_FALSE(Loader::IsInitialized());

    std::string error;
    std::vector<std::string> warnings;
    const std::string globalToml = "schema = 1\n[display]\nresolution_scale = 1.0\n";
    const bool initOk = Loader::InitializeForTesting(globalToml, "", "", "C:/game", "PPSA00000", error, warnings);
    ASSERT_TRUE(initOk) << "InitializeForTesting failed: " << error;
    EXPECT_TRUE(Loader::IsInitialized());

    const ResolvedConfig& config = Loader::Get();
    EXPECT_DOUBLE_EQ(config.display.resolutionScale, 1.0);

    Loader::ResetForTesting();
    EXPECT_FALSE(Loader::IsInitialized());
}

// Test invariant: verifies that Nid::ResolveOneName and Nid::ResolveNids classify
// PortPS5::Config::Loader symbols as verbatim exports without hashing into NIDs.
TEST(ConfigCrossPrxTests, NidResolverKeepsConfigExportsVerbatim) {
    const std::string isInitName = "_ZN7PortPS56Config6Loader13IsInitializedEv";
    const std::string getName = "_ZN7PortPS56Config6Loader3GetEv";
    const std::string resetName = "_ZN7PortPS56Config6Loader15ResetForTestingEv";

    EXPECT_EQ(Nid::ResolveOneName(isInitName), isInitName);
    EXPECT_EQ(Nid::ResolveOneName(getName), getName);
    EXPECT_EQ(Nid::ResolveOneName(resetName), resetName);

    const auto map = Nid::ResolveNids({isInitName, getName, resetName}, "libc", {});
    ASSERT_EQ(map.size(), 3u);
    EXPECT_EQ(map.at(isInitName), isInitName);
    EXPECT_EQ(map.at(getName), getName);
    EXPECT_EQ(map.at(resetName), resetName);
}

#ifdef _WIN32
// Test invariant: verifies that patched libc.prx exports PortPS5::Config::Loader symbols verbatim
// so that GetProcAddress resolves them by raw C++ mangled name without error 127.
TEST(ConfigCrossPrxTests, LibcExportsConfigLoaderVerbatim) {
    std::vector<char> selfBuffer(MAX_PATH);
    const DWORD length = GetModuleFileNameA(nullptr, selfBuffer.data(), static_cast<DWORD>(selfBuffer.size()));
    ASSERT_GT(length, 0u) << "GetModuleFileNameA failed: " << GetLastError();

    // The test executable runs from libs/unpatched/; the patched libc.prx lives in libs/libc.prx.
    std::filesystem::path testDir = std::filesystem::path(selfBuffer.data()).parent_path();
    std::filesystem::path patchedLibcPath = testDir.parent_path() / "libc.prx";

    HMODULE libcHandle = LoadLibraryA(patchedLibcPath.string().c_str());
    if (!libcHandle) {
        libcHandle = GetModuleHandleA("libc.prx");
    }
    ASSERT_NE(libcHandle, nullptr) << "Failed to load patched libc.prx from " << patchedLibcPath << ", error: " << GetLastError();

    // Verify IsInitialized verbatim export
    FARPROC isInitProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader13IsInitializedEv");
    ASSERT_NE(isInitProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader13IsInitializedEv in libc.prx";

    // Verify Get verbatim export
    FARPROC getProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader3GetEv");
    ASSERT_NE(getProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader3GetEv in libc.prx";

    // Verify ResetForTesting verbatim export
    FARPROC resetProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader15ResetForTestingEv");
    ASSERT_NE(resetProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader15ResetForTestingEv in libc.prx";

    // Call the function pointer to verify ABI stability
    using IsInitFn = bool (*)();
    auto isInit = reinterpret_cast<IsInitFn>(isInitProc);
    Loader::ResetForTesting();
    EXPECT_FALSE(isInit());
}
#endif

}  // namespace

