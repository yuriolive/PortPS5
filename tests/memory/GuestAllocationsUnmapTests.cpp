// tests/memory/GuestAllocationsUnmapTests.cpp
// Verifies GuestAllocations::Mutation::Unmap with FreeBSD munmap semantics:
// one request may span several registered allocations and unregistered gaps.
// Pieces are reported to the callback in address order, holes left by earlier
// partial unmaps are skipped, a request that touches non-releasable image
// memory unmaps nothing, and a pinned piece blocks the whole request.
//
// The registry stores only addresses, so the tests register synthetic
// ranges at fixed high addresses without any host mapping behind them. Each
// test cleans up the ranges it created (gtest_discover_tests also runs each
// test in its own process).

#include "common/TestHarness.hpp"
#include "prx/libc/include/GuestAllocations.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uintptr_t kBase = 0x300000000000ull;  // synthetic guest address, never dereferenced
constexpr std::size_t kPage = 16 * 1024;

// One callback invocation recorded by Unmap.
struct Piece {
    std::uintptr_t address;
    std::size_t bytes;
    std::uintptr_t allocation;
    bool last;
    bool operator==(const Piece&) const = default;
};

void* Ptr(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

// Runs Unmap and returns every reported piece.
std::vector<Piece> UnmapRecording(GuestAllocations::Mutation& mutation, std::uintptr_t address, std::size_t bytes) {
    std::vector<Piece> pieces;
    mutation.Unmap(Ptr(address), bytes, [&](const void* piece, std::size_t pieceBytes, const void* allocation, bool last) {
        pieces.push_back({reinterpret_cast<std::uintptr_t>(piece), pieceBytes, reinterpret_cast<std::uintptr_t>(allocation), last});
    });
    return pieces;
}

bool Registered(std::uintptr_t address) {
    GuestAllocations::Mutation mutation;
    try {
        mutation.Find(Ptr(address));
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

// Invariant: a range covering two adjacent allocations unmaps both, one piece
// each, each marked last because nothing of either allocation remains. The old
// code threw "unmap crosses allocation boundaries" for this.
TEST(GuestAllocationsUnmap, SpansAdjacentAllocations) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase), 2 * kPage, true, true);
    mutation.Add(Ptr(kBase + 2 * kPage), 3 * kPage, true, false);
    const auto pieces = UnmapRecording(mutation, kBase, 5 * kPage);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0], (Piece{kBase, 2 * kPage, kBase, true}));
    EXPECT_EQ(pieces[1], (Piece{kBase + 2 * kPage, 3 * kPage, kBase + 2 * kPage, true}));
    EXPECT_TRUE(GuestAllocations::GuestAllocationsAcquire_nid_postfix().empty());
}

// Invariant: unregistered gaps inside the request (and before/after it) are
// ignored, as on FreeBSD; only registered pieces reach the callback.
TEST(GuestAllocationsUnmap, SkipsUnregisteredGaps) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase + kPage), kPage, true, true);
    mutation.Add(Ptr(kBase + 4 * kPage), kPage, true, true);
    const auto pieces = UnmapRecording(mutation, kBase, 6 * kPage);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].address, kBase + kPage);
    EXPECT_EQ(pieces[1].address, kBase + 4 * kPage);
    EXPECT_TRUE(GuestAllocations::GuestAllocationsAcquire_nid_postfix().empty());
}

// Invariant: a hole left inside an allocation by an earlier partial unmap does
// not make a later spanning unmap fail. The remaining fragments are reported
// separately; only the final one is `last`. An implementation that derives
// pieces from the allocation extent instead of the registered fragments
// throws on the hole.
TEST(GuestAllocationsUnmap, SkipsHoleInsideAllocation) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase), 4 * kPage, true, true);
    // Punch a hole in the middle: fragments [0,1) and [2,4) stay.
    const auto hole = UnmapRecording(mutation, kBase + kPage, kPage);
    ASSERT_EQ(hole.size(), 1u);
    EXPECT_FALSE(hole[0].last);
    const auto rest = UnmapRecording(mutation, kBase, 4 * kPage);
    ASSERT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0], (Piece{kBase, kPage, kBase, false}));
    EXPECT_EQ(rest[1], (Piece{kBase + 2 * kPage, 2 * kPage, kBase, true}));
    EXPECT_TRUE(GuestAllocations::GuestAllocationsAcquire_nid_postfix().empty());
}

// Invariant: a request may start or end in the middle of allocations; the
// untouched remainders stay registered with their original allocation base.
TEST(GuestAllocationsUnmap, PartialCoverageOfBothEnds) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase), 3 * kPage, true, true);
    mutation.Add(Ptr(kBase + 3 * kPage), 3 * kPage, true, true);
    const auto pieces = UnmapRecording(mutation, kBase + 2 * kPage, 2 * kPage);  // last page of A, first page of B
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0], (Piece{kBase + 2 * kPage, kPage, kBase, false}));
    EXPECT_EQ(pieces[1], (Piece{kBase + 3 * kPage, kPage, kBase + 3 * kPage, false}));
    EXPECT_TRUE(Registered(kBase));
    EXPECT_TRUE(Registered(kBase + 3 * kPage));
    // Clean up the remainders.
    EXPECT_EQ(UnmapRecording(mutation, kBase, 6 * kPage).size(), 2u);
}

// Invariant: a request with nothing registered in it still fails (this tree's
// existing contract; EINVAL at the SCE layer), and so does a wrapping request.
TEST(GuestAllocationsUnmap, NothingRegisteredOrWrappingThrows) {
    GuestAllocations::Mutation mutation;
    EXPECT_THROW(UnmapRecording(mutation, kBase, kPage), std::runtime_error);
    EXPECT_THROW(UnmapRecording(mutation, 0xFFFFFFFFFFFFF000ull, 0x2000), std::runtime_error);
    EXPECT_THROW(UnmapRecording(mutation, kBase, 0), std::runtime_error);
}

// Invariant: a request that reaches non-releasable main-image memory fails
// before any host unmap: the releasable allocation next to it must be
// untouched and the callback never called.
TEST(GuestAllocationsUnmap, ImagePieceRejectsWholeRequestBeforeApplying) {
    GuestAllocations::Mutation mutation;
    mutation.RegisterMainImage();
    static const int imageProbe = 0;  // lives in the main image's data segment
    const auto imageAddress = reinterpret_cast<std::uintptr_t>(&imageProbe) & ~(kPage - 1);
    // Find the first registered image fragment at or below the probe's page; build the neighbour below it.
    std::uintptr_t imageStart = imageAddress;
    {
        // Scoped: a live lease would pin the fragments and make Unmap throw for the wrong reason.
        const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();
        for (const auto& range : lease) {
            if (range->address <= imageAddress && imageAddress < range->address + range->bytes) imageStart = range->address;
        }
    }
    const auto below = imageStart - 2 * kPage;  // may be unmapped on the host: the registry only records addresses
    mutation.Add(Ptr(below), 2 * kPage, true, true);
    int applied = 0;
    EXPECT_THROW(mutation.Unmap(Ptr(below), 3 * kPage, [&](const void*, std::size_t, const void*, bool) { ++applied; }), std::runtime_error);
    EXPECT_EQ(applied, 0);
    EXPECT_TRUE(Registered(below));
    EXPECT_EQ(UnmapRecording(mutation, below, 2 * kPage).size(), 1u);  // cleanup
}

// Invariant: if the host unmap of a later piece throws, earlier pieces are
// already gone from the registry (they were unmapped on the host) and the
// failing piece is still registered, so registry and host never disagree.
TEST(GuestAllocationsUnmap, FailingCallbackKeepsRegistryConsistent) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase), kPage, true, true);
    mutation.Add(Ptr(kBase + kPage), kPage, true, true);
    int calls = 0;
    EXPECT_THROW(mutation.Unmap(Ptr(kBase), 2 * kPage, [&](const void*, std::size_t, const void*, bool) {
        if (++calls == 2) throw std::runtime_error("host unmap failed");
    }), std::runtime_error);
    EXPECT_FALSE(Registered(kBase));
    EXPECT_TRUE(Registered(kBase + kPage));
    EXPECT_EQ(UnmapRecording(mutation, kBase + kPage, kPage).size(), 1u);  // cleanup
}

// Invariant: a GPU lease on any overlapped allocation blocks the whole request
// before the callback runs, so a spanning unmap never frees pinned memory.
TEST(GuestAllocationsUnmap, PinnedPieceBlocksWholeRequest) {
    GuestAllocations::Mutation mutation;
    mutation.Add(Ptr(kBase), kPage, true, true);
    mutation.Add(Ptr(kBase + kPage), kPage, true, true);
    {
        const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();  // holds both entries
        int applied = 0;
        EXPECT_THROW(mutation.Unmap(Ptr(kBase), 2 * kPage, [&](const void*, std::size_t, const void*, bool) { ++applied; }), std::runtime_error);
        EXPECT_EQ(applied, 0);
    }
    EXPECT_EQ(UnmapRecording(mutation, kBase, 2 * kPage).size(), 2u);  // lease gone: now allowed
}

}  // namespace
