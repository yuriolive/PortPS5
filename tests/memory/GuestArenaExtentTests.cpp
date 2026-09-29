// tests/memory/GuestArenaExtentTests.cpp
// GoogleTest suite for the arena extent-tree allocator
// (core/libs/prx/libc/include/GuestArenaExtent.hpp,
// docs/spec/guest-memory.md, Target design "Arena allocator").
//
// Strategy: differential-test the tree against a reference ascending linear
// first-fit scan that mirrors upstream GuestArena::Allocate
// (upstream core/libs/prx/libc/src/GuestArena.cpp, Allocate loop over the
// used-range map). Every Allocate in the fuzz stream must return the exact
// same address from both implementations, and the free sets must match
// periodically. Allocation order is guest-visible (titles index tables by
// absolute address), so any mismatch is a correctness bug, not a preference.

#include "common/TestHarness.hpp"
#include "prx/libc/include/GuestArenaExtent.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace {

/// Rounds value up to a nonzero power-of-two alignment and writes out on success.
/// Returns false on overflow, leaving out unchanged.
bool AlignUp(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) {
    const std::uint64_t mask = alignment - 1;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    out = (value + mask) & ~mask;
    return true;
}

// Reference model: ascending linear first-fit over allocated ranges, exactly
// the upstream GuestArena::Allocate scan (candidate starts at the aligned
// arena base and hops over each used range in address order).
class LinearModel {
public:
    /// Resets the reference arena and clears allocations; an empty or wrapping range returns false unchanged.
    bool Init(std::uint64_t base, std::uint64_t bytes) {
        if (bytes == 0 || bytes > UINT64_MAX - base) {
            return false;
        }
        base_ = base;
        end_ = base + bytes;
        used_.clear();
        return true;
    }

    /// Returns the first aligned gap from the linear scan, recording the allocation.
    /// Returns 0 for invalid arguments, alignment overflow, or insufficient space;
    /// callers initialize the model with the nonzero arena used by the tests.
    std::uint64_t Allocate(std::uint64_t bytes, std::uint64_t alignment) {
        if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
            return 0;
        }
        std::uint64_t candidate = 0;
        if (!AlignUp(base_, alignment, candidate)) {
            return 0;
        }
        for (const auto& [start, finish] : used_) {
            if (candidate <= UINT64_MAX - bytes && candidate + bytes <= start) {
                break;
            }
            std::uint64_t stepped = 0;
            if (!AlignUp(finish, alignment, stepped)) {
                return 0;
            }
            candidate = candidate > stepped ? candidate : stepped;
        }
        if (bytes > end_ - candidate) {
            return 0;
        }
        used_.emplace(candidate, candidate + bytes);
        return candidate;
    }

    /// Removes an exact recorded [base, base + bytes) range, or returns false without mutation.
    bool Free(std::uint64_t base, std::uint64_t bytes) {
        const auto it = used_.find(base);
        if (it == used_.end() || it->second != base + bytes) {
            return false;
        }
        used_.erase(it);
        return true;
    }

    /// Returns free gaps as ascending (base, size) pairs derived from the used map.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> FreeExtents() const {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> gaps;
        std::uint64_t cursor = base_;
        for (const auto& [start, finish] : used_) {
            if (cursor < start) {
                gaps.emplace_back(cursor, start - cursor);
            }
            cursor = finish;
        }
        if (cursor < end_) {
            gaps.emplace_back(cursor, end_ - cursor);
        }
        return gaps;
    }

private:
    std::uint64_t base_ = 0;
    std::uint64_t end_ = 0;
    std::map<std::uint64_t, std::uint64_t> used_;  // start -> end
};

// Deterministic PRNG (splitmix64) so the op stream is identical every run.
class Rng {
public:
    /// Seeds the deterministic operation stream; identical seeds reproduce the same sequence.
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    /// Advances the generator state and returns the next mixed 64-bit value.
    std::uint64_t Next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t state_;
};

/// Asserts that model and tree have identical ascending free extents, comparing count, bases, and sizes.
void ExpectFreeSetsEqual(const LinearModel& model, const PortPS5::GuestMemory::ExtentAllocator& tree) {
    const auto expected = model.FreeExtents();
    std::vector<std::pair<std::uint64_t, std::uint64_t>> actual;
    tree.ForEachFree([&](std::uint64_t base, std::uint64_t size) { actual.emplace_back(base, size); });
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].first, expected[i].first) << "free extent " << i << " base";
        EXPECT_EQ(actual[i].second, expected[i].second) << "free extent " << i << " size";
    }
}

}  // namespace

/// Verifies fresh allocations come back in ascending address order (the
/// guest-visible first-fit contract titles rely on).
TEST(GuestArenaExtent, AscendingFirstFitOrder) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x10000ULL));
    const std::uint64_t first = tree.Allocate(0x1000ULL, 1ULL);
    const std::uint64_t second = tree.Allocate(0x1000ULL, 1ULL);
    const std::uint64_t third = tree.Allocate(0x1000ULL, 1ULL);
    EXPECT_EQ(first, 0x10000ULL);
    EXPECT_EQ(second, 0x11000ULL);
    EXPECT_EQ(third, 0x12000ULL);
}

/// Verifies an aligned request skips a tight low extent whose alignment
/// padding would overflow it, landing exactly where the linear scan lands.
TEST(GuestArenaExtent, AlignmentSkipsTightExtent) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    LinearModel model;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x20000ULL));
    ASSERT_TRUE(model.Init(0x10000ULL, 0x20000ULL));
    EXPECT_EQ(tree.Allocate(0x800ULL, 1ULL), model.Allocate(0x800ULL, 1ULL));
    // Free head is [0x10800, ...): 0x800 bytes at 4 KiB alignment must hop to 0x11000.
    EXPECT_EQ(tree.Allocate(0x800ULL, 0x1000ULL), 0x11000ULL);
    EXPECT_EQ(model.Allocate(0x800ULL, 0x1000ULL), 0x11000ULL);
}

/// Verifies freeing the middle of three contiguous blocks reuses the lowest
/// fit, and that freeing all three coalesces back to a single extent.
TEST(GuestArenaExtent, CoalesceBothNeighbours) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x30000ULL));
    const std::uint64_t a = tree.Allocate(0x1000ULL, 1ULL);
    const std::uint64_t b = tree.Allocate(0x1000ULL, 1ULL);
    const std::uint64_t c = tree.Allocate(0x1000ULL, 1ULL);
    ASSERT_TRUE(tree.Free(a, 0x1000ULL));
    ASSERT_TRUE(tree.Free(c, 0x1000ULL));
    // c's release merges right into the tail extent; a stays disjoint.
    EXPECT_EQ(tree.FreeExtentCount(), 2ULL);
    ASSERT_TRUE(tree.Free(b, 0x1000ULL));
    // Middle release bridges both neighbours into the whole arena.
    EXPECT_EQ(tree.FreeExtentCount(), 1ULL);
    EXPECT_EQ(tree.Allocate(0x3000ULL, 1ULL), 0x10000ULL);
}

/// Verifies an allocation of the whole arena drains the tree, further
/// allocation fails, and a full free restores one single extent.
TEST(GuestArenaExtent, ExactFitAndExhaustion) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x4000ULL));
    EXPECT_EQ(tree.FreeExtentCount(), 1ULL);
    EXPECT_EQ(tree.Allocate(0x4000ULL, 1ULL), 0x10000ULL);
    EXPECT_EQ(tree.FreeExtentCount(), 0ULL);
    EXPECT_EQ(tree.Allocate(1ULL, 1ULL), 0ULL);
    ASSERT_TRUE(tree.Free(0x10000ULL, 0x4000ULL));
    EXPECT_EQ(tree.FreeExtentCount(), 1ULL);
    EXPECT_EQ(tree.Allocate(0x4000ULL, 0x4000ULL), 0x10000ULL);
}

/// Verifies invalid arguments fail cleanly with no state change and no throw:
/// zero bytes, non-pow2 alignment, unknown/double/overlapping frees, ranges
/// outside the arena, and wrapping ranges.
TEST(GuestArenaExtent, InvalidArgsRejected) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x10000ULL));
    EXPECT_EQ(tree.Allocate(0ULL, 1ULL), 0ULL);
    EXPECT_EQ(tree.Allocate(0x100ULL, 0ULL), 0ULL);
    EXPECT_EQ(tree.Allocate(0x100ULL, 3ULL), 0ULL);
    EXPECT_FALSE(tree.Free(0x50000ULL, 0x1000ULL));   // outside arena
    EXPECT_FALSE(tree.Free(0x10000ULL, 0x1000ULL));   // never allocated
    EXPECT_FALSE(tree.Free(UINT64_MAX - 100ULL, 200ULL));  // wrapping range
    const std::uint64_t live = tree.Allocate(0x1000ULL, 1ULL);
    ASSERT_NE(live, 0ULL);
    EXPECT_FALSE(tree.Free(live, 0x2000ULL));  // overlaps live tail
    ASSERT_TRUE(tree.Free(live, 0x1000ULL));
    EXPECT_FALSE(tree.Free(live, 0x1000ULL));  // double free
    // Failed re-Init leaves the working state untouched.
    EXPECT_FALSE(tree.Init(0x20000ULL, 0ULL));
    EXPECT_EQ(tree.Allocate(0x1000ULL, 1ULL), 0x10000ULL);
}

/// Verifies failed operations never mutate the free set: oversized requests
/// (including bytes larger than the whole arena, which once wrapped the
/// containment subtraction and wrongly reported containment, letting Free
/// insert past arenaEnd_ on a drained arena), unknown and double frees, and
/// invalid arguments all leave the tree identical, and the arena still serves
/// allocations afterwards.
TEST(GuestArenaExtent, FailedOpsPreserveFreeSet) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x10000ULL));
    const std::uint64_t live = tree.Allocate(0x1000ULL, 1ULL);
    ASSERT_EQ(live, 0x10000ULL);
    auto snapshot = [&]() {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
        tree.ForEachFree([&](std::uint64_t base, std::uint64_t size) { extents.emplace_back(base, size); });
        return extents;
    };
    const auto before = snapshot();
    // Oversized containment must be rejected, not wrap around.
    EXPECT_FALSE(tree.Contains(0x10000ULL, 0x10001ULL));
    EXPECT_FALSE(tree.Contains(0x10000ULL, UINT64_MAX - 0x10000ULL));
    // Oversized frees rejected without inserting past arenaEnd_.
    EXPECT_FALSE(tree.Free(0x10000ULL, 0x10001ULL));
    // Failing allocs: larger than the arena, zero bytes, bad alignment.
    EXPECT_EQ(tree.Allocate(0x10001ULL, 1ULL), 0ULL);
    EXPECT_EQ(tree.Allocate(0ULL, 1ULL), 0ULL);
    EXPECT_EQ(tree.Allocate(0x100ULL, 3ULL), 0ULL);
    // Failing frees: outside the arena, inside a free extent (unknown), and
    // sub-ranges of the live allocation (never returned as a whole: head
    // slice, tail slice, overhang). The sub-range cases were accepted before
    // ownership tracking and would have aliased live guest memory.
    EXPECT_FALSE(tree.Free(0x50000ULL, 0x1000ULL));
    EXPECT_FALSE(tree.Free(0x15000ULL, 0x1000ULL));
    EXPECT_FALSE(tree.Free(live, 0x800ULL));
    EXPECT_FALSE(tree.Free(live + 0x800ULL, 0x800ULL));
    EXPECT_EQ(snapshot(), before);
    // Drained-arena variant: with no free extents left, an oversized Free
    // must still be rejected (it previously slipped past the overlap checks
    // and inserted memory outside the arena, which Allocate could then hand
    // out).
    PortPS5::GuestMemory::ExtentAllocator full;
    ASSERT_TRUE(full.Init(0x10000ULL, 0x10000ULL));
    ASSERT_EQ(full.Allocate(0x10000ULL, 1ULL), 0x10000ULL);
    EXPECT_FALSE(full.Free(0x10000ULL, 0x10001ULL));
    EXPECT_EQ(full.FreeExtentCount(), 0ULL);
    EXPECT_EQ(full.Allocate(1ULL, 1ULL), 0ULL);
    // Arena still fully functional afterwards.
    ASSERT_TRUE(tree.Free(live, 0x1000ULL));
    EXPECT_FALSE(tree.Free(live, 0x1000ULL));  // double free
    EXPECT_EQ(tree.Allocate(0x10000ULL, 1ULL), 0x10000ULL);
}

/// Verifies a ForEachFree visitor may call back into the same allocator: the
/// traversal runs over a snapshot, so extents allocated mid-visit cannot
/// corrupt the iteration (previously resuming through a deleted node's right
/// pointer was use-after-free).
TEST(GuestArenaExtent, ForEachFreeReentrantVisitorSafe) {
    PortPS5::GuestMemory::ExtentAllocator tree;
    ASSERT_TRUE(tree.Init(0x10000ULL, 0x10000ULL));
    std::uint64_t visited = 0;
    tree.ForEachFree([&](std::uint64_t, std::uint64_t) {
        ++visited;
        if (visited == 1) {
            EXPECT_EQ(tree.Allocate(0x1000ULL, 1ULL), 0x10000ULL);
        }
    });
    // Snapshot semantics: exactly the one pre-visit extent is observed, even
    // though the tree changed underneath.
    EXPECT_EQ(visited, 1ULL);
    // Tree stays coherent: the remainder is one extent serving the rest.
    EXPECT_EQ(tree.FreeExtentCount(), 1ULL);
    EXPECT_EQ(tree.Allocate(0xF000ULL, 1ULL), 0x11000ULL);
}

/// Verifies the tree returns bit-identical addresses to the reference linear
/// scan across a mixed alloc/free stream, with periodic free-set comparison.
TEST(GuestArenaExtent, MatchesLinearScanFuzz) {
    constexpr std::uint64_t kBase = 0x10'0000'0000ULL;  // arena-like placement above 1 TiB
    constexpr std::uint64_t kSize = 0x4000'0000ULL;     // 1 GiB
    constexpr int kOps = 1000000;  // spec guest-memory.md: 10^6 random operations
    constexpr std::uint64_t kAligns[] = {1ULL, 16ULL, 0x1000ULL, 0x4000ULL, 0x10000ULL, 0x200000ULL};

    PortPS5::GuestMemory::ExtentAllocator tree;
    LinearModel model;
    ASSERT_TRUE(tree.Init(kBase, kSize));
    ASSERT_TRUE(model.Init(kBase, kSize));

    Rng rng(0x12345678ULL);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> live;
    live.reserve(8192);
    for (int i = 0; i < kOps; ++i) {
        const std::uint64_t roll = rng.Next() % 100;
        if (roll < 4 && !live.empty()) {
            // Invalid-free probe: corrupt a live entry's base or size, or
            // probe inside a free gap. Both implementations must reject
            // without mutating (ownership gate). Skipped when the corrupted
            // range accidentally matches a live entry exactly.
            const auto [lb, ls] = live[static_cast<std::size_t>(rng.Next() % live.size())];
            std::uint64_t fb = lb;
            std::uint64_t fs = ls;
            if (rng.Next() % 2) {
                fb = lb + 1 + rng.Next() % 255;
            } else {
                fs = ls + 1 + rng.Next() % 255;
            }
            bool exact = false;
            for (const auto& e : live) {
                exact = exact || (e.first == fb && e.second == fs);
            }
            if (exact) {
                continue;
            }
            EXPECT_FALSE(model.Free(fb, fs)) << "op " << i << " model accepted invalid free";
            EXPECT_FALSE(tree.Free(fb, fs)) << "op " << i << " base=" << fb << " size=" << fs;
            continue;
        }
        if (roll < 8) {
            // Invalid-alloc probe: both implementations return 0.
            std::uint64_t bytes = 0x100ULL;
            std::uint64_t align = 3ULL;  // non-pow2
            switch (rng.Next() % 3) {
                case 0: bytes = 0ULL; align = 1ULL; break;
                case 1: bytes = 0x100ULL; align = 3ULL; break;
                default: bytes = kSize * 2ULL; align = 1ULL; break;  // oversized
            }
            EXPECT_EQ(tree.Allocate(bytes, align), model.Allocate(bytes, align)) << "op " << i;
            continue;
        }
        const bool doAlloc = live.empty() || (rng.Next() % 100) < 70;
        if (doAlloc) {
            // Size mix: tiny grains, 16 KiB-page multiples, and large spans.
            const std::uint64_t pick = rng.Next() % 100;
            std::uint64_t bytes = 0;
            if (pick < 40) {
                bytes = 1 + rng.Next() % 256;
            } else if (pick < 80) {
                bytes = (1 + rng.Next() % 64) * 0x4000ULL;
            } else {
                bytes = (1 + rng.Next() % 16) * 0x100000ULL;
            }
            const std::uint64_t align = kAligns[rng.Next() % 6];
            const std::uint64_t want = model.Allocate(bytes, align);
            const std::uint64_t got = tree.Allocate(bytes, align);
            ASSERT_EQ(got, want) << "op " << i << " alloc bytes=" << bytes << " align=" << align;
            if (want != 0) {
                live.emplace_back(want, bytes);
            }
        } else {
            const std::size_t idx = static_cast<std::size_t>(rng.Next() % live.size());
            const auto [base, bytes] = live[idx];
            ASSERT_TRUE(model.Free(base, bytes)) << "op " << i;
            ASSERT_TRUE(tree.Free(base, bytes)) << "op " << i << " base=" << base;
            live[idx] = live.back();
            live.pop_back();
        }
        if ((i & 0x3FFF) == 0) {
            SCOPED_TRACE(::testing::Message() << "op " << i);
            ExpectFreeSetsEqual(model, tree);
        }
    }
    SCOPED_TRACE(::testing::Message() << "final drain check");
    ExpectFreeSetsEqual(model, tree);
}
