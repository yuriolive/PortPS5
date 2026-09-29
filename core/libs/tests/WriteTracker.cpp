// core/libs/tests/WriteTracker.cpp
// GoogleTest suite for the IWriteTracker contract and its implementations
// (core/libs/prx/libc/include/WriteTracker.hpp,
// core/libs/prx/libc/src/WriteTracker.cpp,
// docs/spec/guest-memory.md, Target design "Write tracking" and
// "Page-state table").
//
// Strategy: pin the interface contract on NullTracker (unknown everywhere),
// then verify WriteWatchTracker behavior in three layers that need no guest
// arena: (1) pure-metadata paths on synthetic windows (MarkWritten novelty,
// pins, page-state table, unknown/out-of-window Collect); (2) real
// write-watch memory on Windows (VirtualAlloc MEM_WRITE_WATCH: dirtied pages
// stamp generations, clean re-collects prove unchanged); (3) the flush hook
// fires once per Collect with the exact range and no tracker lock held.

#include "prx/libc/include/WriteTracker.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Synthetic window far from any real mapping: metadata-only paths
// (MarkWritten, pins, page table, unknown Collect) never touch memory, so
// they are safe anywhere, including Linux CI.
constexpr std::uint64_t kSyntheticBase = 0x10'0000'0000ULL;
constexpr std::uint64_t kSyntheticBytes = 3ULL * 1073741824ULL;  // 3 GiB: spans shards

using namespace PortPS5::GuestMemory;

static_assert(std::is_trivially_copyable_v<PinToken>);
static_assert(sizeof(PageState) == 1);

// NullTracker reports unknown for everything, so the driver falls back to
// byte compares: Collect 0, inert pins, NotGuest pages, MarkWritten 0.
TEST(WriteTracker, NullTrackerContract) {
    NullTracker tracker;
    IWriteTracker& iface = tracker;
    EXPECT_EQ(iface.Collect(0x100000000ULL, 65536), 0U);
    const AddrRange ranges[] = {{0x100000000ULL, 4096}, {0x200000000ULL, 8192}};
    const PinToken token = iface.Pin(ranges);
    EXPECT_EQ(token, PinToken{});
    iface.Unpin(token);
    EXPECT_EQ(iface.PageStateAt(0x100000000ULL), PageState::NotGuest);
    EXPECT_EQ(iface.MarkWritten(0x100000000ULL, 4096), 0U);
    iface.SetFlushHook(nullptr, nullptr);
    EXPECT_EQ(iface.Collect(0x100000000ULL, 65536), 0U);
}

// PinToken crosses the tracker/driver boundary by value: equality is
// identity, and the null token is distinct from minted ones.
TEST(WriteTracker, PinTokenIdentity) {
    EXPECT_EQ(PinToken{}, PinToken{});
    EXPECT_NE(PinToken{1}, PinToken{2});
}

// Init rejects empty and wrapping windows without touching global state;
// a tracker that never initialized still reports unknown, never crashes.
TEST(WriteTracker, InitRejectsBadWindows) {
    WriteWatchTracker tracker;
    EXPECT_FALSE(tracker.Init(kSyntheticBase, 0));
    EXPECT_FALSE(tracker.Init(UINT64_MAX, 2));
    EXPECT_EQ(tracker.Collect(kSyntheticBase, 4096), 0U);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase), PageState::NotGuest);
    EXPECT_EQ(tracker.UnknownWalks(), 1U);
}

// MarkWritten bumps novelty on a synthetic window with no real memory: the
// first report is nonzero, disjoint ranges advance independently, and
// re-marking advances again (shared per-shard monotonic counter).
TEST(WriteTracker, MarkWrittenAdvancesGenerations) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    const std::uint64_t first = tracker.MarkWritten(kSyntheticBase, 65536);
    EXPECT_NE(first, 0U);
    const std::uint64_t second = tracker.MarkWritten(kSyntheticBase + 0x40000000ULL, 65536);
    EXPECT_GT(second, first);
    const std::uint64_t third = tracker.MarkWritten(kSyntheticBase, 65536);
    EXPECT_GT(third, second);
}

// MarkWritten outside the window (and empty/wrapping ranges) reports 0
// without touching shard state: GPU reports for never-tracked memory must
// not fabricate novelty.
TEST(WriteTracker, MarkWrittenRejectsUntrackedRanges) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    EXPECT_EQ(tracker.MarkWritten(kSyntheticBase - 4096, 4096), 0U);
    EXPECT_EQ(tracker.MarkWritten(kSyntheticBase + kSyntheticBytes, 4096), 0U);
    EXPECT_EQ(tracker.MarkWritten(kSyntheticBase, 0), 0U);
    EXPECT_EQ(tracker.MarkWritten(UINT64_MAX, 2), 0U);
}

// Collect over synthetic (unmapped) addresses cannot run GetWriteWatch, so
// it reports unknown and counts the walk: the driver compares bytes instead
// of trusting a generation. No memory is touched, so this is safe on a
// window nobody committed.
TEST(WriteTracker, CollectUnknownOnUnmappedWindow) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    EXPECT_EQ(tracker.Collect(kSyntheticBase, 65536), 0U);
    EXPECT_EQ(tracker.Collect(kSyntheticBase - 4096, 4096), 0U);
    EXPECT_EQ(tracker.Collect(kSyntheticBase, 0), 0U);
    EXPECT_EQ(tracker.UnknownWalks(), 3U);
}

// Pins balance by token: distinct Pins mint distinct tokens, Unpin releases
// exactly once, and the null token is a no-op (NullTracker parity).
TEST(WriteTracker, PinUnpinBalance) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    const AddrRange ranges[] = {{kSyntheticBase, 4096}};
    const PinToken first = tracker.Pin(ranges);
    const PinToken second = tracker.Pin(ranges);
    EXPECT_NE(first, PinToken{});
    EXPECT_NE(second, PinToken{});
    EXPECT_NE(first, second);
    tracker.Unpin(first);
    tracker.Unpin(second);
    tracker.Unpin(PinToken{});
}

// Releasing an unknown or already-released token aborts: pin corruption
// would retire GPU work while still referenced, so it fails loudly.
TEST(WriteTracker, DoubleUnpinAborts) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    const AddrRange ranges[] = {{kSyntheticBase, 4096}};
    const PinToken token = tracker.Pin(ranges);
    tracker.Unpin(token);
    EXPECT_DEATH(tracker.Unpin(token), ".*");
}

// The flush hook fires once per Collect with the exact range, letting the
// driver land pending GPU writes before the CPU-side pass runs.
TEST(WriteTracker, FlushHookFiresPerCollect) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    struct Capture {
        unsigned calls = 0;
        std::uint64_t address = 0;
        std::uint64_t bytes = 0;
    } capture;
    tracker.SetFlushHook(
        [](void* context, std::uint64_t address, std::uint64_t bytes) {
            auto* out = static_cast<Capture*>(context);
            ++out->calls;
            out->address = address;
            out->bytes = bytes;
        },
        &capture);
    tracker.Collect(kSyntheticBase + 4096, 8192);
    EXPECT_EQ(capture.calls, 1U);
    EXPECT_EQ(capture.address, kSyntheticBase + 4096);
    EXPECT_EQ(capture.bytes, 8192U);
    tracker.SetFlushHook(nullptr, nullptr);
    tracker.Collect(kSyntheticBase + 4096, 8192);
    EXPECT_EQ(capture.calls, 1U);
}

// Unbound page table: in-window reads are Uncommitted (conservative — wait
// or compare), out-of-window reads are NotGuest.
TEST(WriteTracker, UnboundPageStateReads) {
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes));
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase), PageState::Uncommitted);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase + kSyntheticBytes - 1), PageState::Uncommitted);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase - 1), PageState::NotGuest);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase + kSyntheticBytes), PageState::NotGuest);
}

// Bound page table delegates: Updated states show through PageStateAt,
// including across the 1 GiB shard boundary the synthetic window spans.
TEST(WriteTracker, BoundPageStateDelegates) {
    PageStateTable table;
    ASSERT_TRUE(table.Init(kSyntheticBase, kSyntheticBytes));
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(kSyntheticBase, kSyntheticBytes, &table));
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase), PageState::Uncommitted);
    table.Update(kSyntheticBase, 8192, PageState::ReadWrite);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase), PageState::ReadWrite);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase + 4096), PageState::ReadWrite);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase + 8192), PageState::Uncommitted);
    const std::uint64_t farAddr = kSyntheticBase + 2ULL * 1073741824ULL;
    table.Update(farAddr, 4096, PageState::ReadOnly);
    EXPECT_EQ(tracker.PageStateAt(farAddr), PageState::ReadOnly);
    EXPECT_EQ(tracker.PageStateAt(kSyntheticBase - 4096), PageState::NotGuest);
}

// Page-state table stores per-4 KiB states, clamps sub-page updates to the
// containing pages, and starts every fresh shard Uncommitted (never stale).
TEST(WriteTracker, PageStateTableBasics) {
    PageStateTable table;
    EXPECT_FALSE(table.Init(kSyntheticBase, 0));
    ASSERT_TRUE(table.Init(kSyntheticBase, kSyntheticBytes));
    EXPECT_EQ(table.At(kSyntheticBase), PageState::Uncommitted);
    // Sub-page update covers the whole containing page (conservative).
    table.Update(kSyntheticBase + 100, 100, PageState::ReadOnly);
    EXPECT_EQ(table.At(kSyntheticBase), PageState::ReadOnly);
    EXPECT_EQ(table.At(kSyntheticBase + 4096), PageState::Uncommitted);
    // Empty update is a no-op, not an abort.
    table.Update(kSyntheticBase, 0, PageState::ReadWrite);
    EXPECT_EQ(table.At(kSyntheticBase), PageState::ReadOnly);
    EXPECT_EQ(table.At(kSyntheticBase - 1), PageState::NotGuest);
    EXPECT_EQ(table.At(kSyntheticBase + kSyntheticBytes), PageState::NotGuest);
}

#ifdef _WIN32
// Real write-watch memory: dirtied 4 KiB pages stamp their 64 KiB block, a
// clean re-collect returns the same generation (unchanged is provable), and
// new dirt advances it. This is the core Collect contract from the spec.
TEST(WriteTracker, CollectTracksRealWrites) {
    constexpr std::size_t kBytes = 262144;  // 4 blocks of 64 KiB
    void* memory = VirtualAlloc(nullptr, kBytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    ASSERT_NE(memory, nullptr);
    const std::uint64_t base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory));
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(base, kBytes));
    static_cast<volatile char*>(memory)[0] = 1;
    static_cast<volatile char*>(memory)[131072] = 2;
    const std::uint64_t dirty = tracker.Collect(base, kBytes);
    EXPECT_NE(dirty, 0U);
    // Clean re-collect: same generation, no new stamps (unchanged provable).
    EXPECT_EQ(tracker.Collect(base, kBytes), dirty);
    // Fresh dirt in a new block advances the newest generation.
    static_cast<volatile char*>(memory)[200000] = 3;
    EXPECT_GT(tracker.Collect(base, kBytes), dirty);
    EXPECT_TRUE(VirtualFree(memory, 0, MEM_RELEASE));
}

// Plain (non-write-watch) heap memory cannot be walked: Collect reports
// unknown instead of fabricating generations from unreadable state.
TEST(WriteTracker, CollectUnknownOnPlainMemory) {
    std::vector<char> buffer(65536, 0);
    const std::uint64_t base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(buffer.data()));
    WriteWatchTracker tracker;
    ASSERT_TRUE(tracker.Init(base, static_cast<std::uint64_t>(buffer.size())));
    buffer[0] = 1;
    EXPECT_EQ(tracker.Collect(base, static_cast<std::uint64_t>(buffer.size())), 0U);
    EXPECT_EQ(tracker.UnknownWalks(), 1U);
}
#endif

}  // namespace
