// FrameTimingGateTests.cpp - regression tests for the [FrameTiming] report gate.
//
// Subsystem: libSceAgcDriver Execution (PerformanceTimer.hpp), consumed by libSceVideoOut.
// Why: the per-frame report is ~20 KB of text plus an fflush(stdout). It used to be written on
// every flip by default, which made console runs of a converted title several times slower
// than necessary (Dreaming Sarah boot profile). It is now a profiling aid enabled only by
// `[debug] profile = ["gpu"]` (docs/spec/configuration.md). No GPU or game data is needed.
#include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {

using AgcDriver::FrameTiming;
using AgcDriver::FrameTimingReportEnabled;
using PortPS5::Config::DebugConfig;
using PortPS5::Config::ProfileCategory;

// Gives `timing` the minimal valid lineage Print() requires (one submission, one flip).
// FrameTiming owns a mutex and is not movable, so it is filled in place rather than returned.
void MakeCompleteFrame(FrameTiming& timing) {
    const auto now = FrameTiming::Clock::now();
    timing.IncludeSubmission(1, now, now, now, true);
    timing.SetFlip(1, 0, now, now);
}

// Invariant: a default config (no `profile` entries) leaves the report disabled.
TEST(FrameTimingGate, DisabledByDefault) {
    EXPECT_FALSE(FrameTimingReportEnabled(DebugConfig{}));
}

// Invariant: `profile = ["gpu"]` enables the report.
TEST(FrameTimingGate, EnabledByGpuProfile) {
    DebugConfig debug;
    debug.profile.insert(ProfileCategory::Gpu);
    EXPECT_TRUE(FrameTimingReportEnabled(debug));
}

// Invariant: with the config loader uninitialised (this process never loads one), Print()
// writes nothing. Fails without the gate, which printed "[FrameTiming] ..." unconditionally.
// Precondition: no Config::Loader::Initialize call in this test binary.
TEST(FrameTimingGate, PrintIsSilentWithoutProfileConfig) {
    FrameTiming timing(1);
    MakeCompleteFrame(timing);
    testing::internal::CaptureStdout();
    timing.Print(1, 0, 0, FrameTiming::Clock::now(), FrameTiming::Clock::duration::zero());
    const std::string out = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(out.empty()) << "unexpected report: " << out;
}

}  // namespace
