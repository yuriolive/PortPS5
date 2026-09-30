// core/libs/prx/libSceAgcDriver/tests/HostImportTests.cpp
//
// GoogleTest suite for Graphics/HostImport: host import of guest memory with a staging fallback
// (docs/spec/gpu-driver.md, "Host-import budget and staging"; ROADMAP M1 Lane A, portps5-6). The GPU
// side is vkCmdFillBuffer, so no shader or game data is involved. Staging tests run on any Vulkan
// device; import tests are skipped on devices without VK_EXT_external_memory_host (lavapipe has it).
#include "RecorderTestSupport.hpp"
#include "prx/libSceAgcDriver/Graphics/include/HostImport.hpp"
#include <gtest/gtest.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace AgcDriver::Graphics;
using AgcDriverTest::AlignedBlock;
using AgcDriverTest::FakeTracker;
using AgcDriverTest::VulkanTestDevice;

namespace {

class HostImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Ok()) GTEST_SKIP() << "no Vulkan device: " << device.Failure();
        Recorder::Options options;
        options.timelineSemaphores = device.Timeline();
        options.tracker = &tracker;
        recorder = std::make_unique<Recorder>(device.GetContext(), options);
    }
    void TearDown() override {
        // Teardown order: imports/staging first (they sync the recorder), then the recorder.
        imports.reset();
        recorder.reset();
    }
    void MakeImports(std::uint64_t budget, std::uint64_t stagingCache = 256ull << 20) {
        HostImportOptions options;
        options.importBudgetBytes = budget;
        options.stagingCacheBytes = stagingCache;
        imports = std::make_unique<HostImport>(device.GetContext(), *recorder, tracker, options);
    }
    bool CanImport() const { return device.GetContext().externalMemoryHost && device.GetContext().hostImportAlignment != 0; }
    std::uint64_t Alignment() const { return device.GetContext().hostImportAlignment; }
    // Records a fill of the bound range with `value` into the open batch.
    void Fill(const GuestBinding& binding, std::uint32_t value) {
        const Recorder::Scope scope(*recorder);
        const auto commands = recorder->Commands();
        device.GetContext().Function<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(commands, binding.buffer, binding.offset, binding.bytes, value);
    }

    VulkanTestDevice device;
    FakeTracker tracker;
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr<HostImport> imports;
};

// Invariant: with the import budget at 0 (the spec's test hook) every bind stages, never imports, and
// the staging copy holds the guest bytes the range had at bind time.
TEST_F(HostImportTest, BudgetZeroForcesStagingAndCopiesGuestBytes) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    for (std::size_t i = 0; i < 256; ++i) guest.Data()[i] = static_cast<std::byte>(i);
    const auto result = imports->Bind(guest.Address(), 256, GuestAccess::Read);
    ASSERT_EQ(result.status, BindStatus::Ok);
    EXPECT_FALSE(result.binding.imported);
    EXPECT_NE(result.binding.buffer, static_cast<VkBuffer>(VK_NULL_HANDLE));
    EXPECT_EQ(result.binding.bytes, 256u);
    const auto stats = imports->Stats();
    EXPECT_EQ(stats.imports, 0u);
    EXPECT_EQ(stats.stagingBinds, 1u);
    EXPECT_EQ(stats.stagingUploads, 1u);
}

// Invariant: a staged range is re-uploaded only when the tracker generation moved (or is unknown, 0):
// an unchanged generation must not copy again, a changed one must.
TEST_F(HostImportTest, StagingRefreshesOnlyWhenTheGenerationMoves) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    ASSERT_EQ(imports->Bind(guest.Address(), 128, GuestAccess::Read).status, BindStatus::Ok);
    ASSERT_EQ(imports->Bind(guest.Address(), 128, GuestAccess::Read).status, BindStatus::Ok);
    EXPECT_EQ(imports->Stats().stagingUploads, 1u) << "same generation: no second copy";
    tracker.generation = 2;
    ASSERT_EQ(imports->Bind(guest.Address(), 128, GuestAccess::Read).status, BindStatus::Ok);
    EXPECT_EQ(imports->Stats().stagingUploads, 2u);
    tracker.generation = 0;  // unknown: compare nothing, copy always
    ASSERT_EQ(imports->Bind(guest.Address(), 128, GuestAccess::Read).status, BindStatus::Ok);
    ASSERT_EQ(imports->Bind(guest.Address(), 128, GuestAccess::Read).status, BindStatus::Ok);
    EXPECT_EQ(imports->Stats().stagingUploads, 4u);
}

// Invariant: GPU stores into a staged write bind reach guest memory after the batch completed (write-back
// completion), are reported to the tracker, and are not visible before.
TEST_F(HostImportTest, StagedWritesAreWrittenBackOnCompletion) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    const auto result = imports->Bind(guest.Address() + 64, 128, GuestAccess::Write);
    ASSERT_EQ(result.status, BindStatus::Ok);
    ASSERT_FALSE(result.binding.imported);
    Fill(result.binding, 0xDEADBEEF);
    std::uint32_t before = 1;
    std::memcpy(&before, guest.Data() + 64, 4);
    EXPECT_EQ(before, 0u) << "guest memory is untouched until the write-back";
    recorder->Sync();
    for (std::size_t offset = 64; offset < 64 + 128; offset += 4) {
        std::uint32_t value = 0;
        std::memcpy(&value, guest.Data() + offset, 4);
        ASSERT_EQ(value, 0xDEADBEEFu) << "offset " << offset;
    }
    std::uint32_t outside = 1;
    std::memcpy(&outside, guest.Data() + 64 + 128, 4);
    EXPECT_EQ(outside, 0u) << "only the bound range is written back";
    ASSERT_EQ(tracker.Written().size(), 1u);
    EXPECT_EQ(tracker.Written()[0], std::make_pair(guest.Address() + 64, std::uint64_t{128}));
}

// Invariant: a CPU read through the tracker (flush hook) of a staged, GPU-written range waits for the
// batch and its write-back, so the CPU observes the GPU's result without an explicit Sync.
TEST_F(HostImportTest, CpuAccessAfterStagedWriteSeesTheResult) {
    MakeImports(0);
    recorder->Activate(&tracker);
    AlignedBlock guest(4096, 4096);
    const auto result = imports->Bind(guest.Address(), 64, GuestAccess::Write);
    ASSERT_EQ(result.status, BindStatus::Ok);
    Fill(result.binding, 0x01020304);
    tracker.Collect(guest.Address(), 64);
    std::uint32_t value = 0;
    std::memcpy(&value, guest.Data(), 4);
    EXPECT_EQ(value, 0x01020304u);
}

// Invariant: bind failures are codes, not exceptions: empty, null and overflowing ranges are
// InvalidRange; an unmapped range is NotMapped.
TEST_F(HostImportTest, InvalidAndUnmappedRangesReturnCodes) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    EXPECT_EQ(imports->Bind(guest.Address(), 0, GuestAccess::Read).status, BindStatus::InvalidRange);
    EXPECT_EQ(imports->Bind(0, 16, GuestAccess::Read).status, BindStatus::InvalidRange);
    EXPECT_EQ(imports->Bind(~std::uint64_t{0} - 4, 64, GuestAccess::Read).status, BindStatus::InvalidRange);
    EXPECT_EQ(imports->Bind(0x10, 16, GuestAccess::Read).status, BindStatus::NotMapped);
    // Rejected binds reserve nothing (scenario from sharpemu EmptyUploadDoesNotReserveStagingBytes).
    const auto stats = imports->Stats();
    EXPECT_EQ(stats.stagingBinds, 0u);
    EXPECT_EQ(stats.stagingUploads, 0u);
    EXPECT_EQ(stats.imports, 0u);
}

// Invariant: the staging cache is bounded: past the cap the least recently used copies are dropped, and
// a dropped range simply re-uploads on its next bind (performance only, never results).
TEST_F(HostImportTest, StagingCacheDropsLeastRecentlyUsed) {
    MakeImports(0, 1024);
    AlignedBlock guest(16384, 4096);
    for (std::size_t i = 0; i < 4; ++i) ASSERT_EQ(imports->Bind(guest.Address() + i * 4096, 512, GuestAccess::Read).status, BindStatus::Ok);
    EXPECT_EQ(imports->Stats().stagingUploads, 4u);
    ASSERT_EQ(imports->Bind(guest.Address(), 512, GuestAccess::Read).status, BindStatus::Ok);
    EXPECT_EQ(imports->Stats().stagingUploads, 5u) << "the oldest copy was dropped, so it re-uploads";
}

// The scenarios below are adapted from sharpemu's GPU buffer tests (GPL-2.0-or-later, tests/
// SharpEmu.Libs.Tests/Gpu/Buffers/GuestBufferCacheTests.cs); only behaviour that has a counterpart in
// this design is ported, re-expressed against HostImport, not copied.

// Invariant (sharpemu UnalignedReadbackAcrossDownloadBatchesPreservesAdjacentGuestBytes): a staged write
// back of an odd-offset, odd-length range changes exactly the bound bytes and leaves the adjacent guest
// bytes (set to a sentinel) untouched.
TEST_F(HostImportTest, UnalignedStagedWriteBackPreservesAdjacentBytes) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    std::memset(guest.Data(), 0xEE, 256);
    const auto result = imports->Bind(guest.Address() + 3, 36, GuestAccess::Write);
    ASSERT_EQ(result.status, BindStatus::Ok);
    Fill(result.binding, 0x5A5A5A5A);
    recorder->Sync();
    for (std::size_t i = 0; i < 256; ++i) {
        const auto expected = (i >= 3 && i < 3 + 36) ? std::byte{0x5A} : std::byte{0xEE};
        ASSERT_EQ(guest.Data()[i], expected) << "byte " << i;
    }
}

// Invariant (sharpemu MergedAllocationPreservesBothGpuWrittenRanges): two overlapping staged write binds
// from successive batches both land, in submission order: the later batch wins the overlap and the
// non-overlapping parts of both survive.
TEST_F(HostImportTest, OverlappingStagedWritesLandInSubmissionOrder) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    const auto first = imports->Bind(guest.Address(), 64, GuestAccess::Write);
    ASSERT_EQ(first.status, BindStatus::Ok);
    Fill(first.binding, 0x11111111);
    recorder->Submit();
    const auto second = imports->Bind(guest.Address() + 32, 64, GuestAccess::Write);
    ASSERT_EQ(second.status, BindStatus::Ok);
    Fill(second.binding, 0x22222222);
    recorder->Sync();
    for (std::size_t offset = 0; offset < 96; offset += 4) {
        std::uint32_t value = 0;
        std::memcpy(&value, guest.Data() + offset, 4);
        ASSERT_EQ(value, offset < 32 ? 0x11111111u : 0x22222222u) << "offset " << offset;
    }
}

// Invariant (sharpemu ReplacedBufferRemainsAliveUntilRecordedCopiesComplete): refreshing a staged range
// while a batch still reads the old copy hands out a DIFFERENT buffer, so the in-flight batch never sees
// its source overwritten.
TEST_F(HostImportTest, StagingRefreshNeverReusesABufferAnUnfinishedBatchReads) {
    MakeImports(0);
    AlignedBlock guest(4096, 4096);
    const auto before = imports->Bind(guest.Address(), 64, GuestAccess::Read);
    ASSERT_EQ(before.status, BindStatus::Ok);
    Fill(before.binding, 1);  // the unfinished open batch uses the first copy
    tracker.generation = 2;
    const auto after = imports->Bind(guest.Address(), 64, GuestAccess::Read);
    ASSERT_EQ(after.status, BindStatus::Ok);
    EXPECT_NE(after.binding.buffer, before.binding.buffer);
    EXPECT_EQ(imports->Stats().stagingUploads, 2u);
}

#ifdef _WIN32
// Invariant (sharpemu WrittenObtain_AcrossABackingGapIsRefusedEvenWhenBothEndsAreBacked): a write bind
// whose range crosses an inaccessible page is refused as NotMapped even though both ends are mapped, and
// the refusal changes no state: no pending write is noted and no completion is queued.
TEST_F(HostImportTest, WriteAcrossAnUnmappedHoleIsRefusedWithoutSideEffects) {
    MakeImports(0);
    recorder->Activate();
    constexpr std::size_t Page = 4096;
    auto* base = static_cast<std::byte*>(VirtualAlloc(nullptr, 3 * Page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    ASSERT_NE(base, nullptr);
    DWORD previous = 0;
    ASSERT_TRUE(VirtualProtect(base + Page, Page, PAGE_NOACCESS, &previous));
    const auto address = reinterpret_cast<std::uint64_t>(base) + Page - 16;
    const auto result = imports->Bind(address, 32 + Page, GuestAccess::Write);
    EXPECT_EQ(result.status, BindStatus::NotMapped);
    EXPECT_FALSE(recorder->PendingWriteOverlaps(address, 32));
    EXPECT_FALSE(recorder->HasCompletions());
    EXPECT_TRUE(imports->Stats().stagingBinds == 0);
    VirtualFree(base, 0, MEM_RELEASE);
}
#endif

// Invariant: a device with VK_EXT_external_memory_host and a budget imports the guest allocation itself
// (no copy), and GPU stores land in guest memory directly; the tracker learns of them at completion.
TEST_F(HostImportTest, ImportedWritesLandInGuestMemoryDirectly) {
    if (!CanImport()) GTEST_SKIP() << "VK_EXT_external_memory_host unavailable";
    MakeImports(64ull << 20);
    AlignedBlock guest(static_cast<std::size_t>(Alignment()) * 2, static_cast<std::size_t>(Alignment()));
    const auto result = imports->Bind(guest.Address() + 16, 256, GuestAccess::Write);
    ASSERT_EQ(result.status, BindStatus::Ok);
    ASSERT_TRUE(result.binding.imported);
    EXPECT_EQ(result.binding.offset, 16u) << "the binding starts inside the aligned import window";
    Fill(result.binding, 0xCAFEF00D);
    recorder->Sync();
    std::uint32_t inside = 0;
    std::memcpy(&inside, guest.Data() + 16, 4);
    EXPECT_EQ(inside, 0xCAFEF00Du);
    std::uint32_t before = 1;
    std::memcpy(&before, guest.Data() + 12, 4);
    EXPECT_EQ(before, 0u) << "bytes before the range are untouched";
    const auto stats = imports->Stats();
    EXPECT_EQ(stats.imports, 1u);
    EXPECT_EQ(stats.stagingBinds, 0u);
    ASSERT_EQ(tracker.Written().size(), 1u);
    EXPECT_EQ(tracker.Written()[0], std::make_pair(guest.Address() + 16, std::uint64_t{256}));
}

// Invariant: a second bind inside an existing import window reuses it (no second import).
TEST_F(HostImportTest, ImportWindowIsReused) {
    if (!CanImport()) GTEST_SKIP() << "VK_EXT_external_memory_host unavailable";
    MakeImports(64ull << 20);
    AlignedBlock guest(static_cast<std::size_t>(Alignment()) * 2, static_cast<std::size_t>(Alignment()));
    ASSERT_EQ(imports->Bind(guest.Address(), 64, GuestAccess::Read).status, BindStatus::Ok);
    const auto second = imports->Bind(guest.Address() + 128, 64, GuestAccess::Read);
    ASSERT_EQ(second.status, BindStatus::Ok);
    EXPECT_TRUE(second.binding.imported);
    const auto stats = imports->Stats();
    EXPECT_EQ(stats.imports, 1u);
    EXPECT_EQ(stats.importHits, 1u);
}

// Invariant: when any page of the import window is not tracked ReadWrite the bind stages instead (a
// refused import changes performance only); a ReadOnly window still imports for read-only binds.
TEST_F(HostImportTest, UntrackedOrReadOnlyPagesFallBackToStaging) {
    if (!CanImport()) GTEST_SKIP() << "VK_EXT_external_memory_host unavailable";
    MakeImports(64ull << 20);
    AlignedBlock guest(static_cast<std::size_t>(Alignment()) * 2, static_cast<std::size_t>(Alignment()));
    tracker.state = PortPS5::GuestMemory::PageState::ReadOnly;
    const auto write = imports->Bind(guest.Address(), 64, GuestAccess::Write);
    ASSERT_EQ(write.status, BindStatus::Ok);
    EXPECT_FALSE(write.binding.imported) << "GPU writes need ReadWrite pages";
    tracker.state = PortPS5::GuestMemory::PageState::NotGuest;
    const auto unknown = imports->Bind(guest.Address() + 256, 64, GuestAccess::Read);
    ASSERT_EQ(unknown.status, BindStatus::Ok);
    EXPECT_FALSE(unknown.binding.imported);
    tracker.state = PortPS5::GuestMemory::PageState::ReadOnly;
    const auto read = imports->Bind(guest.Address() + 512, 64, GuestAccess::Read);
    ASSERT_EQ(read.status, BindStatus::Ok);
    EXPECT_TRUE(read.binding.imported) << "read-only pages import for read-only binds";
    EXPECT_EQ(imports->Stats().importRefusals, 2u);
}

// Invariant: the budget is enforced. An import that would exceed it while an unfinished batch still uses
// the older import is refused (staged); once that batch completed, the LRU import is evicted for it.
TEST_F(HostImportTest, BudgetEvictsOnlyImportsWhoseBatchesFinished) {
    if (!CanImport()) GTEST_SKIP() << "VK_EXT_external_memory_host unavailable";
    const auto window = Alignment();
    MakeImports(window);  // room for exactly one window
    AlignedBlock first(static_cast<std::size_t>(window), static_cast<std::size_t>(window));
    AlignedBlock second(static_cast<std::size_t>(window), static_cast<std::size_t>(window));
    ASSERT_TRUE(imports->Bind(first.Address(), 64, GuestAccess::Read).binding.imported);
    // The first import is still referenced by the unfinished open batch: it cannot be evicted.
    const auto refused = imports->Bind(second.Address(), 64, GuestAccess::Read);
    ASSERT_EQ(refused.status, BindStatus::Ok);
    EXPECT_FALSE(refused.binding.imported);
    EXPECT_EQ(imports->Stats().evictions, 0u);
    EXPECT_EQ(imports->Stats().importRefusals, 1u);
    // Finish the batch that could have used the first import, then the second bind evicts it.
    recorder->Commands();
    recorder->Sync();
    const auto admitted = imports->Bind(second.Address(), 64, GuestAccess::Read);
    ASSERT_EQ(admitted.status, BindStatus::Ok);
    EXPECT_TRUE(admitted.binding.imported);
    const auto stats = imports->Stats();
    EXPECT_EQ(stats.evictions, 1u);
    EXPECT_EQ(stats.imports, 2u);
    EXPECT_EQ(stats.importedBytes, window);
}

// Invariant: a window larger than the whole budget is never imported (and never evicts others for it).
TEST_F(HostImportTest, WindowLargerThanTheBudgetStages) {
    if (!CanImport()) GTEST_SKIP() << "VK_EXT_external_memory_host unavailable";
    MakeImports(Alignment());
    AlignedBlock guest(static_cast<std::size_t>(Alignment()) * 4, static_cast<std::size_t>(Alignment()));
    const auto result = imports->Bind(guest.Address(), static_cast<std::size_t>(Alignment()) * 2, GuestAccess::Read);
    ASSERT_EQ(result.status, BindStatus::Ok);
    EXPECT_FALSE(result.binding.imported);
    EXPECT_EQ(imports->Stats().imports, 0u);
}

}
