// tests/memory/MemoryTrackerTests.cpp
// GoogleTest suite for guest-memory write tracking (GuestMemoryTracking::Watch,
// core/libs/prx/libc/src/GuestMemoryTracking.cpp; docs/spec/guest-memory.md).
//
// Ported in behaviour from KytyPS5 tests/MemoryTrackerTests.cpp (GPL-2.0).
// Kyty's MemoryTracker models CPU/GPU dirty ownership over 4 MiB regions with
// upload/download range iteration. PortPS5's tracker is smaller: a Watch owns
// a page-rounded range, Protect() arms None/Read/ReadWrite, and a fault (or an
// explicit Resolve/Invalidate) calls the owner's resolver, which must release
// CPU access. Only the invariants that have a counterpart here are kept:
//   - range validation and page rounding        (Kyty TestGuestRange)
//   - protection armed/released, fault resolves (Kyty TestCpuDirtyUpload,
//                                                TestGpuDirtyBits)
//   - invalidate transfers ownership            (Kyty TestRangeInvalidation)
//   - resolver must release / no re-entry       (Kyty fatal-path cases)
//   - concurrent publication and resolution     (Kyty TestConcurrentRegionPublication)
// Dropped as Kyty-internal with no PortPS5 counterpart: RangeSet, GuestRange,
// ForEachUploadRange/ForEachDownloadRange, 4 MiB region masks and protection
// call batching, per-region lock independence (PortPS5 uses one global
// recursive mutex by design), the Linux protected-signal-stack fault test, and
// the process-spawning death harness.
//
// Error contract: libc.prx is a separate module, so a C++ exception thrown by
// a tracker export cannot unwind into the test binary; it reaches
// std::terminate (the same "no throw crosses the export boundary" rule as
// docs/spec/threading.md). Contract violations are therefore verified as fatal
// paths with EXPECT_DEATH (.agents/rules/testing.md), matching the what()
// text that the terminate handler prints to stderr.
//
// Threading: all tests run single-threaded except the explicit concurrency
// cases, which join every thread before the fixture unmaps the memory.

#include "prx/libc/include/GuestMemoryBacking.hpp"
#include "prx/libc/include/GuestMemoryTracking.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using GuestMemoryTracking::Access;
using GuestMemoryTracking::Protection;
using GuestMemoryTracking::Watch;

// What a test resolver does when the tracker asks for CPU access.
enum class Release { ReadWrite, ReadOnly, Nothing };

// Shared resolver state. The resolver runs on the faulting thread (from the
// vectored/signal handler) or on the caller of Resolve/Invalidate, always with
// the tracker's recursive mutex held, so plain members are safe; counters that
// concurrency tests read without the lock are atomic.
struct ResolverContext {
    Watch* watch = nullptr;
    Release release = Release::ReadWrite;
    std::atomic<int> reads{0};
    std::atomic<int> writes{0};
    std::atomic<int> invalidates{0};
    // Optional hook run inside the resolver, used for re-entrancy checks.
    void (*inside)(ResolverContext&) = nullptr;
    std::uint64_t address = 0;
    std::size_t bytes = 0;

    int Total() const { return reads + writes + invalidates; }
};

// C-style resolver matching GuestMemoryTracking::Resolver.
void Resolve(void* raw, Access access) {
    auto& ctx = *static_cast<ResolverContext*>(raw);
    switch (access) {
    case Access::Read: ctx.reads++; break;
    case Access::Write: ctx.writes++; break;
    case Access::Invalidate: ctx.invalidates++; break;
    }
    if (ctx.inside != nullptr) ctx.inside(ctx);
    switch (ctx.release) {
    case Release::ReadWrite: ctx.watch->Protect(Protection::ReadWrite); break;
    case Release::ReadOnly:
        // A read-only release satisfies a read but not a write or invalidate.
        ctx.watch->Protect(Protection::Read);
        break;
    case Release::Nothing: break;
    }
}

// Maps kPages native pages of shared guest backing for each test and unmaps it
// afterwards. Tests must destroy their Watch objects before TearDown so the
// unmap does not have to invalidate live watches (that path has its own test).
class MemoryTrackerTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPages = 8;

    void SetUp() override {
        pageSize_ = GuestMemoryTracking::GuestMemoryTrackingPageSize_nid_postfix();
        bytes_ = pageSize_ * kPages;
        // Protection 3 = read|write; 64 KiB alignment satisfies every host
        // allocation granularity.
        base_ = reinterpret_cast<std::uint8_t*>(
            GuestMemoryBacking::GuestMemoryBackingMap_nid_postfix(nullptr, bytes_, 64 * 1024, 3));
        ASSERT_NE(base_, nullptr);
    }

    void TearDown() override {
        if (base_ != nullptr) GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(base_, bytes_);
    }

    std::uint64_t Addr(std::size_t page, std::size_t offset = 0) const {
        return reinterpret_cast<std::uint64_t>(base_) + page * pageSize_ + offset;
    }

    std::uint8_t* base_ = nullptr;
    std::size_t pageSize_ = 0;
    std::size_t bytes_ = 0;
};

// ---- Range validation (Kyty TestGuestRange) --------------------------------

// Invariant: a watch needs a nonzero address and a nonzero size; the ranges
// Kyty rejects as invalid GuestRange (zero address with bytes, empty range,
// wrapping end) are fatal here (std::invalid_argument).
TEST_F(MemoryTrackerTest, CreateRejectsInvalidRange) {
    ResolverContext ctx;
    EXPECT_DEATH(Watch(0, pageSize_, &ctx, Resolve), "invalid tracked guest memory range");
    EXPECT_DEATH(Watch(Addr(0), 0, &ctx, Resolve), "invalid tracked guest memory range");
    // End of range would wrap past the top of the address space.
    EXPECT_DEATH(Watch(UINT64_MAX, 2, &ctx, Resolve), "invalid tracked guest memory range");
}

// Invariant: a watch without its resolver or context could never release CPU
// access, so it is refused up front rather than at first fault.
TEST_F(MemoryTrackerTest, CreateRejectsMissingResolver) {
    ResolverContext ctx;
    EXPECT_DEATH(Watch(Addr(0), pageSize_, nullptr, Resolve), "missing guest memory ownership resolver");
    EXPECT_DEATH(Watch(Addr(0), pageSize_, &ctx, nullptr), "missing guest memory ownership resolver");
}

// Invariant: only memory that has shared guest backing can be watched. Kyty's
// unowned-range query tolerates this; PortPS5 refuses, because a Watch needs
// the backing record to restore native permissions. Failure mode: runtime_error.
TEST_F(MemoryTrackerTest, CreateRejectsUnbackedRange) {
    ResolverContext ctx;
    // One byte past the end of the mapping is unbacked.
    EXPECT_DEATH(Watch(Addr(kPages), 1, &ctx, Resolve), "guest memory backing range is unmapped");
    // A range straddling the end of the mapping is also unbacked.
    EXPECT_DEATH(Watch(Addr(kPages - 1), pageSize_ * 2, &ctx, Resolve), "guest memory backing range is unmapped");
}

// Invariant: two watches may not own the same tracker page. Overlap is judged
// after rounding to native pages, so disjoint byte ranges in one page collide
// while neighbouring pages do not.
TEST_F(MemoryTrackerTest, OverlappingPagesAreRejected) {
    ResolverContext ctx;
    Watch first(Addr(1, 16), 32, &ctx, Resolve);
    EXPECT_DEATH(Watch(Addr(1, 512), 32, &ctx, Resolve), "overlapping guest memory ownership pages");
    EXPECT_DEATH(Watch(Addr(0, 1), pageSize_ * 2, &ctx, Resolve), "overlapping guest memory ownership pages");
    EXPECT_NO_THROW(Watch(Addr(0), pageSize_, &ctx, Resolve));
    EXPECT_NO_THROW(Watch(Addr(2), pageSize_, &ctx, Resolve));
}

// Invariant: destroying a watch frees its pages for a new owner.
TEST_F(MemoryTrackerTest, RangeIsReusableAfterDestroy) {
    ResolverContext ctx;
    { Watch first(Addr(1), pageSize_, &ctx, Resolve); }
    EXPECT_NO_THROW(Watch(Addr(1), pageSize_, &ctx, Resolve));
}

// ---- Page rounding (Kyty "upload range was not page aligned") --------------

// Invariant: a sub-page byte range protects and resolves the whole native page
// and nothing else. Resolve() on a neighbouring page must not call the
// resolver; any byte inside the watched page must.
TEST_F(MemoryTrackerTest, WatchCoversWholeNativePageOnly) {
    ResolverContext ctx;
    Watch watch(Addr(3, 16), 32, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);

    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(2, pageSize_ - 1), 1, false);
    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(4), 1, false);
    EXPECT_EQ(ctx.Total(), 0);

    // Last byte of the page, outside the requested 32-byte window.
    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(3, pageSize_ - 1), 1, false);
    EXPECT_EQ(ctx.reads, 1);
}

// ---- Protection transitions and fault resolution (Kyty TestCpuDirtyUpload,
// TestGpuDirtyBits) -----------------------------------------------------------

// Invariant: a Read-protected page lets the CPU read freely, and the first CPU
// write faults into the resolver with Access::Write. The resolver releases the
// page, the write lands, and later writes no longer fault.
TEST_F(MemoryTrackerTest, WriteToReadProtectedPageFaultsOnceThenSucceeds) {
    ResolverContext ctx;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::Read);

    volatile std::uint8_t* page = base_ + pageSize_;
    const std::uint8_t before = page[0];  // A read must not fault.
    EXPECT_EQ(before, 0);
    EXPECT_EQ(ctx.Total(), 0);

    page[0] = 0xA5;  // Faults into the resolver, which releases write access.
    EXPECT_EQ(ctx.writes, 1);
    EXPECT_EQ(ctx.reads, 0);
    EXPECT_EQ(page[0], 0xA5);

    page[1] = 0x5A;  // Page is ReadWrite now: no second fault.
    EXPECT_EQ(ctx.Total(), 1);
}

// Invariant: a None-protected page faults on read too, reported as
// Access::Read; a read-only release then satisfies reads and a later write
// re-enters the resolver as Access::Write (the two-step GPU-owned then
// CPU-dirty hand-off).
TEST_F(MemoryTrackerTest, ReadFaultOnNoAccessPageThenUpgradeOnWrite) {
    ResolverContext ctx;
    ctx.release = Release::ReadOnly;
    Watch watch(Addr(2), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);

    volatile std::uint8_t* page = base_ + pageSize_ * 2;
    const std::uint8_t value = page[7];
    EXPECT_EQ(value, 0);
    EXPECT_EQ(ctx.reads, 1);
    EXPECT_EQ(ctx.writes, 0);

    ctx.release = Release::ReadWrite;
    page[7] = 0x11;
    EXPECT_EQ(ctx.writes, 1);
    EXPECT_EQ(page[7], 0x11);
}

// Invariant: a fault only touches the watched page; its neighbours keep
// native ReadWrite and never reach the resolver.
TEST_F(MemoryTrackerTest, FaultDoesNotLeakToNeighbourPages) {
    ResolverContext ctx;
    Watch watch(Addr(3), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);

    volatile std::uint8_t* before = base_ + pageSize_ * 2;
    volatile std::uint8_t* after = base_ + pageSize_ * 4;
    before[0] = 1;
    after[0] = 2;
    EXPECT_EQ(ctx.Total(), 0);
}

// Invariant: Protect() to the current protection is idempotent, and dropping
// from None to Read to ReadWrite keeps the original permissions so the page
// ends native ReadWrite (no residual protection).
TEST_F(MemoryTrackerTest, ProtectSequenceEndsReadWrite) {
    ResolverContext ctx;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);
    watch.Protect(Protection::None);
    watch.Protect(Protection::Read);
    watch.Protect(Protection::ReadWrite);
    watch.Protect(Protection::ReadWrite);

    volatile std::uint8_t* page = base_ + pageSize_;
    page[0] = 9;
    EXPECT_EQ(page[0], 9);
    EXPECT_EQ(ctx.Total(), 0);
}

// Invariant: destroying an armed watch restores the pre-watch native
// protection, so later writes do not fault (and so cannot reach a dead
// resolver). Covers both the None and Read arming paths.
TEST_F(MemoryTrackerTest, DestroyRestoresOriginalProtection) {
    ResolverContext ctx;
    for (Protection armed : {Protection::Read, Protection::None}) {
        {
            Watch watch(Addr(1), pageSize_, &ctx, Resolve);
            ctx.watch = &watch;
            watch.Protect(armed);
        }
        volatile std::uint8_t* page = base_ + pageSize_;
        page[0] = 0x42;
        EXPECT_EQ(page[0], 0x42);
    }
    EXPECT_EQ(ctx.Total(), 0);
}

// ---- Explicit Resolve (used by GPU sync before CPU/GPU touches memory) -----

// Invariant: Resolve(read) on a Read-protected watch is a no-op; Resolve(write)
// on it calls the resolver. Zero-length requests never call it.
TEST_F(MemoryTrackerTest, ExplicitResolveHonoursRequestedAccess) {
    ResolverContext ctx;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::Read);

    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, false);
    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), 0, true);
    EXPECT_EQ(ctx.Total(), 0);

    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, true);
    EXPECT_EQ(ctx.writes, 1);

    // Released to ReadWrite: a second write request is now a no-op.
    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, true);
    EXPECT_EQ(ctx.Total(), 1);
}

// Invariant: one Resolve over several watches calls each overlapped owner once
// and skips the rest (Kyty batches ownership transfer across regions).
TEST_F(MemoryTrackerTest, ExplicitResolveSpansMultipleWatches) {
    ResolverContext a, b, c;
    Watch wa(Addr(0), pageSize_, &a, Resolve);
    Watch wb(Addr(1), pageSize_, &b, Resolve);
    Watch wc(Addr(5), pageSize_, &c, Resolve);
    a.watch = &wa; b.watch = &wb; c.watch = &wc;
    wa.Protect(Protection::None);
    wb.Protect(Protection::None);
    wc.Protect(Protection::None);

    GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(0, 100), pageSize_ + 100, false);
    EXPECT_EQ(a.reads, 1);
    EXPECT_EQ(b.reads, 1);
    EXPECT_EQ(c.Total(), 0);
}

// ---- Invalidate (Kyty TestRangeInvalidation) --------------------------------

// Invariant: Invalidate calls the resolver with Access::Invalidate for every
// overlapped watch even when it is clean (ReadWrite), skips disjoint ones, and
// treats a zero-length range as a no-op. After it, the watch is inactive.
TEST_F(MemoryTrackerTest, InvalidateNotifiesOverlappedWatchesOnly) {
    ResolverContext hit, clean, miss;
    Watch whit(Addr(1), pageSize_, &hit, Resolve);
    Watch wclean(Addr(2), pageSize_, &clean, Resolve);
    Watch wmiss(Addr(6), pageSize_, &miss, Resolve);
    hit.watch = &whit; clean.watch = &wclean; miss.watch = &wmiss;
    whit.Protect(Protection::None);

    GuestMemoryTracking::GuestMemoryTrackingInvalidate_nid_postfix(Addr(1), 0);
    EXPECT_EQ(hit.Total() + clean.Total() + miss.Total(), 0);

    GuestMemoryTracking::GuestMemoryTrackingInvalidate_nid_postfix(Addr(1, 16), pageSize_ * 2 - 32);
    EXPECT_EQ(hit.invalidates, 1);
    EXPECT_EQ(clean.invalidates, 1);
    EXPECT_EQ(miss.Total(), 0);

    // Resolver released the page, so the CPU can write without faulting.
    volatile std::uint8_t* page = base_ + pageSize_;
    page[0] = 3;
    EXPECT_EQ(hit.writes, 0);
}

// Invariant: unmapping backing invalidates live watches first (so GPU state
// is flushed before the pages disappear). The resolver observes Invalidate,
// and the Watch may be destroyed afterwards without touching unmapped memory.
TEST_F(MemoryTrackerTest, UnmapInvalidatesLiveWatch) {
    ResolverContext ctx;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);

    GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(base_ + pageSize_, pageSize_);
    EXPECT_EQ(ctx.invalidates, 1);
    // Re-map is not needed; the rest of the allocation is still backed, so
    // TearDown's unmap of the full range would fail. Unmap the remainder as
    // two explicit pieces and clear base_.
    GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(base_, pageSize_);
    GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(base_ + pageSize_ * 2, bytes_ - pageSize_ * 2);
    base_ = nullptr;
}

// ---- Resolver contract (Kyty fatal-path cases) ------------------------------

// Invariant: a resolver must release CPU access (Kyty "gpu-dirty-explicit-cpu"
// fatal case). One that leaves the page protected is a contract violation and
// terminates the process with "did not release CPU access".
TEST_F(MemoryTrackerTest, ResolverThatDoesNotReleaseAccessIsFatal) {
    ResolverContext ctx;
    ctx.release = Release::Nothing;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);
    EXPECT_DEATH(GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, false),
                 "guest memory resolver did not release CPU access");
}

// Invariant: the same contract holds on the real fault path. The vectored
// handler cannot let an exception escape into the faulting guest code, so a
// resolver that does not release access on a CPU write ends the process.
TEST_F(MemoryTrackerTest, FaultWithUnreleasedAccessIsFatal) {
    ResolverContext ctx;
    ctx.release = Release::Nothing;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::Read);
    volatile std::uint8_t* page = base_ + pageSize_;
    EXPECT_DEATH({ page[0] = 1; }, "did not release CPU access");
}

// Invariant: a read-only release is insufficient for a write request.
TEST_F(MemoryTrackerTest, ReadOnlyReleaseDoesNotSatisfyWrite) {
    ResolverContext ctx;
    ctx.release = Release::ReadOnly;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);
    EXPECT_DEATH(GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, true),
                 "guest memory resolver did not release CPU access");
}

// Invariant: a resolver that re-enters resolution of its own watch (Kyty's
// "reentrant-upload" case) is fatal instead of recursing without bound.
TEST_F(MemoryTrackerTest, ReentrantResolutionIsFatal) {
    ResolverContext ctx;
    ctx.release = Release::Nothing;
    ctx.address = Addr(1);
    ctx.bytes = pageSize_;
    ctx.inside = [](ResolverContext& c) {
        GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(c.address, c.bytes, false);
    };
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);
    EXPECT_DEATH(GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, false),
                 "recursive guest memory ownership resolution");
}

// ---- Validate (native permission record for protected pages) ---------------

// Invariant: Validate hands the callback every byte outside tracked pages and
// every ReadWrite tracked page, but skips pages that are currently protected
// (their native permission is recorded, not probed).
TEST_F(MemoryTrackerTest, ValidateSkipsProtectedPages) {
    ResolverContext ctx;
    Watch watch(Addr(2), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;

    auto collect = [&](std::uint64_t address, std::size_t bytes) {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        GuestMemoryTracking::GuestMemoryTrackingValidate_nid_postfix(
            address, bytes, [&](std::uint64_t a, std::size_t n) { ranges.emplace_back(a, n); });
        return ranges;
    };

    // ReadWrite watch: the whole request is validated (possibly in pieces).
    std::uint64_t covered = 0;
    for (auto& r : collect(Addr(1), pageSize_ * 3)) covered += r.second;
    EXPECT_EQ(covered, pageSize_ * 3);

    watch.Protect(Protection::Read);
    const auto ranges = collect(Addr(1), pageSize_ * 3);
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0], std::make_pair(Addr(1), static_cast<std::uint64_t>(pageSize_)));
    EXPECT_EQ(ranges[1], std::make_pair(Addr(3), static_cast<std::uint64_t>(pageSize_)));

    watch.Protect(Protection::ReadWrite);
}

// Invariant: Validate without a callback is fatal; zero length is a no-op.
TEST_F(MemoryTrackerTest, ValidateArgumentChecks) {
    EXPECT_DEATH(GuestMemoryTracking::GuestMemoryTrackingValidate_nid_postfix(Addr(0), pageSize_, {}),
                 "missing native memory range validator");
    bool called = false;
    GuestMemoryTracking::GuestMemoryTrackingValidate_nid_postfix(Addr(0), 0, [&](std::uint64_t, std::size_t) { called = true; });
    EXPECT_FALSE(called);
}

// ---- Concurrency (Kyty TestConcurrentRegionPublication) --------------------

// Invariant: many threads resolving the same protected watch call its resolver
// exactly once; losers observe the already-released page. The global tracker
// mutex makes the check-then-resolve atomic.
TEST_F(MemoryTrackerTest, ConcurrentResolveCallsResolverOnce) {
    ResolverContext ctx;
    Watch watch(Addr(1), pageSize_, &ctx, Resolve);
    ctx.watch = &watch;
    watch.Protect(Protection::None);

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(1), pageSize_, true);
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    EXPECT_EQ(ctx.Total(), 1);
}

// Invariant: threads publishing, arming, resolving and destroying watches on
// disjoint pages never corrupt the shared registry; every resolver fires
// exactly once per cycle (Kyty's concurrent region publication).
TEST_F(MemoryTrackerTest, ConcurrentCreateResolveDestroyOnDisjointPages) {
    constexpr int kThreads = 8;
    constexpr int kCycles = 50;
    static_assert(kThreads <= kPages, "one page per thread");
    std::vector<ResolverContext> contexts(kThreads);
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int c = 0; c < kCycles; ++c) {
                Watch watch(Addr(i), pageSize_, &contexts[i], Resolve);
                contexts[i].watch = &watch;
                watch.Protect(Protection::None);
                GuestMemoryTracking::GuestMemoryTrackingResolve_nid_postfix(Addr(i), pageSize_, true);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    for (int i = 0; i < kThreads; ++i) EXPECT_EQ(contexts[i].Total(), kCycles) << "watch " << i;
}

// Invariant: disjoint pages can be watched, armed and faulted from different
// threads at once; each thread's fault reaches only its own resolver.
TEST_F(MemoryTrackerTest, ConcurrentFaultsOnDisjointWatches) {
    constexpr int kThreads = 4;
    std::vector<ResolverContext> contexts(kThreads);
    std::vector<std::unique_ptr<Watch>> watches;
    for (int i = 0; i < kThreads; ++i) {
        watches.push_back(std::make_unique<Watch>(Addr(i), pageSize_, &contexts[i], Resolve));
        contexts[i].watch = watches[i].get();
        watches[i]->Protect(Protection::Read);
    }
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            volatile std::uint8_t* page = base_ + pageSize_ * i;
            page[0] = static_cast<std::uint8_t>(i + 1);
        });
    }
    for (auto& t : threads) t.join();
    for (int i = 0; i < kThreads; ++i) {
        EXPECT_EQ(contexts[i].writes, 1) << "watch " << i;
        EXPECT_EQ(contexts[i].Total(), 1) << "watch " << i;
        EXPECT_EQ(base_[pageSize_ * i], i + 1);
    }
}

}  // namespace
