// GoogleTest suite for the sceLibcMspace* allocator (core/libs/prx/libc/src/Mspace.cpp).
//
// Covers return codes and errno values for every export, allocator invariants (alignment, no overlap,
// coalescing, in-place growth, accounting) and a randomized stress run checked against a host-side model.
// No GPU and no game data needed: each test hands the allocator a plain host buffer as the "guest region".
//
// Converted from the former bare-main() test (AnyPS5 commits 3058980d, 02b431eb, 8f982168) to GoogleTest as
// required by .agents/rules/testing.md. Mspace state is process-global (handle == region base), so every
// fixture destroys its mspace in TearDown to keep tests order-independent.
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <thread>
#include <vector>

extern "C" {
// Declaration of `sceLibcMspaceCreate_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceCreate_nid_postfix(const char*, void*, std::size_t, unsigned);
// Declaration of `sceLibcMspaceDestroy_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sceLibcMspaceDestroy_nid_postfix(void*);
// Declaration of `sceLibcMspaceMalloc_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceMalloc_nid_postfix(void*, std::size_t);
// Declaration of `sceLibcMspaceCalloc_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceCalloc_nid_postfix(void*, std::size_t, std::size_t);
// Declaration of `sceLibcMspaceRealloc_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceRealloc_nid_postfix(void*, void*, std::size_t);
// Declaration of `sceLibcMspaceFree_nid_postfix`; its contract is documented at the definition.
void APS5_VABI sceLibcMspaceFree_nid_postfix(void*, void*);
// Declaration of `sceLibcMspacePosixMemalign_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sceLibcMspacePosixMemalign_nid_postfix(void*, void**, std::size_t, std::size_t);
// Declaration of `sceLibcMspaceMallocUsableSize_nid_postfix`; its contract is documented at the definition.
std::size_t APS5_VABI sceLibcMspaceMallocUsableSize_nid_postfix(const void*);
// Declaration of `sceLibcMspaceMemalign_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceMemalign_nid_postfix(void*, std::size_t, std::size_t);
// Declaration of `sceLibcMspaceReallocalign_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI sceLibcMspaceReallocalign_nid_postfix(void*, void*, std::size_t, std::size_t);
// Declaration of `sceLibcMspaceMallocStats_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sceLibcMspaceMallocStats_nid_postfix(void*, void*);
// Declaration of `sceLibcMspaceMallocStatsFast_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sceLibcMspaceMallocStatsFast_nid_postfix(void*, void*);
// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
int* APS5_VABI __error_nid_postfix();
}

namespace {

constexpr int Einval = 22;
constexpr int Enomem = 12;

// Mirror of the guest-visible stats struct (0x28 bytes).
struct MallocManagedSize {
    std::uint16_t size;
    std::uint16_t version;
    std::uint32_t reserved;
    std::size_t maxSystemSize;
    std::size_t currentSystemSize;
    std::size_t maxInuseSize;
    std::size_t currentInuseSize;
};

int GuestErrno() { return *__error_nid_postfix(); }

// Fixture owning a 64 KiB 16-byte-aligned region with a live mspace over it.
class MspaceTest : public ::testing::Test {
protected:
    static constexpr std::size_t RegionSize = 65536;

    void SetUp() override {
        storage.resize(RegionSize + 16);
        region = reinterpret_cast<void*>((reinterpret_cast<std::uintptr_t>(storage.data()) + 15) & ~std::uintptr_t{15});
        arena = sceLibcMspaceCreate_nid_postfix("test", region, RegionSize, 0);
        ASSERT_EQ(arena, region);
    }
    void TearDown() override { sceLibcMspaceDestroy_nid_postfix(arena); }

    std::vector<unsigned char> storage;
    void* region = nullptr;
    void* arena = nullptr;
};

}  // namespace

// Invariant: creating over the exact same region is rejected (EINVAL); so are bad arguments and unknown flags.
TEST_F(MspaceTest, CreateRejectsOverlapAndBadArguments) {
    *__error_nid_postfix() = 0;
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("overlap", region, RegionSize, 0), nullptr);
    EXPECT_EQ(GuestErrno(), Einval);
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("null", nullptr, RegionSize, 0), nullptr);
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("unaligned", static_cast<unsigned char*>(region) + 8, 4096, 0), nullptr);
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("tiny", static_cast<unsigned char*>(region) + RegionSize, 16, 0), nullptr);
    // Unknown flag bit 2 must be rejected; bit 1 (thread-unsafe) is accepted (see nested test).
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("flags", region, RegionSize, 2), nullptr);
    // A region that wraps the address space is rejected rather than overflowing `start + size`.
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("wrap", reinterpret_cast<void*>(~std::uintptr_t{15}), 4096, 0), nullptr);
}

// Invariant: destroy of an unknown handle returns -1/EINVAL; after destroy the handle no longer allocates and
// the same region can be reused for a fresh mspace.
TEST_F(MspaceTest, DestroyInvalidatesHandleAndAllowsReuse) {
    EXPECT_EQ(sceLibcMspaceDestroy_nid_postfix(static_cast<unsigned char*>(region) + 16), -1);
    EXPECT_EQ(GuestErrno(), Einval);
    EXPECT_EQ(sceLibcMspaceDestroy_nid_postfix(arena), 0);
    EXPECT_EQ(sceLibcMspaceMalloc_nid_postfix(arena, 8), nullptr);
    EXPECT_EQ(GuestErrno(), Einval);
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("reuse", region, RegionSize, 0), region);
}

// Invariant: calloc zeroes, realloc preserves contents when it must move, and a failed realloc leaves the
// original block untouched.
TEST_F(MspaceTest, CallocReallocPreserveContents) {
    auto* first = static_cast<unsigned char*>(sceLibcMspaceCalloc_nid_postfix(arena, 32, 4));
    ASSERT_NE(first, nullptr);
    EXPECT_GT(first, region);
    for (int i = 0; i < 128; ++i) { ASSERT_EQ(first[i], 0); first[i] = static_cast<unsigned char>(i); }
    void* blocker = sceLibcMspaceMalloc_nid_postfix(arena, 128);  // pins the neighbour so growth must move
    ASSERT_NE(blocker, nullptr);
    auto* grown = static_cast<unsigned char*>(sceLibcMspaceRealloc_nid_postfix(arena, first, 4096));
    ASSERT_NE(grown, nullptr);
    EXPECT_NE(grown, first);
    for (int i = 0; i < 128; ++i) ASSERT_EQ(grown[i], i);
    EXPECT_GE(sceLibcMspaceMallocUsableSize_nid_postfix(grown), 4096u);
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, grown, RegionSize), nullptr);
    EXPECT_EQ(GuestErrno(), Enomem);
    EXPECT_EQ(grown[127], 127);
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, grown, 0), nullptr);  // size 0 frees
    sceLibcMspaceFree_nid_postfix(arena, blocker);
    // Everything freed: one maximal allocation fits again (coalescing).
    void* large = sceLibcMspaceMalloc_nid_postfix(arena, RegionSize - 256);
    EXPECT_NE(large, nullptr);
}

// Invariant (overflow regression): calloc/malloc/realloc/reallocalign requests whose 16-byte rounding would wrap
// size_t must fail with ENOMEM. Before the fix, realloc(ptr, SIZE_MAX) rounded to 0, "fit" in the existing
// chunk and recorded a bogus SIZE_MAX request size.
TEST_F(MspaceTest, HugeRequestsFailInsteadOfWrapping) {
    constexpr auto Huge = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(sceLibcMspaceCalloc_nid_postfix(arena, Huge, 2), nullptr);
    EXPECT_EQ(GuestErrno(), Enomem);
    EXPECT_EQ(sceLibcMspaceMalloc_nid_postfix(arena, Huge), nullptr);
    EXPECT_EQ(GuestErrno(), Enomem);
    void* block = sceLibcMspaceMalloc_nid_postfix(arena, 64);
    ASSERT_NE(block, nullptr);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, block, Huge), nullptr);
    EXPECT_EQ(GuestErrno(), Enomem);
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, block, Huge - 8), nullptr);
    EXPECT_EQ(sceLibcMspaceReallocalign_nid_postfix(arena, block, Huge, 64), nullptr);
    EXPECT_EQ(sceLibcMspaceMemalign_nid_postfix(arena, Huge, 64), nullptr);  // huge alignment
    EXPECT_EQ(sceLibcMspaceMemalign_nid_postfix(arena, std::uintptr_t{1} << 62, 64), nullptr);
    // The original block must still be a live 64-byte allocation afterwards.
    EXPECT_GE(sceLibcMspaceMallocUsableSize_nid_postfix(block), 64u);
    EXPECT_LT(sceLibcMspaceMallocUsableSize_nid_postfix(block), 1024u);
}

// Invariant: growing a block whose right-hand neighbour is free extends in place (same pointer, usable size
// tracks the new size), and shrinking keeps the pointer.
TEST_F(MspaceTest, ReallocGrowsInPlaceIntoFreeNeighbour) {
    void* head = sceLibcMspaceMalloc_nid_postfix(arena, 64);
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, head, 1024), head);
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(head), 1024u);
    EXPECT_EQ(sceLibcMspaceRealloc_nid_postfix(arena, head, 32), head);
    sceLibcMspaceFree_nid_postfix(arena, head);
}

// Invariant: posix_memalign returns an aligned block, rejects non-power-of-two / too-small alignment with
// EINVAL without touching *result, and reports ENOMEM when nothing fits.
TEST_F(MspaceTest, PosixMemalignContract) {
    void* aligned = nullptr;
    ASSERT_EQ(sceLibcMspacePosixMemalign_nid_postfix(arena, &aligned, 4096, 1024), 0);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) & 4095, 0u);
    void* unchanged = aligned;
    EXPECT_EQ(sceLibcMspacePosixMemalign_nid_postfix(arena, &unchanged, 3, 8), Einval);
    EXPECT_EQ(unchanged, aligned);
    EXPECT_EQ(sceLibcMspacePosixMemalign_nid_postfix(arena, &unchanged, 4, 8), Einval);  // < sizeof(void*)
    EXPECT_EQ(sceLibcMspacePosixMemalign_nid_postfix(arena, nullptr, 16, 8), Einval);
    EXPECT_EQ(sceLibcMspacePosixMemalign_nid_postfix(arena, &unchanged, 16, RegionSize * 2), Enomem);
    EXPECT_EQ(unchanged, aligned);
    int dummy = 0;
    EXPECT_EQ(sceLibcMspacePosixMemalign_nid_postfix(&dummy, &unchanged, 16, 8), Einval);  // unknown handle
}

// Invariant: memalign honours power-of-two alignments and rejects others with EINVAL + nullptr.
TEST_F(MspaceTest, MemalignContract) {
    void* pointer = sceLibcMspaceMemalign_nid_postfix(arena, 256, 100);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) & 255, 0u);
    EXPECT_EQ(sceLibcMspaceMemalign_nid_postfix(arena, 24, 100), nullptr);
    EXPECT_EQ(GuestErrno(), Einval);
    EXPECT_EQ(sceLibcMspaceMemalign_nid_postfix(arena, 0, 100), nullptr);
    // Small alignments are promoted to the 16-byte granule.
    void* small = sceLibcMspaceMemalign_nid_postfix(arena, 4, 10);
    ASSERT_NE(small, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(small) & 15, 0u);
}

// Invariant: reallocalign preserves data, result alignment, and validates alignment/size arguments.
TEST_F(MspaceTest, ReallocalignContract) {
    auto* block = static_cast<unsigned char*>(sceLibcMspaceReallocalign_nid_postfix(arena, nullptr, 64, 64));
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(block) & 63, 0u);
    for (int i = 0; i < 64; ++i) block[i] = static_cast<unsigned char>(i + 1);
    auto* regrown = static_cast<unsigned char*>(sceLibcMspaceReallocalign_nid_postfix(arena, block, 256, 64));
    ASSERT_NE(regrown, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(regrown) & 63, 0u);
    for (int i = 0; i < 64; ++i) ASSERT_EQ(regrown[i], static_cast<unsigned char>(i + 1));
    // Stricter alignment than the current block has forces a move that still preserves contents.
    auto* moved = static_cast<unsigned char*>(sceLibcMspaceReallocalign_nid_postfix(arena, regrown, 256, 1024));
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(moved) & 1023, 0u);
    for (int i = 0; i < 64; ++i) ASSERT_EQ(moved[i], static_cast<unsigned char>(i + 1));
    EXPECT_EQ(sceLibcMspaceReallocalign_nid_postfix(arena, moved, 16, 0), nullptr);
    EXPECT_EQ(GuestErrno(), Einval);
    EXPECT_EQ(sceLibcMspaceReallocalign_nid_postfix(arena, moved, 16, 3), nullptr);
    // A pointer the mspace never handed out is rejected.
    EXPECT_EQ(sceLibcMspaceReallocalign_nid_postfix(arena, moved + 1, 16, 16), nullptr);
    EXPECT_EQ(sceLibcMspaceReallocalign_nid_postfix(arena, moved, 0, 16), nullptr);  // size 0 frees
    // The block is gone now, so a second free/usable-size query must fail.
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(moved), 0u);
}

// Invariant: stats report the region size as system size, track current/peak in-use bytes, and reject short
// or null structs with EINVAL (both the normal and the Fast entry points).
TEST_F(MspaceTest, MallocStatsAccounting) {
    MallocManagedSize stats{sizeof(MallocManagedSize), 1, 0, 0, 0, 0, 0};
    ASSERT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentSystemSize, RegionSize);
    EXPECT_EQ(stats.maxSystemSize, RegionSize);
    EXPECT_EQ(stats.currentInuseSize, 0u);
    void* block = sceLibcMspaceMalloc_nid_postfix(arena, 100);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(sceLibcMspaceMallocStatsFast_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentInuseSize, 112u);  // 100 rounded up to the 16-byte granule
    sceLibcMspaceFree_nid_postfix(arena, block);
    ASSERT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentInuseSize, 0u);
    EXPECT_EQ(stats.maxInuseSize, 112u);
    MallocManagedSize shortStats{8, 1, 0, 0, 0, 0, 0};
    EXPECT_EQ(sceLibcMspaceMallocStatsFast_nid_postfix(arena, &shortStats), Einval);
    EXPECT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, nullptr), Einval);
    int dummy = 0;
    EXPECT_EQ(sceLibcMspaceMallocStats_nid_postfix(&dummy, &stats), Einval);
}

// Invariant: freeing alternating small blocks leaves fragments that can not satisfy a large request, and
// freeing the rest coalesces them so the large request succeeds (the ordered allocator's coalescing).
TEST_F(MspaceTest, FragmentationHealsAfterFree) {
    std::array<void*, 2000> small{};
    for (auto& pointer : small) {
        pointer = sceLibcMspaceMalloc_nid_postfix(arena, 16);
        ASSERT_NE(pointer, nullptr);
    }
    for (std::size_t i = 0; i < small.size(); i += 2) sceLibcMspaceFree_nid_postfix(arena, small[i]);
    EXPECT_EQ(sceLibcMspaceMalloc_nid_postfix(arena, RegionSize - 256), nullptr);
    for (std::size_t i = 1; i < small.size(); i += 2) sceLibcMspaceFree_nid_postfix(arena, small[i]);
    void* large = sceLibcMspaceMalloc_nid_postfix(arena, RegionSize - 256);
    EXPECT_NE(large, nullptr);
}

// Invariant: free() of null is a no-op; free() of an interior pointer or double free is rejected (EINVAL) and
// does not corrupt accounting.
TEST_F(MspaceTest, FreeValidation) {
    sceLibcMspaceFree_nid_postfix(arena, nullptr);
    void* block = sceLibcMspaceMalloc_nid_postfix(arena, 64);
    ASSERT_NE(block, nullptr);
    *__error_nid_postfix() = 0;
    sceLibcMspaceFree_nid_postfix(arena, static_cast<unsigned char*>(block) + 8);
    EXPECT_EQ(GuestErrno(), Einval);
    sceLibcMspaceFree_nid_postfix(arena, block);
    *__error_nid_postfix() = 0;
    sceLibcMspaceFree_nid_postfix(arena, block);  // double free
    EXPECT_EQ(GuestErrno(), Einval);
    MallocManagedSize stats{sizeof(MallocManagedSize), 1, 0, 0, 0, 0, 0};
    ASSERT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentInuseSize, 0u);
}

// Invariant: an mspace can be created inside a live allocation of another (nested mspace), but not straddling
// an allocation boundary; usable-size lookup resolves inner and outer blocks independently.
TEST_F(MspaceTest, NestedMspace) {
    void* outer = sceLibcMspaceMalloc_nid_postfix(arena, 8192);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(sceLibcMspaceCreate_nid_postfix("outside", static_cast<unsigned char*>(outer) + 8192, 4096, 0), nullptr);
    void* nested = sceLibcMspaceCreate_nid_postfix("nested", outer, 8192, 1 /* thread-unsafe flag accepted */);
    ASSERT_EQ(nested, outer);
    void* inner = sceLibcMspaceMalloc_nid_postfix(nested, 64);
    ASSERT_NE(inner, nullptr);
    EXPECT_GT(inner, outer);
    EXPECT_LT(inner, static_cast<unsigned char*>(outer) + 8192);
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(inner), 64u);
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(outer), 8192u);
    EXPECT_EQ(sceLibcMspaceDestroy_nid_postfix(nested), 0);
    sceLibcMspaceFree_nid_postfix(arena, outer);
}

// Invariant: usable size of null is 0 without touching errno semantics; of a foreign pointer is 0 with EINVAL.
TEST_F(MspaceTest, UsableSizeRejectsForeignPointers) {
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(nullptr), 0u);
    int foreign = 0;
    EXPECT_EQ(sceLibcMspaceMallocUsableSize_nid_postfix(&foreign), 0u);
    EXPECT_EQ(GuestErrno(), Einval);
}

// Invariant: concurrent malloc/free from several threads never hands out the same block twice and leaves the
// arena fully free (global mutex correctness).
TEST_F(MspaceTest, ConcurrentAllocFreeKeepsArenaConsistent) {
    std::array<std::thread, 4> workers;
    std::atomic<int> failures{0};
    for (auto& worker : workers) worker = std::thread([&] {
        for (int i = 0; i < 1000; ++i) {
            void* pointer = sceLibcMspaceMalloc_nid_postfix(arena, 97);
            if (!pointer) { ++failures; continue; }
            std::memset(pointer, 42, 97);
            if (sceLibcMspaceMallocUsableSize_nid_postfix(pointer) < 97) ++failures;
            sceLibcMspaceFree_nid_postfix(arena, pointer);
        }
    });
    for (auto& worker : workers) worker.join();
    EXPECT_EQ(failures.load(), 0);
    MallocManagedSize stats{sizeof(MallocManagedSize), 1, 0, 0, 0, 0, 0};
    ASSERT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentInuseSize, 0u);
}

// Invariant (stress): a random mix of malloc/calloc/realloc/memalign/free keeps every live block inside the
// region, correctly aligned, non-overlapping, and byte-for-byte intact, and frees back to an empty arena.
TEST_F(MspaceTest, RandomizedStressMatchesModel) {
    struct Live { std::size_t size; std::size_t alignment; unsigned char fill; };
    std::map<unsigned char*, Live> live;
    std::mt19937 rng(12345);
    const auto base = static_cast<unsigned char*>(region);
    auto fill = [](unsigned char* pointer, const Live& info) { std::memset(pointer, info.fill, info.size); };
    auto verify = [&](unsigned char* pointer, const Live& info, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) ASSERT_EQ(pointer[i], info.fill) << "corruption at " << i;
    };
    auto checkNoOverlap = [&] {
        unsigned char* previousEnd = base;
        for (const auto& [pointer, info] : live) {
            ASSERT_GE(pointer, previousEnd);
            ASSERT_LE(pointer + info.size, base + RegionSize);
            ASSERT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % info.alignment, 0u);
            ASSERT_GE(sceLibcMspaceMallocUsableSize_nid_postfix(pointer), info.size);
            previousEnd = pointer + info.size;
        }
    };
    for (int step = 0; step < 6000; ++step) {
        const int action = static_cast<int>(rng() % 10);
        if (action < 4 || live.empty()) {
            const std::size_t size = 1 + rng() % 700;
            std::size_t alignment = 16;
            unsigned char* pointer = nullptr;
            if (action == 3) {
                alignment = std::size_t{16} << (rng() % 5);
                pointer = static_cast<unsigned char*>(sceLibcMspaceMemalign_nid_postfix(arena, alignment, size));
            } else if (action == 2) {
                pointer = static_cast<unsigned char*>(sceLibcMspaceCalloc_nid_postfix(arena, 1, size));
                if (pointer) for (std::size_t i = 0; i < size; ++i) ASSERT_EQ(pointer[i], 0);
            } else {
                pointer = static_cast<unsigned char*>(sceLibcMspaceMalloc_nid_postfix(arena, size));
            }
            if (!pointer) continue;  // arena full: legal, just skip
            Live info{size, alignment, static_cast<unsigned char>(rng())};
            fill(pointer, info);
            ASSERT_TRUE(live.emplace(pointer, info).second) << "duplicate allocation returned";
        } else {
            auto it = live.begin();
            std::advance(it, rng() % live.size());
            unsigned char* pointer = it->first;
            Live info = it->second;
            verify(pointer, info, info.size);
            if (action < 7) {
                sceLibcMspaceFree_nid_postfix(arena, pointer);
                live.erase(it);
            } else {
                const std::size_t newSize = 1 + rng() % 900;
                auto* moved = static_cast<unsigned char*>(sceLibcMspaceRealloc_nid_postfix(arena, pointer, newSize));
                if (!moved) continue;  // failure must leave the original intact
                verify(moved, info, std::min(info.size, newSize));
                live.erase(it);
                Live resized{newSize, 16, info.fill};
                fill(moved, resized);
                ASSERT_TRUE(live.emplace(moved, resized).second);
            }
        }
        if (step % 200 == 0) checkNoOverlap();
    }
    checkNoOverlap();
    for (auto& [pointer, info] : live) { verify(pointer, info, info.size); sceLibcMspaceFree_nid_postfix(arena, pointer); }
    MallocManagedSize stats{sizeof(MallocManagedSize), 1, 0, 0, 0, 0, 0};
    ASSERT_EQ(sceLibcMspaceMallocStats_nid_postfix(arena, &stats), 0);
    EXPECT_EQ(stats.currentInuseSize, 0u);
    EXPECT_NE(sceLibcMspaceMalloc_nid_postfix(arena, RegionSize - 256), nullptr);  // fully coalesced
}
