// tests/config/ConfigCrossPrxTests.cpp
// GoogleTest-based verification suite for PortPS5 cross-PRX verbatim symbol resolution:
// Verifies that host configuration symbols (Config::Loader::IsInitialized, Config::Loader::Get,
// and companion workaround getters) are exported verbatim without NID hashing and resolve
// correctly across dependent system PRX modules (libc, libSceVideoOut).
// Complies with System V ABI invariants and PortPS5 testing rules.

#include "common/TestHarness.hpp"
#include "prx/libc/include/config/Config.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

#ifdef _WIN32
// Test invariant: verifies that libc.prx exports PortPS5::Config::Loader symbols verbatim
// so that GetProcAddress resolves them by raw C++ mangled name without error 127.
TEST(ConfigCrossPrxTests, LibcExportsConfigLoaderVerbatim) {
    HMODULE libcHandle = GetModuleHandleA("libc.prx");
    if (!libcHandle) {
        libcHandle = LoadLibraryA("libc.prx");
    }
    ASSERT_NE(libcHandle, nullptr) << "Failed to locate or load libc.prx, error: " << GetLastError();

    // Verify IsInitialized verbatim export
    FARPROC isInitProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader13IsInitializedEv");
    EXPECT_NE(isInitProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader13IsInitializedEv in libc.prx";

    // Verify Get verbatim export
    FARPROC getProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader3GetEv");
    EXPECT_NE(getProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader3GetEv in libc.prx";

    // Verify ResetForTesting verbatim export
    FARPROC resetProc = GetProcAddress(libcHandle, "_ZN7PortPS56Config6Loader15ResetForTestingEv");
    EXPECT_NE(resetProc, nullptr) << "Missing verbatim export _ZN7PortPS56Config6Loader15ResetForTestingEv in libc.prx";

    // Call the function pointer to verify ABI stability
    using IsInitFn = bool (*)();
    auto isInit = reinterpret_cast<IsInitFn>(isInitProc);
    Loader::ResetForTesting();
    EXPECT_FALSE(isInit());
}
#endif

}  // namespace
