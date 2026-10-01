// tests/memory/MemoryMappingTests.cpp
// End-to-end checks of sceKernelMapFlexibleMemory / sceKernelMunmap placement
// semantics (FreeBSD mmap/munmap): an address without MAP_FIXED is only a
// hint, NO_OVERWRITE (0x80) is accepted with MAP_FIXED and never replaces an
// existing mapping, and munmap may cover several mappings, holes and gaps.
//
// Mappings are real host memory obtained through the exports, so the tests
// run wherever libkernel runs. Neighbouring mappings are placed with fixed
// no-overwrite requests inside a window that was just freed.

#include "common/TestHarness.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kProtReadWrite = 3;
constexpr int kMapFixed = 0x10;
constexpr int kMapNoOverwrite = 0x80;
constexpr size_t kPage = PS5_PAGE_SIZE;

void* MapFlexible(void* hint, size_t bytes, int flags, int* result = nullptr) {
    void* addr = hint;
    const int ret = sceKernelMapFlexibleMemory(&addr, bytes, kProtReadWrite, flags);
    if (result) *result = ret;
    return ret == 0 ? addr : nullptr;
}

bool IsRegistered(void* addr) {
    GuestAllocations::Mutation mutation;
    try {
        mutation.Find(addr);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int Unmap(void* addr, size_t bytes) { return sceKernelMunmap(reinterpret_cast<uint64_t>(addr), bytes); }

// Invariant: a hint that is already mapped is not an error; the mapping is
// placed somewhere else. The old code answered EINVAL for any non-null
// address without MAP_FIXED.
TEST(MemoryMapping, OccupiedHintFallsBackToAnotherAddress) {
    void* first = MapFlexible(nullptr, 4 * kPage, 0);
    ASSERT_NE(first, nullptr);
    int ret = -1;
    void* second = MapFlexible(first, 4 * kPage, 0, &ret);
    ASSERT_EQ(ret, 0);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(second) & (kPage - 1), 0u);
    EXPECT_EQ(Unmap(second, 4 * kPage), 0);
    EXPECT_EQ(Unmap(first, 4 * kPage), 0);
}

// Invariant: a free, aligned hint is honoured exactly.
TEST(MemoryMapping, FreeHintIsHonoured) {
    void* probe = MapFlexible(nullptr, 2 * kPage, 0);
    ASSERT_NE(probe, nullptr);
    ASSERT_EQ(Unmap(probe, 2 * kPage), 0);  // now free
    void* hinted = MapFlexible(probe, 2 * kPage, 0);
    ASSERT_NE(hinted, nullptr);
    EXPECT_EQ(hinted, probe);
    EXPECT_EQ(Unmap(hinted, 2 * kPage), 0);
}

// Invariant: a misaligned hint is advisory too; it is ignored, not rejected.
TEST(MemoryMapping, MisalignedHintIsIgnored) {
    void* first = MapFlexible(nullptr, kPage, 0);
    ASSERT_NE(first, nullptr);
    int ret = -1;
    void* second = MapFlexible(static_cast<char*>(first) + 1, kPage, 0, &ret);
    ASSERT_EQ(ret, 0);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(second) & (kPage - 1), 0u);
    EXPECT_EQ(Unmap(second, kPage), 0);
    EXPECT_EQ(Unmap(first, kPage), 0);
}

// Invariant: FIXED|NO_OVERWRITE on a free address maps exactly there. The old
// code rejected the 0x80 bit as an unsupported flag.
TEST(MemoryMapping, FixedNoOverwriteOnFreeAddress) {
    void* probe = MapFlexible(nullptr, 2 * kPage, 0);
    ASSERT_NE(probe, nullptr);
    ASSERT_EQ(Unmap(probe, 2 * kPage), 0);
    int ret = -1;
    void* mapped = MapFlexible(probe, 2 * kPage, kMapFixed | kMapNoOverwrite, &ret);
    ASSERT_EQ(ret, 0);
    EXPECT_EQ(mapped, probe);
    EXPECT_EQ(Unmap(mapped, 2 * kPage), 0);
}

// Invariant: FIXED|NO_OVERWRITE over an existing mapping fails and leaves that
// mapping, and the data in it, untouched.
TEST(MemoryMapping, FixedNoOverwriteNeverReplaces) {
    auto* existing = static_cast<unsigned char*>(MapFlexible(nullptr, 2 * kPage, 0));
    ASSERT_NE(existing, nullptr);
    std::memset(existing, 0xA5, 2 * kPage);
    int ret = 0;
    EXPECT_EQ(MapFlexible(existing, 2 * kPage, kMapFixed | kMapNoOverwrite, &ret), nullptr);
    EXPECT_NE(ret, 0);
    EXPECT_TRUE(IsRegistered(existing));
    EXPECT_EQ(existing[0], 0xA5);
    EXPECT_EQ(existing[2 * kPage - 1], 0xA5);
    EXPECT_EQ(Unmap(existing, 2 * kPage), 0);
}

// Invariant: flag bits we do not understand are still rejected.
TEST(MemoryMapping, UnknownFlagBitsAreRejected) {
    int ret = 0;
    EXPECT_EQ(MapFlexible(nullptr, kPage, 0x2, &ret), nullptr);
    EXPECT_EQ(ret, ::SCE_KERNEL_ERROR_EINVAL);
}

// Invariant: munmap over a hole left by an earlier partial munmap succeeds and
// removes both remaining fragments. The registry used to reject this because
// the request was not covered by one contiguous allocation.
TEST(MemoryMapping, MunmapAcrossAHoleInsideOneAllocation) {
    void* base = MapFlexible(nullptr, 4 * kPage, 0);
    ASSERT_NE(base, nullptr);
    ASSERT_EQ(Unmap(static_cast<char*>(base) + kPage, kPage), 0);  // punch a hole
    EXPECT_EQ(Unmap(base, 4 * kPage), 0);
    EXPECT_FALSE(IsRegistered(base));
    EXPECT_FALSE(IsRegistered(static_cast<char*>(base) + 2 * kPage));
}

// Two mappings at known offsets inside a window that was just freed, so adjacency
// and gaps are deterministic regardless of what the host placed around them.
struct Pair {
    char* window = nullptr;  // start of the freed window
    void* first = nullptr;   // at window + 4 pages
    void* second = nullptr;  // at first + firstBytes + gap
};

Pair PlacePair(size_t firstBytes, size_t gap, size_t secondBytes) {
    constexpr size_t windowBytes = 64 * kPage;
    void* probe = MapFlexible(nullptr, windowBytes, 0);
    Pair pair;
    if (probe == nullptr || Unmap(probe, windowBytes) != 0) return pair;
    pair.window = static_cast<char*>(probe);
    pair.first = MapFlexible(pair.window + 4 * kPage, firstBytes, kMapFixed | kMapNoOverwrite);
    if (pair.first == nullptr) return pair;
    pair.second = MapFlexible(static_cast<char*>(pair.first) + firstBytes + gap, secondBytes, kMapFixed | kMapNoOverwrite);
    return pair;
}

// Invariant: one munmap may cover two adjacent mappings (FreeBSD semantics).
TEST(MemoryMapping, MunmapSpanningTwoMappings) {
    const Pair pair = PlacePair(4 * kPage, 0, 4 * kPage);
    ASSERT_NE(pair.second, nullptr);
    EXPECT_EQ(Unmap(pair.first, 8 * kPage), 0);
    EXPECT_FALSE(IsRegistered(pair.first));
    EXPECT_FALSE(IsRegistered(pair.second));
}

// Invariant: an unmapped gap between two mappings inside the range is ignored.
TEST(MemoryMapping, MunmapSpanningAGapBetweenMappings) {
    const Pair pair = PlacePair(2 * kPage, 2 * kPage, 2 * kPage);  // [A][gap][B]
    ASSERT_NE(pair.second, nullptr);
    EXPECT_EQ(Unmap(pair.first, 6 * kPage), 0);
    EXPECT_FALSE(IsRegistered(pair.first));
    EXPECT_FALSE(IsRegistered(pair.second));
}

// Invariant: a range that starts and ends in the middle of two adjacent
// mappings unmaps only the covered part; the rest stays mapped and usable.
TEST(MemoryMapping, MunmapPartiallyCoveringBothNeighbours) {
    const Pair pair = PlacePair(4 * kPage, 0, 4 * kPage);
    ASSERT_NE(pair.second, nullptr);
    auto* keepLow = static_cast<unsigned char*>(pair.first);
    auto* keepHigh = static_cast<unsigned char*>(pair.second) + 3 * kPage;
    keepLow[0] = 0x11;
    keepHigh[0] = 0x22;
    EXPECT_EQ(Unmap(keepLow + 2 * kPage, 4 * kPage), 0);  // upper half of first, lower half of second
    EXPECT_EQ(keepLow[0], 0x11);
    EXPECT_EQ(keepHigh[0], 0x22);
    EXPECT_EQ(Unmap(keepLow, 2 * kPage), 0);
    EXPECT_EQ(Unmap(keepHigh - kPage, kPage), 0);
    EXPECT_EQ(Unmap(keepHigh, kPage), 0);
}

// Invariant: a range with nothing mapped in it is still EINVAL (this tree's
// contract), and a misaligned length is rejected before any lookup.
TEST(MemoryMapping, MunmapOfNothingIsRejected) {
    void* probe = MapFlexible(nullptr, kPage, 0);
    ASSERT_NE(probe, nullptr);
    ASSERT_EQ(Unmap(probe, kPage), 0);
    EXPECT_EQ(Unmap(probe, kPage), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(Unmap(probe, kPage + 1), ::SCE_KERNEL_ERROR_EINVAL);
}

}  // namespace
