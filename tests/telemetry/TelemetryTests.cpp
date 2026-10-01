// GoogleTest coverage for the telemetry core (core/libs/prx/libc/include/telemetry/Telemetry.hpp).
//
// All tests use a synthetic clock and an in-memory sink: no sleeps, no threads,
// no game data. They pin the record schema read by tools/regress.py, the
// 1 s stall / 30 s softlock definitions, the latched watchdog verdict and the
// audio / A/V-offset sampler.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "prx/libc/include/telemetry/Telemetry.hpp"

namespace {
using namespace PortPS5::Telemetry;

class MemorySink final : public LineSink {
public:
    void Write(std::string_view line) override { lines.emplace_back(line); }
    std::vector<std::string> lines;
};

// Invariant: the first record is run.start with the schema tag the runner requires.
TEST(TelemetryLog, RunStartHeaderMatchesRunnerContract) {
    MemorySink sink;
    Log log(sink);
    log.RunStart(1920, 1080, true, "none");
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.lines[0],
              "{\"ev\":\"run.start\",\"schema\":\"portps5.telemetry/1\",\"resolution\":\"1920x1080\","
              "\"pipeline_cache\":\"warm\",\"audio_device\":\"none\"}");
}

// Invariant: the first present sets the baseline and logs nothing; later presents log dt_ms and t_ms (the absolute time lets the runner detect a hang after the last present).
// A failure here would shift every frame time by one frame or invent a first interval.
TEST(TelemetryLog, FrameLogsIntervalsAfterBaseline) {
    MemorySink sink;
    Log log(sink);
    EXPECT_EQ(log.Frame(1000), 0u);
    EXPECT_TRUE(sink.lines.empty());
    EXPECT_EQ(log.Frame(1016), 16u);
    EXPECT_EQ(log.Frame(3016), 2000u);  // a stall is logged as-is; the reader excludes it
    ASSERT_EQ(sink.lines.size(), 2u);
    EXPECT_EQ(sink.lines[0], "{\"ev\":\"frame\",\"dt_ms\":16,\"t_ms\":1016}");
    EXPECT_EQ(sink.lines[1], "{\"ev\":\"frame\",\"dt_ms\":2000,\"t_ms\":3016}");
}

// Invariant: events carry only numeric fields; integers print without a fraction.
TEST(TelemetryLog, EventFormatsNumbersOnly) {
    MemorySink sink;
    Log log(sink);
    log.Event("dialog.open");
    log.Event("av.offset", {{"ms", -42.5}, {"n", 3.0}});
    EXPECT_EQ(sink.lines[0], "{\"ev\":\"dialog.open\"}");
    EXPECT_EQ(sink.lines[1], "{\"ev\":\"av.offset\",\"ms\":-42.500,\"n\":3}");
}

// Invariant: the watchdog is silent until armed, silent at exactly the limit and trips above it.
TEST(TelemetryWatchdog, PresentStallTripsAboveLimitOnly) {
    Watchdog wd;
    EXPECT_EQ(wd.Evaluate(100000), WatchdogVerdict::Ok);  // not armed
    wd.Arm(0);
    wd.NotePresent(1000);
    EXPECT_EQ(wd.Evaluate(31000), WatchdogVerdict::Ok);  // exactly 30 s idle
    std::uint64_t idle = 0;
    EXPECT_EQ(wd.Evaluate(31001, &idle), WatchdogVerdict::PresentStall);
    EXPECT_EQ(idle, 30001u);
}

// Invariant: the verdict latches, so the runtime reacts (log + abort) exactly once.
TEST(TelemetryWatchdog, VerdictLatches) {
    Watchdog wd(100);
    wd.Arm(0);
    EXPECT_EQ(wd.Evaluate(101), WatchdogVerdict::PresentStall);
    EXPECT_EQ(wd.Evaluate(5000), WatchdogVerdict::Ok);
}

// Invariant: a hung guest thread is detected while presents keep flowing, but only once
// the guest heartbeat has been wired (otherwise an unwired runtime would false-positive).
TEST(TelemetryWatchdog, GuestStallNeedsWiredHeartbeat) {
    Watchdog unwired(100);
    unwired.Arm(0);
    unwired.NotePresent(500);
    EXPECT_EQ(unwired.Evaluate(550), WatchdogVerdict::Ok);

    Watchdog wd(100);
    wd.Arm(0);
    wd.NoteGuestProgress(10);
    wd.NotePresent(500);  // presenter alive
    EXPECT_EQ(wd.Evaluate(111), WatchdogVerdict::GuestStall);
}

// Invariant: a synthetic stalled thread (progress stops, presents continue) trips with a
// bounded, deterministic number of evaluations, no real waiting involved.
TEST(TelemetryWatchdog, SyntheticStalledThreadTripsWithinBound) {
    Watchdog wd(kSoftlockMs);
    wd.Arm(0);
    wd.NoteGuestProgress(0);
    int tripAtSecond = -1;
    for (int s = 1; s <= 40; ++s) {
        const std::uint64_t now = static_cast<std::uint64_t>(s) * 1000;
        wd.NotePresent(now);  // healthy presenter, hung guest thread
        if (wd.Evaluate(now) == WatchdogVerdict::GuestStall) {
            tripAtSecond = s;
            break;
        }
    }
    EXPECT_EQ(tripAtSecond, 31);
}

// Invariant: underruns are reported as deltas, never re-reported, and no event is written
// while the FMV window is closed (A/V offset is only meaningful during FMV).
TEST(TelemetrySampler, UnderrunDeltasAndClosedFmvWindow) {
    MemorySink sink;
    Log log(sink);
    Sampler s(log);
    s.Sample({0, 20.0});
    EXPECT_TRUE(sink.lines.empty());
    s.Sample({3, 20.0});
    s.Sample({3, 20.0});
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.lines[0], "{\"ev\":\"audio.underrun\",\"n\":3}");
}

// Invariant: while the FMV window is open, av.offset = audio_latency - video_latency.
TEST(TelemetrySampler, AvOffsetDuringFmvWindow) {
    MemorySink sink;
    Log log(sink);
    Sampler s(log);
    s.SetVideoLatencyMs(50.0);
    s.SetFmvWindow(true);
    s.Sample({0, 20.0});
    ASSERT_EQ(sink.lines.size(), 2u);
    EXPECT_EQ(sink.lines[0], "{\"ev\":\"video_latency_ms\",\"ms\":50}");
    EXPECT_EQ(sink.lines[1], "{\"ev\":\"av.offset\",\"ms\":-30}");
}
}  // namespace
