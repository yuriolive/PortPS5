// GoogleTest coverage for the process-wide telemetry exports in libc
// (core/libs/prx/libc/src/Telemetry.cpp).
//
// The runtime is a process singleton, so a single test drives the whole
// lifecycle (each gtest case runs in its own process via gtest_discover_tests).
// It writes only to a temp directory and checks the on-disk JSONL, no game data.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "prx/libc/include/telemetry/TelemetryRuntime.hpp"

namespace {

std::vector<std::string> ReadLines(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

// Invariant: Start writes the run.start header, presents write frame records, and after
// Shutdown the log ends at run.end: later presents and events are no-ops (review finding,
// straggler threads must not append after the documented end marker).
TEST(TelemetryRuntime, LogEndsAtRunEndAfterShutdown) {
    const auto dir = std::filesystem::temp_directory_path() / "portps5_telemetry_runtime_test";
    std::filesystem::remove_all(dir);
    ASSERT_TRUE(PortPS5_Telemetry_Start_nid_no_patch(dir.string().c_str(), 1920, 1080, 1, "none"));

    PortPS5_Telemetry_NotePresent_nid_no_patch();  // baseline, writes nothing
    PortPS5_Telemetry_NotePresent_nid_no_patch();  // first interval
    PortPS5_Telemetry_Event_nid_no_patch("dialog.open", nullptr, 0);
    PortPS5_Telemetry_Shutdown_nid_no_patch(3, 7);

    PortPS5_Telemetry_NotePresent_nid_no_patch();  // after Shutdown: dropped
    PortPS5_Telemetry_NotePresent_nid_no_patch();
    PortPS5_Telemetry_Event_nid_no_patch("dialog.open", nullptr, 0);

    const auto lines = ReadLines(dir / "logs" / "telemetry.jsonl");
    ASSERT_GE(lines.size(), 3u);
    EXPECT_NE(lines.front().find("\"ev\":\"run.start\""), std::string::npos);
    EXPECT_NE(lines.back().find("\"ev\":\"run.end\""), std::string::npos);
    EXPECT_NE(lines.back().find("\"capture_split\":3"), std::string::npos);
    EXPECT_NE(lines.back().find("\"write_faults\":7"), std::string::npos);
    std::filesystem::remove_all(dir);
}
}  // namespace
