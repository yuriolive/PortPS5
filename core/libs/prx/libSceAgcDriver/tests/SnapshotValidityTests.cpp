// core/libs/prx/libSceAgcDriver/tests/SnapshotValidityTests.cpp
//
// GoogleTest suite for Graphics/SnapshotValidity.hpp. A false "still valid" shows stale textures, so each test pins
// one rule of the IWriteTracker contract: equal nonzero generation skips the compare, anything else compares bytes.
// Hermetic: a counting fake tracker and plain host buffers, no GPU and no guest arena.
#include "prx/libSceAgcDriver/Graphics/include/SnapshotValidity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;
using PortPS5::GuestMemory::IWriteTracker;

// Tracker whose generation the test controls; counts Collect calls.
class CountingTracker final : public IWriteTracker {
public:
    std::uint64_t generation = 0;
    int collects = 0;
    std::uint64_t Collect(std::uint64_t, std::uint64_t) override { ++collects; return generation; }
    PortPS5::GuestMemory::PinToken Pin(std::span<const PortPS5::GuestMemory::AddrRange>) override { return {1}; }
    void Unpin(PortPS5::GuestMemory::PinToken) noexcept override {}
    PortPS5::GuestMemory::PageState PageStateAt(std::uint64_t) const override { return PortPS5::GuestMemory::PageState::ReadWrite; }
    std::uint64_t MarkWritten(std::uint64_t, std::uint64_t) override { return ++generation; }
    void SetFlushHook(PortPS5::GuestMemory::FlushHook, void*) override {}
};

std::uint64_t addressOf(const std::vector<std::byte>& bytes) { return reinterpret_cast<std::uint64_t>(bytes.data()); }

// Invariant: without a tracker the result is exactly a byte compare (pre-existing behaviour), and a changed byte is caught.
TEST(SnapshotValidity, NoTrackerComparesBytes) {
    std::vector<std::byte> live(256, std::byte{7});
    const auto snapshot = live;
    std::uint64_t stamp = 0;
    EXPECT_TRUE(SnapshotStillValid(nullptr, addressOf(live), snapshot, stamp));
    live[100] = std::byte{8};
    EXPECT_FALSE(SnapshotStillValid(nullptr, addressOf(live), snapshot, stamp));
}

// Invariant: a matching nonzero generation skips the compare. The live bytes are deliberately different, so a
// compare would return false; "true" proves the compare was skipped. Fails if the generation shortcut is removed.
TEST(SnapshotValidity, MatchingNonzeroGenerationSkipsCompare) {
    CountingTracker tracker;
    tracker.generation = 5;
    std::vector<std::byte> live(256, std::byte{1});
    const std::vector<std::byte> snapshot(256, std::byte{2});
    std::uint64_t stamp = 5;
    EXPECT_TRUE(SnapshotStillValid(&tracker, addressOf(live), snapshot, stamp));
}

// Invariant: a newer generation (CPU write or MarkWritten) forces a byte compare, so a real change is detected and
// the stamp is cleared. This is the "texture changed by the guest or by GPU write-back is re-read" requirement.
TEST(SnapshotValidity, NewerGenerationComparesAndDetectsChange) {
    CountingTracker tracker;
    tracker.generation = 5;
    std::vector<std::byte> live(256, std::byte{1});
    const auto snapshot = live;
    std::uint64_t stamp = 5;
    live[10] = std::byte{9};
    tracker.MarkWritten(addressOf(live), live.size());
    EXPECT_FALSE(SnapshotStillValid(&tracker, addressOf(live), snapshot, stamp));
    EXPECT_EQ(stamp, 0u);
}

// Invariant: a newer generation with identical bytes (a rewrite of the same data) stays valid and adopts the new
// generation, so later draws skip the compare again.
TEST(SnapshotValidity, NewerGenerationWithEqualBytesRefreshesStamp) {
    CountingTracker tracker;
    tracker.generation = 5;
    std::vector<std::byte> live(256, std::byte{1});
    const auto snapshot = live;
    std::uint64_t stamp = 5;
    tracker.MarkWritten(addressOf(live), live.size());
    EXPECT_TRUE(SnapshotStillValid(&tracker, addressOf(live), snapshot, stamp));
    EXPECT_EQ(stamp, tracker.generation);
}

// Invariant: generation 0 means "unknown" and must never be treated as a match, even when the stored stamp is also 0
// (a never-stamped snapshot). The bytes differ, so a skipped compare would wrongly return true.
TEST(SnapshotValidity, ZeroGenerationIsUnknownAndAlwaysCompares) {
    CountingTracker tracker;
    tracker.generation = 0;
    std::vector<std::byte> live(256, std::byte{1});
    const std::vector<std::byte> snapshot(256, std::byte{2});
    std::uint64_t stamp = 0;
    EXPECT_FALSE(SnapshotStillValid(&tracker, addressOf(live), snapshot, stamp));
}

// Invariant: StampSnapshot reports 0 without a tracker and the tracker's generation with one.
TEST(SnapshotValidity, StampSnapshotReadsTracker) {
    CountingTracker tracker;
    tracker.generation = 9;
    EXPECT_EQ(StampSnapshot(nullptr, 0x1000, 64), 0u);
    EXPECT_EQ(StampSnapshot(&tracker, 0x1000, 64), 9u);
    EXPECT_EQ(tracker.collects, 1);
}

}
