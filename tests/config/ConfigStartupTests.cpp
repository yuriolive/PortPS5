// Configuration startup regressions using synthetic install trees, never game data.
// Each test resets libc's singleton; CTest isolates tests in separate processes.
#include "prx/libc/include/config/Config.hpp"
#include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>
#endif

namespace {

/** Own a temporary install tree and clear configuration before removing its files. */
class ConfigStartup : public testing::Test {
protected:
    std::filesystem::path root;
    std::string previousDebug;
    bool hadDebug = false;

    // Config scans the host environment, so inherited profiling must not affect fixtures.
    void SetDebug(const char* value) {
#ifdef _WIN32
        // Config reads the CRT `_environ` copy, which SetEnvironmentVariableA does not update;
        // _putenv_s does. An empty value removes the variable.
        ASSERT_EQ(_putenv_s("PORTPS5_DEBUG", value ? value : ""), 0);
#else
        ASSERT_EQ(value ? ::setenv("PORTPS5_DEBUG", value, 1) : ::unsetenv("PORTPS5_DEBUG"), 0);
#endif
    }

    void SetUp() override {
        PortPS5::Config::Loader::ResetForTesting();
        const char* debug = std::getenv("PORTPS5_DEBUG");
        hadDebug = debug != nullptr;
        previousDebug = debug ? debug : "";
        SetDebug(nullptr);
        root = std::filesystem::temp_directory_path() /
            ("portps5-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "app0/sce_sys");
        std::filesystem::create_directories(root / "config/games");
        std::ofstream(root / "app0/sce_sys/param.json") <<
            R"({"titleId":"PPSA00000","localizedParameters":{"en-US":{"titleName":"Synthetic"}}})";
    }

    void TearDown() override {
        PortPS5::Config::Loader::ResetForTesting();
        std::filesystem::remove_all(root);
        SetDebug(hadDebug ? previousDebug.c_str() : nullptr);
    }

    bool Start() {
        return PortPS5_Config_Startup_nid_no_patch((root / "eboot.exe").string().c_str());
    }
};

// A real on-disk game layer must enable the no-argument gate before the first flip,
// even though the test's working directory is outside the install tree.
TEST_F(ConfigStartup, GameGpuProfileEnablesFirstFrameReport) {
    std::ofstream(root / "config/global.toml") << "schema = 1\n";
    std::ofstream(root / "config/games/PPSA00000.toml") <<
        "schema = 1\ntitle_id = \"PPSA00000\"\n[debug]\nprofile = [\"gpu\"]\n";
    ASSERT_TRUE(Start());
    EXPECT_EQ(PortPS5::Config::Loader::Get().titleId, "PPSA00000");
    ASSERT_TRUE(AgcDriver::FrameTimingReportEnabled());
    AgcDriver::FrameTiming timing(1);
    const auto now = AgcDriver::FrameTiming::Clock::now();
    timing.IncludeSubmission(1, now, now, now, true);
    timing.SetFlip(1, 0, now, now);
    testing::internal::CaptureStdout();
    timing.Print(1, 0, 0, now, AgcDriver::FrameTiming::Clock::duration::zero());
    EXPECT_NE(testing::internal::GetCapturedStdout().find("[FrameTiming]"), std::string::npos);
}

// Default startup succeeds with missing config files, while reporting stays off.
TEST_F(ConfigStartup, MissingConfigUsesDefaults) {
    ASSERT_TRUE(Start());
    EXPECT_TRUE(PortPS5::Config::Loader::IsInitialized());
    EXPECT_FALSE(AgcDriver::FrameTimingReportEnabled());
}

// Invalid configuration must return failure and preserve the file/key diagnostic;
// the entry stub uses this failure to exit before guest code can run.
TEST_F(ConfigStartup, InvalidConfigFailsBeforePublishing) {
    std::ofstream(root / "config/global.toml") << "schema = 1\n[debug]\nprofile = [\"bogus\"]\n";
    testing::internal::CaptureStderr();
    const bool started = Start();
    const auto diagnostic = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(started);
    EXPECT_FALSE(PortPS5::Config::Loader::IsInitialized());
    EXPECT_NE(diagnostic.find("global.toml:3: debug.profile:"), std::string::npos);
}

// The supported environment override reaches the same production startup path.
TEST_F(ConfigStartup, EnvironmentGpuProfileEnablesReport) {
    SetDebug("profile=gpu");
    ASSERT_TRUE(Start());
    EXPECT_TRUE(AgcDriver::FrameTimingReportEnabled());
}

// Metadata failures stay inside the noexcept host boundary, with no partial config.
TEST_F(ConfigStartup, MissingMetadataFailsBeforePublishing) {
    std::filesystem::remove(root / "app0/sce_sys/param.json");
    EXPECT_FALSE(Start());
    EXPECT_FALSE(PortPS5::Config::Loader::IsInitialized());
}

} // namespace
