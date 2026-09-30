// core/libs/prx/libSceAgcDriver/tests/RecorderTests.cpp
//
// GoogleTest suite for Graphics/Recorder (docs/spec/gpu-driver.md, Target design "Recorder"; ROADMAP M1
// Lane A, portps5-6). Runs on a real Vulkan device (lavapipe on hosted CI) with empty command buffers:
// the Recorder's contract is ordering, serials, pending-write tracking and labels, none of which needs
// shaders or game data. Tests are skipped when no Vulkan device exists.
#include "RecorderTestSupport.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include <gtest/gtest.h>
#include <thread>

using AgcDriver::Graphics::Recorder;
using AgcDriverTest::FakeTracker;
using AgcDriverTest::VulkanTestDevice;

namespace {

// A device-function resolver that can make vkQueueSubmit fail, to exercise Submit's failure path.
PFN_vkGetDeviceProcAddr realDeviceProc = nullptr;
std::atomic<bool> failQueueSubmit{false};
PFN_vkCmdPipelineBarrier realPipelineBarrier = nullptr;
std::atomic<int> hostStageBarriers{0};

// Forwards to the real barrier, counting those that make device writes visible to the HOST stage.
VKAPI_ATTR void VKAPI_CALL CountingPipelineBarrier(VkCommandBuffer commands, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, std::uint32_t memoryCount, const VkMemoryBarrier* memory, std::uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, std::uint32_t imageCount, const VkImageMemoryBarrier* images) {
    if ((destination & VK_PIPELINE_STAGE_HOST_BIT) != 0) ++hostStageBarriers;
    realPipelineBarrier(commands, source, destination, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
}

VKAPI_ATTR VkResult VKAPI_CALL FailingQueueSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    return VK_ERROR_DEVICE_LOST;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FaultInjectingDeviceProc(VkDevice device, const char* name) {
    if (failQueueSubmit.load() && std::strcmp(name, "vkQueueSubmit") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&FailingQueueSubmit);
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0) {
        realPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(realDeviceProc(device, name));
        return reinterpret_cast<PFN_vkVoidFunction>(&CountingPipelineBarrier);
    }
    return realDeviceProc(device, name);
}

class RecorderTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Ok()) GTEST_SKIP() << "no Vulkan device: " << device.Failure();
        Recorder::Options options;
        options.timelineSemaphores = device.Timeline();
        options.tracker = &tracker;
        recorder = std::make_unique<Recorder>(device.GetContext(), options);
    }
    void TearDown() override {
        // The Recorder must go before the device it records on.
        recorder.reset();
    }
    // Opens a batch (an empty command buffer is a valid submission) and notes the range as written.
    void RecordWrite(std::uint64_t address, std::size_t bytes) {
        const Recorder::Scope scope(*recorder);
        recorder->Commands();
        recorder->NotePendingWrite(address, bytes);
    }

    VulkanTestDevice device;
    FakeTracker tracker;
    std::unique_ptr<Recorder> recorder;
};

// Invariant: Submit numbers batches 1, 2, ...; with a timeline the serial is waitable without the lock
// and FinishUpTo then runs the completions, advancing CompletedSerial.
TEST_F(RecorderTest, SerialsAdvanceAndFinishUpToCompletes) {
    recorder->Commands();
    EXPECT_EQ(recorder->SubmitAndEpoch(), 1u);
    recorder->Commands();
    EXPECT_EQ(recorder->SubmitAndEpoch(), 2u);
    EXPECT_EQ(recorder->Submissions(), 2u);
    if (recorder->HasTimeline()) recorder->WaitSerial(2);
    recorder->FinishUpTo(1);
    EXPECT_EQ(recorder->CompletedSerial(), 1u);
    EXPECT_FALSE(recorder->Idle()) << "batch 2 must stay in flight";
    recorder->FinishUpTo(2);
    EXPECT_EQ(recorder->CompletedSerial(), 2u);
    EXPECT_TRUE(recorder->Idle());
}

// Invariant: SubmitAndEpoch with nothing recorded and nothing in flight returns 0, not a stale serial.
TEST_F(RecorderTest, SubmitAndEpochIsZeroWhenNothingWasRecorded) {
    EXPECT_EQ(recorder->SubmitAndEpoch(), 0u);
    EXPECT_TRUE(recorder->Idle());
}

// Invariant: completion actions run in submission order, each only after its own batch finished.
TEST_F(RecorderTest, CompletionsRunInSubmissionOrder) {
    std::vector<int> order;
    recorder->OnComplete([&] { order.push_back(1); });
    recorder->Submit();
    recorder->OnComplete([&] { order.push_back(2); });
    recorder->Submit();
    EXPECT_TRUE(recorder->HasCompletions());
    recorder->Sync();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_FALSE(recorder->HasCompletions());
}

// Invariant: Keep'd objects live until their batch completed and are then released (not leaked).
TEST_F(RecorderTest, KeptObjectsLiveUntilTheirBatchCompleted) {
    auto object = std::make_shared<int>(7);
    const std::weak_ptr<int> weak = object;
    recorder->Keep(std::move(object));
    recorder->Submit();
    EXPECT_FALSE(weak.expired());
    recorder->Sync();
    EXPECT_TRUE(weak.expired());
}

// Invariant: the lock-free snapshot reports exactly the noted ranges of unfinished batches and drops
// them once the batch's completions ran.
TEST_F(RecorderTest, PendingWriteSnapshotTracksUnfinishedBatches) {
    recorder->Activate();
    RecordWrite(0x10000, 0x100);
    EXPECT_TRUE(Recorder::SnapshotWriteOverlaps(0x10080, 4));
    EXPECT_TRUE(Recorder::SnapshotWriteOverlaps(0xff80, 0x100)) << "partial overlap counts";
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(0x10100, 4)) << "ranges are half-open";
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(0xff00, 0x100));
    EXPECT_TRUE(recorder->OpenWriteOverlaps(0x10000, 1));
    recorder->Submit();
    EXPECT_FALSE(recorder->OpenWriteOverlaps(0x10000, 1));
    EXPECT_TRUE(recorder->PendingWriteOverlaps(0x10000, 1));
    EXPECT_TRUE(Recorder::SnapshotWriteOverlaps(0x10000, 1));
    recorder->Sync();
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(0x10000, 1));
    EXPECT_FALSE(recorder->PendingWriteOverlaps(0x10000, 1));
}

// Invariant: noting the same range again does not lose other ranges; overlapping notes merge.
TEST_F(RecorderTest, OverlappingNotesMergeInTheSnapshot) {
    recorder->Activate();
    RecordWrite(0x20000, 0x100);
    RecordWrite(0x20080, 0x100);
    RecordWrite(0x30000, 0x10);
    EXPECT_TRUE(Recorder::SnapshotWriteOverlaps(0x20000, 0x180));
    EXPECT_TRUE(Recorder::SnapshotWriteOverlaps(0x30008, 1));
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(0x20180, 0x100));
    recorder->Sync();
}

// Invariant: SyncThrough finishes only up to the NEWEST batch that writes the range; later batches stay
// in flight (the point of targeted syncs).
TEST_F(RecorderTest, SyncThroughFinishesOnlyUpToTheTargetBatch) {
    RecordWrite(0x40000, 0x10);
    recorder->Submit();  // serial 1
    RecordWrite(0x50000, 0x10);
    recorder->Submit();  // serial 2
    recorder->SyncThrough(0x40000, 0x10);
    EXPECT_EQ(recorder->CompletedSerial(), 1u);
    EXPECT_FALSE(recorder->Idle());
    EXPECT_FALSE(recorder->PendingWriteOverlaps(0x40000, 0x10));
    EXPECT_TRUE(recorder->PendingWriteOverlaps(0x50000, 0x10));
    recorder->Sync();
}

// Invariant: a range written by the OPEN batch makes SyncThrough submit it (the GPU cannot finish what
// was never submitted) and wait; nothing remains in flight.
TEST_F(RecorderTest, SyncThroughSubmitsTheOpenBatchThatWritesTheRange) {
    RecordWrite(0x60000, 0x10);
    recorder->SyncThrough(0x60000, 0x10);
    EXPECT_TRUE(recorder->Idle());
    EXPECT_EQ(recorder->CompletedSerial(), 1u);
}

// Invariant: SyncThrough on a range nothing writes is a no-op (no submission, no wait).
TEST_F(RecorderTest, SyncThroughIgnoresUnwrittenRanges) {
    RecordWrite(0x70000, 0x10);
    recorder->SyncThrough(0x90000, 0x10);
    EXPECT_TRUE(recorder->Recording());
    EXPECT_EQ(recorder->Submissions(), 0u);
    recorder->Sync();
}

// Invariant: the unlocked wait (hook path) yields the same result as the locked one: the written
// batch's completion has run when SyncThrough returns.
TEST_F(RecorderTest, UnlockedSyncThroughRunsTheCompletion) {
    bool ran = false;
    RecordWrite(0x80000, 0x10);
    recorder->OnComplete([&] { ran = true; });
    recorder->Submit();
    recorder->SyncThrough(0x80000, 0x10, true);
    EXPECT_TRUE(ran);
    EXPECT_TRUE(recorder->Idle());
}

// Invariant: a CPU access through the tracker (Collect runs the flush hook) to memory that recorded GPU
// work writes waits for that work and its write-back first; an unrelated range does not wait.
TEST_F(RecorderTest, FlushHookWaitsForPendingWritesOnly) {
    recorder->Activate(&tracker);
    bool written_back = false;
    RecordWrite(0xA0000, 0x100);
    recorder->OnComplete([&] { written_back = true; });
    recorder->Submit();
    tracker.Collect(0xC0000, 0x100);
    EXPECT_FALSE(written_back) << "an access overlapping nothing must not wait";
    tracker.Collect(0xA0040, 4);
    EXPECT_TRUE(written_back);
    EXPECT_TRUE(recorder->Idle());
}

// Invariant: a flush-hook sync made from inside a completion action is skipped, so the completing
// batch's store lands before later batches' writes (hardware order) and the completion cannot deadlock.
TEST_F(RecorderTest, SyncFromInsideACompletionDoesNotWaitForLaterBatches) {
    RecordWrite(0xB0000, 0x10);
    bool completionRan = false;
    recorder->OnComplete([&] {
        recorder->SyncThrough(0xB8000, 0x10);  // a later batch writes this range
        completionRan = true;
    });
    recorder->Submit();  // serial 1
    RecordWrite(0xB8000, 0x10);
    recorder->Submit();  // serial 2
    recorder->SyncThrough(0xB0000, 0x10);
    EXPECT_TRUE(completionRan);
    EXPECT_EQ(recorder->CompletedSerial(), 1u);
    EXPECT_FALSE(recorder->Idle()) << "batch 2 stays in flight";
    recorder->Sync();
}

// Invariant: Reap completes only batches whose fence already signaled, and reports when nothing remains.
TEST_F(RecorderTest, ReapRetiresFinishedBatches) {
    bool ran = false;
    recorder->OnComplete([&] { ran = true; });
    recorder->Submit();
    // The GPU finishes an empty batch quickly but not instantly: poll with a bounded deadline.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!recorder->Reap() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(recorder->Idle());
}

// Invariant: labels are found per 4-byte dword with 4- and 8-byte lookups, only when their stamp is
// NEWER than the waiter's, never when misaligned or partially present; entries leave with their batch.
TEST_F(RecorderTest, LabelTableLookupRules) {
    recorder->Activate();
    const std::uint64_t value = 0x1122334455667788ull;
    std::array<std::byte, 8> bytes{};
    std::memcpy(bytes.data(), &value, 8);
    recorder->NoteLabel(0xD0000, bytes, 10, 3);
    std::uint32_t queue = 0;
    EXPECT_EQ(recorder->PendingLabel(0xD0000, 8, 9, queue), value);
    EXPECT_EQ(queue, 3u);
    EXPECT_EQ(recorder->PendingLabel(0xD0000, 4, 9, queue), value & 0xffffffffu);
    EXPECT_EQ(recorder->PendingLabel(0xD0004, 4, 9, queue), value >> 32u);
    EXPECT_FALSE(recorder->PendingLabel(0xD0000, 8, 10, queue).has_value()) << "an equal stamp is not newer";
    EXPECT_FALSE(recorder->PendingLabel(0xD0002, 4, 0, queue).has_value()) << "misaligned";
    EXPECT_FALSE(recorder->PendingLabel(0xD0000, 12, 0, queue).has_value()) << "only 4- and 8-byte labels";
    EXPECT_FALSE(recorder->PendingLabel(0xD0004, 8, 0, queue).has_value()) << "dword 2 is absent";
    EXPECT_TRUE(recorder->PendingLabelIn(0xCFFFC, 8));
    EXPECT_FALSE(recorder->PendingLabelIn(0xD0008, 64));
    EXPECT_EQ(Recorder::LookupLabel(0xD0000, 8, 9, queue), value) << "static lookup serves pollers";
    EXPECT_TRUE(Recorder::PendingLabelSince().has_value());
    recorder->Sync();
    EXPECT_EQ(recorder->PendingLabels(), 0u);
    EXPECT_FALSE(recorder->PendingLabel(0xD0000, 8, 0, queue).has_value());
    EXPECT_FALSE(Recorder::PendingLabelSince().has_value());
}

// Invariant: a later label to the same dword replaces the older entry, and the older batch finishing
// must not erase the newer entry.
TEST_F(RecorderTest, NewerLabelSurvivesTheOlderBatchFinishing) {
    std::array<std::byte, 4> first{std::byte{1}};
    std::array<std::byte, 4> second{std::byte{2}};
    recorder->NoteLabel(0xE0000, first, 1, 0);
    recorder->Submit();
    recorder->NoteLabel(0xE0000, second, 2, 0);
    recorder->FinishUpTo(1);
    std::uint32_t queue = 0;
    EXPECT_EQ(recorder->PendingLabel(0xE0000, 4, 0, queue), 2u);
    recorder->Sync();
}

// Invariant: AfterCompletions lands the label in guest memory only after the batch finished, reports the
// store to the tracker, and counts as a pending completion label until then.
TEST_F(RecorderTest, AfterCompletionsStoresTheLabelOnCompletion) {
    recorder->Activate();
    AgcDriverTest::AlignedBlock memory(4096, 4096);
    const auto address = memory.Address();
    recorder->Commands();
    std::array<std::byte, 4> label{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}, std::byte{0xDD}};
    recorder->AfterCompletions(address, label, 5, 1, false);
    EXPECT_EQ(Recorder::PendingCompletionLabels(), 1u);
    std::uint32_t before = 1;
    std::memcpy(&before, memory.Data(), 4);
    EXPECT_EQ(before, 0u) << "nothing is stored before the batch completed";
    recorder->Sync();
    std::uint32_t after = 0;
    std::memcpy(&after, memory.Data(), 4);
    EXPECT_EQ(after, 0xDDCCBBAAu);
    EXPECT_EQ(Recorder::PendingCompletionLabels(), 0u);
    ASSERT_EQ(tracker.Written().size(), 1u);
    EXPECT_EQ(tracker.Written()[0], std::make_pair(address, std::uint64_t{4}));
}

// Invariant: a label the GPU already stored (storedOnGpu) is NOT re-stored by the completion unless a
// CPU write-back overlapped it meanwhile: an unconditional store could land a stale value on memory the
// game reused (the upstream bug this rule fixes).
TEST_F(RecorderTest, GpuStoredLabelIsNotRestoredUnlessWrittenBack) {
    recorder->Activate();
    AgcDriverTest::AlignedBlock memory(4096, 4096);
    const auto address = memory.Address();
    std::array<std::byte, 4> label{std::byte{0x11}, std::byte{0}, std::byte{0}, std::byte{0}};
    recorder->Commands();
    recorder->AfterCompletions(address, label, 1, 0, true);
    recorder->AfterCompletions(address + 64, label, 2, 0, true);
    Recorder::NoteWrittenBack(address + 64, 4);  // a write-back overlapped the second label only
    recorder->Sync();
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::memcpy(&first, memory.Data(), 4);
    std::memcpy(&second, memory.Data() + 64, 4);
    EXPECT_EQ(first, 0u) << "no overlapping write-back: the GPU's own store stands";
    EXPECT_EQ(second, 0x11u) << "overlapping write-back: the label is re-stored after it";
}

// Invariant: AfterCompletions appends to the newest in-flight batch when none is open (no empty
// submission), and Sync still lands it.
TEST_F(RecorderTest, AfterCompletionsUsesTheNewestInFlightBatch) {
    AgcDriverTest::AlignedBlock memory(4096, 4096);
    recorder->Commands();
    recorder->Submit();
    std::array<std::byte, 4> label{std::byte{0x42}, std::byte{0}, std::byte{0}, std::byte{0}};
    recorder->AfterCompletions(memory.Address(), label, 1, 0, false);
    EXPECT_FALSE(recorder->Recording());
    EXPECT_EQ(recorder->Submissions(), 1u);
    recorder->Sync();
    std::uint32_t value = 0;
    std::memcpy(&value, memory.Data(), 4);
    EXPECT_EQ(value, 0x42u);
}

// Invariant: a completion whose target range is not mapped is reported and skipped; the other
// completions of the batch still run and the pending-label count returns to zero (no reap loop forever).
TEST_F(RecorderTest, FailingCompletionDoesNotBlockOthersOrLeakLabelCount) {
    recorder->Activate();
    bool later = false;
    recorder->Commands();
    std::array<std::byte, 4> label{};
    recorder->AfterCompletions(0x10, label, 1, 0, false);  // unmapped: CheckRange fails
    recorder->OnComplete([&] { later = true; });
    recorder->Sync();
    EXPECT_TRUE(later);
    EXPECT_EQ(Recorder::PendingCompletionLabels(), 0u);
}

// Invariant: AfterCompletions with no batch at all is a caller error reported, not a silent drop.
TEST_F(RecorderTest, AfterCompletionsWithoutABatchIsRejected) {
    std::array<std::byte, 4> label{};
    EXPECT_ANY_THROW(recorder->AfterCompletions(0x1000, label, 1, 0, false));
}

// Invariant: CountRecordedWork accumulates on the active recorder and Submit resets it, even with no
// open batch (a stale count would make callers take the lock for nothing).
TEST_F(RecorderTest, RecordedWorkCountResetsOnSubmit) {
    recorder->Activate();
    Recorder::CountRecordedWork();
    Recorder::CountRecordedWork();
    EXPECT_EQ(Recorder::RecordedWorkSinceSubmit(), 2u);
    recorder->Submit();
    EXPECT_EQ(Recorder::RecordedWorkSinceSubmit(), 0u);
}

// Invariant: WriteGeneration moves with every noted pending write, so pollers re-check labels.
TEST_F(RecorderTest, WriteGenerationAdvancesOnNotes) {
    recorder->Activate();
    const auto before = Recorder::WriteGeneration();
    RecordWrite(0xF0000, 8);
    EXPECT_GT(Recorder::WriteGeneration(), before);
    recorder->Sync();
}

// Invariant: destroying the active recorder clears the statics (no dangling reads), and a destroyed
// recorder with work in flight syncs it first (its completion still runs).
TEST_F(RecorderTest, TeardownSyncsAndDeactivates) {
    recorder->Activate();
    bool ran = false;
    RecordWrite(0x110000, 8);
    recorder->OnComplete([&] { ran = true; });
    recorder->Submit();
    recorder.reset();
    EXPECT_TRUE(ran);
    EXPECT_EQ(Recorder::Active(), nullptr);
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(0x110000, 8));
    EXPECT_EQ(Recorder::WriteGeneration(), 0u);
}

// Review finding (PR #49, gitar): a failing vkQueueSubmit must not leak the batch accounting.
// Invariant: after Submit throws, the batch's completion labels no longer count as pending, its label
// entries and written ranges are gone, nothing is left open or in flight, and the recorder still works.
TEST_F(RecorderTest, FailedSubmitUndoesLabelAccountingAndSnapshot) {
    auto faulty = device.GetContext();
    realDeviceProc = faulty.deviceProc;
    faulty.deviceProc = &FaultInjectingDeviceProc;
    Recorder::Options options;
    options.timelineSemaphores = device.Timeline();
    options.tracker = &tracker;
    Recorder local(faulty, options);
    local.Activate();
    AgcDriverTest::AlignedBlock memory(4096, 4096);
    const auto address = memory.Address();
    local.Commands();
    std::array<std::byte, 4> label{std::byte{1}};
    local.AfterCompletions(address, label, 1, 0, false);
    ASSERT_EQ(Recorder::PendingCompletionLabels(), 1u);
    ASSERT_EQ(local.PendingLabels(), 1u);
    ASSERT_TRUE(Recorder::SnapshotWriteOverlaps(address, 4));
    failQueueSubmit = true;
    EXPECT_ANY_THROW(local.Submit());
    failQueueSubmit = false;
    EXPECT_EQ(Recorder::PendingCompletionLabels(), 0u) << "a label that will never land must not keep workers reaping";
    EXPECT_EQ(local.PendingLabels(), 0u);
    EXPECT_FALSE(Recorder::SnapshotWriteOverlaps(address, 4));
    EXPECT_FALSE(Recorder::PendingLabelSince().has_value()) << "the failed batch's label age must not leak into the next batch";
    EXPECT_TRUE(local.Idle());
    // The pooled command buffer and fence were returned, so the recorder keeps working.
    local.Commands();
    EXPECT_EQ(local.SubmitAndEpoch(), 1u);
    local.Sync();
    EXPECT_TRUE(local.Idle());
}

// Review finding (PR #49, coderabbit): completions read GPU results on the CPU, and a fence wait alone does
// not make device writes visible to the host. Invariant: a batch that wrote guest ranges (or carries
// completions) ends with a memory barrier into the HOST stage; a batch with neither needs none.
TEST_F(RecorderTest, SubmitMakesDeviceWritesVisibleToTheHost) {
    auto counting = device.GetContext();
    realDeviceProc = counting.deviceProc;
    counting.deviceProc = &FaultInjectingDeviceProc;
    Recorder::Options options;
    options.timelineSemaphores = device.Timeline();
    Recorder local(counting, options);
    hostStageBarriers = 0;
    local.Commands();
    local.Submit();
    EXPECT_EQ(hostStageBarriers.load(), 0) << "no writes and no completions: nothing to make visible";
    local.NotePendingWrite(0x300000, 16);
    local.Submit();
    EXPECT_EQ(hostStageBarriers.load(), 1);
    local.OnComplete([] {});
    local.Submit();
    EXPECT_EQ(hostStageBarriers.load(), 2);
    local.Sync();
}

// Invariant (concurrency): many threads noting writes, submitting and syncing through the flush hook
// never deadlock or lose a completion. Each completion must run exactly once.
TEST_F(RecorderTest, ConcurrentRecordersAndHookReadersComplete) {
    recorder->Activate(&tracker);
    std::atomic<int> completions{0};
    constexpr int Threads = 4;
    constexpr int Rounds = 50;
    std::vector<std::thread> threads;
    for (int t = 0; t < Threads; ++t) {
        threads.emplace_back([&, t] {
            const std::uint64_t base = 0x200000ull + static_cast<std::uint64_t>(t) * 0x10000ull;
            for (int i = 0; i < Rounds; ++i) {
                {
                    const Recorder::Scope scope(*recorder);
                    recorder->Commands();
                    recorder->NotePendingWrite(base, 64);
                    recorder->OnComplete([&] { completions.fetch_add(1); });
                    recorder->Submit();
                }
                tracker.Collect(base, 64);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    recorder->Sync();
    EXPECT_EQ(completions.load(), Threads * Rounds);
    EXPECT_TRUE(recorder->Idle());
}

}
