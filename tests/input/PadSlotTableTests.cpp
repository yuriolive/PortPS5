// Unit tests: controller hot-plug slot assignment (docs/spec/input.md §Target design 3).
//
// Drives Pad::SlotTable with synthetic SDL instance ids and device GUIDs through
// plug and unplug sequences: lowest-free-slot assignment, same-GUID slot reclaim,
// duplicate and overflow handling and detach of unknown ids. No SDL device or
// event injection is needed because the policy is a pure data structure.
#include "prx/libScePad/include/PadSlotTable.hpp"

#include <gtest/gtest.h>

namespace {

Pad::DeviceGuid Guid(std::uint8_t tag) {
    Pad::DeviceGuid g{};
    g.fill(tag);
    return g;
}

} // namespace

// Invariant: with nothing remembered, controllers fill slots 0..3 in order, so
// the first pad is always the one single-player titles read.
TEST(PadSlotTable, FillsLowestFreeSlotFirst) {
    Pad::SlotTable t;
    for (std::uint8_t i = 0; i < 4; ++i) {
        const auto r = t.Attach(100 + i, Guid(i));
        ASSERT_EQ(r.status, Pad::AttachStatus::Assigned);
        EXPECT_EQ(r.slot, i);
    }
}

// Invariant: a fifth simultaneous controller is refused (Full), and refusing it
// leaves the four attached devices untouched.
TEST(PadSlotTable, FifthControllerIsRefused) {
    Pad::SlotTable t;
    for (std::uint8_t i = 0; i < 4; ++i) t.Attach(i, Guid(i));
    EXPECT_EQ(t.Attach(99, Guid(9)).status, Pad::AttachStatus::Full);
    for (std::uint8_t i = 0; i < 4; ++i) EXPECT_EQ(t.SlotOf(i), i);
}

// Invariant: attaching the same instance id twice reports Duplicate and does not
// consume a second slot (SDL can deliver an added event for an open pad).
TEST(PadSlotTable, DuplicateInstanceDoesNotTakeASecondSlot) {
    Pad::SlotTable t;
    ASSERT_EQ(t.Attach(7, Guid(1)).status, Pad::AttachStatus::Assigned);
    EXPECT_EQ(t.Attach(7, Guid(1)).status, Pad::AttachStatus::Duplicate);
    const auto next = t.Attach(8, Guid(2));
    ASSERT_EQ(next.status, Pad::AttachStatus::Assigned);
    EXPECT_EQ(next.slot, 1u);
}

// Invariant: a device that is unplugged and plugged back in (new instance id,
// same GUID) returns to the slot it held even when a lower slot is free.
TEST(PadSlotTable, ReconnectReclaimsSameSlot) {
    Pad::SlotTable t;
    t.Attach(1, Guid(0xA));  // slot 0
    t.Attach(2, Guid(0xB));  // slot 1
    EXPECT_EQ(t.Detach(1), 0u);
    EXPECT_EQ(t.Detach(2), 1u);
    const auto b = t.Attach(3, Guid(0xB));
    ASSERT_EQ(b.status, Pad::AttachStatus::Assigned);
    EXPECT_EQ(b.slot, 1u);  // not the lowest free slot (0)
    const auto a = t.Attach(4, Guid(0xA));
    EXPECT_EQ(a.slot, 0u);
}

// Invariant: a different device never waits behind a reservation: it takes the
// lowest free slot, so a replacement pad lands in slot 0 for single-player titles.
TEST(PadSlotTable, DifferentDeviceTakesLowestFreeSlot) {
    Pad::SlotTable t;
    t.Attach(1, Guid(0xA));
    t.Detach(1);
    const auto other = t.Attach(2, Guid(0xC));
    ASSERT_EQ(other.status, Pad::AttachStatus::Assigned);
    EXPECT_EQ(other.slot, 0u);
    // The original device now finds its slot taken and falls back to the lowest free slot.
    const auto original = t.Attach(3, Guid(0xA));
    EXPECT_EQ(original.slot, 1u);
}

// Invariant: Detach of an id that is not attached (or already detached) is a
// harmless no-op that reports kNone and frees nothing.
TEST(PadSlotTable, DetachUnknownIsNoOp) {
    Pad::SlotTable t;
    t.Attach(5, Guid(1));
    EXPECT_EQ(t.Detach(99), Pad::SlotTable::kNone);
    EXPECT_EQ(t.Detach(5), 0u);
    EXPECT_EQ(t.Detach(5), Pad::SlotTable::kNone);
    EXPECT_EQ(t.SlotOf(5), Pad::SlotTable::kNone);
}

// Invariant: after a freed middle slot, the next new device fills the gap, and a
// controller removed from a full table makes room for exactly one more.
TEST(PadSlotTable, FullTableRecoversAfterDetach) {
    Pad::SlotTable t;
    for (std::uint8_t i = 0; i < 4; ++i) t.Attach(10 + i, Guid(i));
    EXPECT_EQ(t.Detach(11), 1u);
    const auto r = t.Attach(50, Guid(0x77));
    ASSERT_EQ(r.status, Pad::AttachStatus::Assigned);
    EXPECT_EQ(r.slot, 1u);
    EXPECT_EQ(t.Attach(51, Guid(0x78)).status, Pad::AttachStatus::Full);
}
