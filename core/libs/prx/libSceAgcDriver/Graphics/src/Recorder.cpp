// core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp
//
// Recorder implementation (see Recorder.hpp and docs/spec/gpu-driver.md). Adapted from AnyPS5 8a69fefe
// Graphics/src/Recorder.cpp: the profiling tables, the environment-variable switches, the GpuMutex coupling and the
// release thread are gone; the batching, serial/timeline, pending-write snapshot, label table and
// completion-ordering logic is kept.
//
// Invariants:
//  - `mutex` (recursive) guards batches, serials and the label table's writers. `depth` is only touched
//    while holding it, so depth == 1 identifies the outermost hold (the only one that may release the
//    lock around a GPU wait).
//  - A batch's written ranges stay in the published snapshot until its completions ran (`finishing`),
//    so a reader that sees no overlap either precedes the note or follows the write-back.
//  - Completions run front-to-back in serial order; a flush-hook sync made from inside one is skipped
//    (every batch still in flight was recorded after the completing one).
//  - Kept objects and completion closures are destroyed after the thread's outermost Scope ended.
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libc/include/HostThreadLocal.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>
#include <utility>
#include <tuple>

namespace AgcDriver::Graphics {

namespace {

constexpr std::int64_t NoPendingLabel = std::numeric_limits<std::int64_t>::min();
constexpr std::uint64_t WriteBackRing = 16384;
constexpr std::size_t SparePool = 64;
constexpr std::uint64_t WaitSliceNs = 5'000'000'000ull;

// The active recorder. `activeMutex` only guards the pointer; a user count on the recorder (hookUsers)
// keeps it alive across a static reader, and ~Recorder waits for it to reach zero.
std::mutex activeMutex;
Recorder* activeRecorder = nullptr;

// Completion actions in progress on this thread: a pending-write sync from inside one is skipped.
thread_local int completionDepth = 0;
// Scopes held on this thread across all recorders; the outermost one releases deferred objects.
thread_local int threadHolds = 0;

struct DeferredBatch {
    std::vector<std::shared_ptr<void>> kept;
    std::vector<std::function<void()>> completions;
};
// The per-thread list of finished batches whose objects still have to be destroyed. HostThreadLocal (FLS
// cleanup on Windows) instead of a thread_local std::vector: a thread_local with a non-trivial destructor
// ran at thread exit through libc's TLS hooks and corrupted the vector's storage when a worker exited
// (AnyPS5 e424b6b hit the same crash in the PR #5 Recorder).
struct DeferredBatchesTag {};
std::vector<DeferredBatch>& DeferredBatches() { return HostThreadLocal<std::vector<DeferredBatch>, DeferredBatchesTag>(); }

void DestroyDeferred() noexcept {
    try {
        // Moved out first: a destructor that opened a Scope of its own must find the list empty.
        auto releasing = std::move(DeferredBatches());
        DeferredBatches().clear();
        for (auto& batch : releasing) {
            // Completion closures first (their captures hold some of the objects), then the objects.
            batch.completions.clear();
            batch.kept.clear();
        }
    } catch (...) {
        // Slot exhaustion only (HostThreadLocal debt); nothing to release then.
    }
}

bool SnapshotOverlapsRanges(const std::vector<std::pair<std::uint64_t, std::uint64_t>>* ranges, std::uint64_t address, std::size_t bytes) {
    if (bytes == 0 || ranges == nullptr || ranges->empty()) return false;
    // Merged ranges are ordered by both bounds: the first one ending past the access decides.
    const auto it = std::partition_point(ranges->begin(), ranges->end(), [&](const auto& range) { return range.second <= address; });
    return it != ranges->end() && it->first < address + bytes;
}

// Timeline wait on handles copied out of the recorder. A hung batch is reported every 5 s.
VkResult WaitTimeline(VkDevice device, VkSemaphore timeline, PFN_vkWaitSemaphoresKHR waitSemaphores, std::uint64_t serial) {
    VkSemaphoreWaitInfoKHR wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline;
    wait.pValues = &serial;
    auto result = waitSemaphores(device, &wait, WaitSliceNs);
    for (int waited = 5; result == VK_TIMEOUT; waited += 5) {
        std::fprintf(stderr, "[gpu] recorded batch %llu still running on the GPU after %d s (timeline wait)\n", static_cast<unsigned long long>(serial), waited);
        result = waitSemaphores(device, &wait, WaitSliceNs);
    }
    return result;
}

struct Waiter {
    std::atomic<int>& count;
    explicit Waiter(std::atomic<int>& c) : count(c) { count.fetch_add(1, std::memory_order_acq_rel); }
    ~Waiter() { count.fetch_sub(1, std::memory_order_acq_rel); }
    Waiter(const Waiter&) = delete;
    Waiter& operator=(const Waiter&) = delete;
};

}

Recorder::Scope::Scope(const Recorder& r) : recorder(r) {
    recorder.mutex.lock();
    ++recorder.depth;
    ++threadHolds;
}

Recorder::Scope::~Scope() {
    --recorder.depth;
    const bool outermost = --threadHolds == 0;
    recorder.mutex.unlock();
    // After the unlock: kept objects never die under a Recorder hold.
    if (outermost) DestroyDeferred();
}

Recorder::Recorder(const Context& ctx, const Options& opts) : context(ctx), options(opts), pendingLabelSince_(NoPendingLabel) {
    snapshot.store(nullptr);
    if (!options.timelineSemaphores) return;
    // The timeline starts at 0 and every Submit signals its serial: a value that only grows, so a
    // thread waiting outside the lock never sees a reset or recycled handle (batch fences are pooled
    // and reset in release()). A failure only disables the unlocked waits.
    VkSemaphoreTypeCreateInfoKHR type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    type.initialValue = 0;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (context.Function<PFN_vkCreateSemaphore>("vkCreateSemaphore")(context.device, &info, nullptr, &timeline) != VK_SUCCESS) {
        std::fprintf(stderr, "[gpu] timeline semaphore creation failed; drains wait under the recorder lock\n");
        timeline = VK_NULL_HANDLE;
    }
}

Recorder::~Recorder() {
    {
        std::lock_guard lock(activeMutex);
        if (activeRecorder == this) activeRecorder = nullptr;
    }
    // Static readers and flush hooks that already pinned this recorder finish before it goes.
    while (hookUsers.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    try {
        Sync();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[gpu] recorder teardown: %s\n", error.what());
    }
    // Sync() completed every batch, so each unlocked timeline wait returns; wait until they left.
    while (unlockedWaiters.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    for (auto& [commands, fence] : spare) {
        context.Function<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(context.device, context.pool, 1, &commands);
        context.Function<PFN_vkDestroyFence>("vkDestroyFence")(context.device, fence, nullptr);
    }
    spare.clear();
    if (timeline != VK_NULL_HANDLE) context.Function<PFN_vkDestroySemaphore>("vkDestroySemaphore")(context.device, timeline, nullptr);
    timeline = VK_NULL_HANDLE;
    DestroyDeferred();
}

Recorder* Recorder::Active() {
    std::lock_guard lock(activeMutex);
    return activeRecorder;
}

// Static readers go through this helper: pins the active recorder so it cannot be destroyed mid-read.
#define AGC_WITH_ACTIVE(ref)                                                                                       \
    Recorder* ref = nullptr;                                                                                       \
    std::atomic<int>* ref##Users = nullptr;                                                                        \
    {                                                                                                              \
        std::lock_guard activeLock(activeMutex);                                                                   \
        ref = activeRecorder;                                                                                      \
        if (ref != nullptr) {                                                                                      \
            ref##Users = &ref->hookUsers;                                                                          \
            ref##Users->fetch_add(1, std::memory_order_acq_rel);                                                   \
        }                                                                                                          \
    }                                                                                                              \
    struct ref##Unpin {                                                                                            \
        std::atomic<int>* users;                                                                                   \
        ~ref##Unpin() {                                                                                            \
            if (users != nullptr) users->fetch_sub(1, std::memory_order_acq_rel);                                  \
        }                                                                                                          \
    } ref##Guard{ref##Users}

std::optional<std::chrono::steady_clock::time_point> Recorder::PendingLabelSince() {
    AGC_WITH_ACTIVE(recorder);
    if (recorder == nullptr) return std::nullopt;
    const auto since = recorder->pendingLabelSince_.load(std::memory_order_acquire);
    if (since == NoPendingLabel) return std::nullopt;
    return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(since));
}

std::uint64_t Recorder::WriteGeneration() {
    AGC_WITH_ACTIVE(recorder);
    return recorder != nullptr ? recorder->writeGeneration_.load(std::memory_order_acquire) : 0;
}

std::uint64_t Recorder::PendingCompletionLabels() {
    AGC_WITH_ACTIVE(recorder);
    return recorder != nullptr ? recorder->completionLabels_.load(std::memory_order_acquire) : 0;
}

std::uint64_t Recorder::RecordedWorkSinceSubmit() {
    AGC_WITH_ACTIVE(recorder);
    return recorder != nullptr ? recorder->workSinceSubmit_.load(std::memory_order_relaxed) : 0;
}

void Recorder::CountRecordedWork() {
    AGC_WITH_ACTIVE(recorder);
    if (recorder != nullptr) recorder->workSinceSubmit_.fetch_add(1, std::memory_order_relaxed);
}

bool Recorder::SnapshotWriteOverlaps(std::uint64_t address, std::size_t bytes) {
    AGC_WITH_ACTIVE(recorder);
    if (recorder == nullptr) return false;
    const auto ranges = recorder->snapshot.load(std::memory_order_acquire);
    return SnapshotOverlapsRanges(ranges.get(), address, bytes);
}

std::optional<std::uint64_t> Recorder::LookupLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue) {
    AGC_WITH_ACTIVE(recorder);
    if (recorder == nullptr) return std::nullopt;
    std::lock_guard tableLock(recorder->labelMutex);
    return recorder->lookupLabel(address, bytes, afterStamp, queue);
}

/**
 * Flush hook registered on the tracker: a CPU access to memory that recorded GPU work will write waits
 * for that work first. Lock-free no-overlap fast path; SyncThrough takes the recorder lock itself so its
 * wait can run unlocked when this is the outermost hold on the thread. Runs with no tracker lock held
 * (IWriteTracker contract).
 */
void Recorder::FlushForAccess(void*, std::uint64_t address, std::uint64_t bytes) {
    AGC_WITH_ACTIVE(recorder);
    if (recorder == nullptr) return;
    const auto ranges = recorder->snapshot.load(std::memory_order_acquire);
    if (!SnapshotOverlapsRanges(ranges.get(), address, static_cast<std::size_t>(bytes))) return;
    if (recorder->PendingWriteOverlaps(address, static_cast<std::size_t>(bytes))) recorder->SyncThrough(address, static_cast<std::size_t>(bytes), true);
}

void Recorder::Activate(PortPS5::GuestMemory::IWriteTracker* tracker) {
    {
        std::lock_guard lock(activeMutex);
        activeRecorder = this;
    }
    if (tracker != nullptr) tracker->SetFlushHook(&FlushForAccess, nullptr);
}

bool Recorder::Recording() const {
    const Scope scope(*this);
    return open != nullptr;
}

bool Recorder::Idle() const {
    const Scope scope(*this);
    return open == nullptr && inFlight.empty();
}

std::uint64_t Recorder::Submissions() const {
    const Scope scope(*this);
    return submissions;
}

std::uint64_t Recorder::CompletedSerial() const {
    return completedSerial.load(std::memory_order_acquire);
}

void Recorder::startBatch() {
    if (open != nullptr) return;
    auto batch = std::make_unique<Batch>();
    try {
        if (!spare.empty()) {
            // The pool resets command buffers on begin; the fence was reset when the batch completed.
            std::tie(batch->commands, batch->fence) = spare.back();
            spare.pop_back();
        } else {
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool = context.pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            Check(context.Function<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(context.device, &allocation, &batch->commands), "vkAllocateCommandBuffers recorder");
            VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Check(context.Function<PFN_vkCreateFence>("vkCreateFence")(context.device, &info, nullptr, &batch->fence), "vkCreateFence recorder");
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(context.Function<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(batch->commands, &begin), "vkBeginCommandBuffer recorder");
    } catch (...) {
        release(*batch);
        throw;
    }
    open = std::move(batch);
}

VkCommandBuffer Recorder::Commands() {
    const Scope scope(*this);
    startBatch();
    return open->commands;
}

void Recorder::Keep(std::shared_ptr<void> object) {
    const Scope scope(*this);
    startBatch();
    open->kept.push_back(std::move(object));
}

void Recorder::OnComplete(std::function<void()> action) {
    const Scope scope(*this);
    startBatch();
    open->completions.push_back(std::move(action));
}

bool Recorder::snapshotCovers(std::uint64_t address, std::uint64_t end) const {
    const auto ranges = snapshot.load(std::memory_order_acquire);
    if (ranges == nullptr || ranges->empty()) return false;
    const auto it = std::partition_point(ranges->begin(), ranges->end(), [&](const auto& range) { return range.second <= address; });
    return it != ranges->end() && it->first <= address && end <= it->second;
}

bool Recorder::noteWrite(std::uint64_t address, std::size_t bytes) {
    if (bytes == 0) return false;
    startBatch();
    const auto end = address + bytes;
    open->writes.emplace_back(address, end);
    // A poller waiting on this range learns that the open batch may now hold its producer.
    writeGeneration_.fetch_add(1, std::memory_order_release);
    // Consecutive dispatches write the same output buffers: a range the snapshot already contains
    // leaves the published union unchanged, so the rebuild is skipped.
    return !snapshotCovers(address, end);
}

void Recorder::noteWriteOn(Batch& batch, std::uint64_t address, std::size_t bytes) {
    if (bytes == 0) return;
    if (&batch == open.get()) {
        NotePendingWrite(address, bytes);
        return;
    }
    // An in-flight batch: its range joins the snapshot at once (the completion that stores it runs when
    // the batch finishes, and the hook must sync for a CPU read until then).
    batch.writes.emplace_back(address, address + bytes);
    writeGeneration_.fetch_add(1, std::memory_order_release);
    if (!snapshotCovers(address, address + bytes)) publishPendingWrites();
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void Recorder::NotePendingWrite(std::uint64_t address, std::size_t bytes) {
    const Scope scope(*this);
    if (!noteWrite(address, bytes)) return;
    publishPendingWrites();
    // The note must be visible before this thread's vkQueueSubmit and the label another queue polls for;
    // the fence makes that order hold without relying on x86 store ordering.
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void Recorder::NotePendingWrites(std::span<const std::pair<std::uint64_t, std::uint64_t>> ranges) {
    const Scope scope(*this);
    bool publish = false;
    for (const auto& [begin, end] : ranges) {
        if (end > begin && noteWrite(begin, static_cast<std::size_t>(end - begin))) publish = true;
    }
    if (!publish) return;
    publishPendingWrites();
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void Recorder::publishPendingWrites() const {
    auto merged = std::make_shared<WriteRanges>();
    if (open != nullptr) merged->insert(merged->end(), open->writes.begin(), open->writes.end());
    for (const auto& batch : inFlight) merged->insert(merged->end(), batch->writes.begin(), batch->writes.end());
    for (const auto* batch : finishing) merged->insert(merged->end(), batch->writes.begin(), batch->writes.end());
    std::sort(merged->begin(), merged->end());
    std::size_t out = 0;
    for (const auto& [begin, end] : *merged) {
        if (out != 0 && begin <= (*merged)[out - 1].second) (*merged)[out - 1].second = std::max((*merged)[out - 1].second, end);
        else (*merged)[out++] = {begin, end};
    }
    merged->resize(out);
    snapshot.store(std::shared_ptr<const WriteRanges>(std::move(merged)), std::memory_order_release);
}

bool Recorder::overlaps(const Batch& batch, std::uint64_t address, std::uint64_t end) {
    for (const auto& [begin, finish] : batch.writes) {
        if (address < finish && begin < end) return true;
    }
    return false;
}

bool Recorder::PendingWriteOverlaps(std::uint64_t address, std::size_t bytes) const {
    if (bytes == 0) return false;
    const Scope scope(*this);
    const auto end = address + bytes;
    if (open != nullptr && overlaps(*open, address, end)) return true;
    return std::any_of(inFlight.begin(), inFlight.end(), [&](const auto& batch) { return overlaps(*batch, address, end); });
}

bool Recorder::OpenWriteOverlaps(std::uint64_t address, std::size_t bytes) const {
    const Scope scope(*this);
    return bytes != 0 && open != nullptr && overlaps(*open, address, address + bytes);
}

bool Recorder::HasCompletions() const {
    const Scope scope(*this);
    if (open != nullptr && !open->completions.empty()) return true;
    return std::any_of(inFlight.begin(), inFlight.end(), [](const auto& batch) { return !batch->completions.empty(); });
}

void Recorder::noteLabelOn(Batch& batch, std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue) {
    // One entry per dword; a label overlapping older entries (a 4-byte store inside an 8-byte one or
    // the reverse) replaces exactly the dwords it stores, so a lookup composes what memory will hold.
    std::lock_guard tableLock(labelMutex);
    for (std::size_t offset = 0; offset + 4 <= bytes.size(); offset += 4) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, 4);
        const auto dword = address + offset;
        labels.insert_or_assign(dword, LabelEntry{value, queue, stamp, &batch});
        batch.labelDwords.push_back(dword);
    }
}

void Recorder::NoteLabel(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue) {
    const Scope scope(*this);
    startBatch();
    noteLabelOn(*open, address, bytes, stamp, queue);
    if (pendingLabelSince_.load(std::memory_order_relaxed) == NoPendingLabel) {
        pendingLabelSince_.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_release);
    }
}

std::optional<std::uint64_t> Recorder::lookupLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue) const {
    // Only whole aligned 4- or 8-byte stores are trusted; every dword must carry a newer stamp.
    if (labels.empty() || (bytes != 4 && bytes != 8) || address % 4 != 0) return std::nullopt;
    std::uint64_t value = 0;
    for (std::size_t offset = 0; offset < bytes; offset += 4) {
        const auto found = labels.find(address + offset);
        if (found == labels.end() || found->second.stamp <= afterStamp) return std::nullopt;
        value |= static_cast<std::uint64_t>(found->second.value) << (offset * 8u);
        if (offset == 0) queue = found->second.queue;
    }
    return value;
}

std::optional<std::uint64_t> Recorder::PendingLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue) const {
    std::lock_guard tableLock(labelMutex);
    return lookupLabel(address, bytes, afterStamp, queue);
}

std::size_t Recorder::PendingLabels() const {
    std::lock_guard tableLock(labelMutex);
    return labels.size();
}

bool Recorder::PendingLabelIn(std::uint64_t address, std::size_t bytes) const {
    std::lock_guard tableLock(labelMutex);
    if (labels.empty() || bytes == 0) return false;
    const auto end = address + bytes;
    return std::any_of(labels.begin(), labels.end(), [&](const auto& entry) { return entry.first >= address && entry.first < end; });
}

void Recorder::AfterCompletions(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue, bool storedOnGpu) {
    const Scope scope(*this);
    Require(open != nullptr || !inFlight.empty(), "no batch to append a completion label to");
    Batch& batch = open != nullptr ? *open : *inFlight.back();
    std::vector<std::byte> copy(bytes.begin(), bytes.end());
    const auto sequence = [&] {
        std::lock_guard ringLock(writtenBackMutex);
        return writtenBackSequence;
    }();
    auto* tracker = options.tracker;
    batch.completions.push_back([this, tracker, address, copy = std::move(copy), sequence, storedOnGpu] {
        // A GPU-stored label is re-stored only if a CPU write-back overlapped it since: an
        // unconditional store could land a stale value on memory the game already reused.
        if (storedOnGpu && !writtenBackSince(sequence, address, address + copy.size())) return;
        // The store bypasses the flush hook (it runs inside finish(): the hook would find this very
        // range noted and sync re-entrantly) but still validates the untrusted guest pointer.
        GuestMemory::CheckRange(reinterpret_cast<const void*>(address), copy.size(), 4, true);
        std::memcpy(reinterpret_cast<void*>(address), copy.data(), copy.size());
        if (tracker != nullptr) tracker->MarkWritten(address, copy.size());
    });
    // Counted per batch and counted down when the batch finishes on every exit path of finish(): a label
    // that will never land must not leave the workers reaping forever.
    ++batch.completionLabelCount;
    completionLabels_.fetch_add(1, std::memory_order_acq_rel);
    // Table entry before the write note: the note bumps the generation a poller watches, and a poller
    // that sees the bump then finds the label without the recorder lock.
    noteLabelOn(batch, address, bytes, stamp, queue);
    noteWriteOn(batch, address, bytes.size());
    if (&batch == open.get() && pendingLabelSince_.load(std::memory_order_relaxed) == NoPendingLabel) {
        pendingLabelSince_.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_release);
    }
}

void Recorder::NoteWrittenBack(std::uint64_t address, std::size_t bytes) {
    AGC_WITH_ACTIVE(recorder);
    if (recorder == nullptr || bytes == 0) return;
    std::lock_guard ringLock(recorder->writtenBackMutex);
    recorder->writtenBack.push_back({++recorder->writtenBackSequence, address, address + bytes});
    while (recorder->writtenBack.size() > WriteBackRing) recorder->writtenBack.pop_front();
}

bool Recorder::writtenBackSince(std::uint64_t sequence, std::uint64_t begin, std::uint64_t end) {
    std::lock_guard ringLock(writtenBackMutex);
    if (writtenBack.empty() || writtenBack.back()[0] <= sequence) return false;
    // The ring no longer reaches back to `sequence`: conservative, the store runs.
    if (writtenBack.front()[0] > sequence + 1) return true;
    for (auto it = writtenBack.rbegin(); it != writtenBack.rend() && (*it)[0] > sequence; ++it) {
        if ((*it)[1] < end && begin < (*it)[2]) return true;
    }
    return false;
}

void Recorder::Submit() {
    const Scope scope(*this);
    // Cleared even when nothing is open: the driver counts a dispatch after its call returns, so a
    // submit by another thread in between leaves a stale count behind.
    workSinceSubmit_.store(0, std::memory_order_relaxed);
    if (open == nullptr) return;
    auto batch = std::move(open);
    Check(context.Function<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(batch->commands), "vkEndCommandBuffer recorder");
    VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submission.commandBufferCount = 1;
    submission.pCommandBuffers = &batch->commands;
    // The timeline reaches this batch's serial when it completes (see WaitSerial).
    const std::uint64_t serial = submissions + 1;
    VkTimelineSemaphoreSubmitInfoKHR timelineInfo{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR};
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &serial;
    if (timeline != VK_NULL_HANDLE) {
        submission.pNext = &timelineInfo;
        submission.signalSemaphoreCount = 1;
        submission.pSignalSemaphores = &timeline;
    }
    Check(context.Function<PFN_vkQueueSubmit>("vkQueueSubmit")(context.queue, 1, &submission, batch->fence), "vkQueueSubmit recorder");
    batch->submitted = true;
    batch->serial = ++submissions;
    inFlight.push_back(std::move(batch));
    pendingLabelSince_.store(NoPendingLabel, std::memory_order_release);
}

std::uint64_t Recorder::SubmitAndEpoch() {
    const Scope scope(*this);
    Submit();
    return inFlight.empty() ? 0 : submissions;
}

void Recorder::WaitSerial(std::uint64_t serial) {
    if (timeline == VK_NULL_HANDLE || serial == 0) return;
    const Waiter waiter{unlockedWaiters};
    Check(WaitTimeline(context.device, timeline, context.Function<PFN_vkWaitSemaphoresKHR>("vkWaitSemaphoresKHR"), serial), "vkWaitSemaphoresKHR recorder");
}

void Recorder::FinishUpTo(std::uint64_t serial) {
    const Scope scope(*this);
    while (!inFlight.empty() && inFlight.front()->serial <= serial) {
        auto batch = std::move(inFlight.front());
        inFlight.pop_front();
        finish(std::move(batch), true);
    }
}

void Recorder::Sync() {
    const Scope scope(*this);
    Submit();
    while (!inFlight.empty()) {
        auto batch = std::move(inFlight.front());
        inFlight.pop_front();
        finish(std::move(batch), true);
    }
}

void Recorder::SyncThrough(std::uint64_t address, std::size_t bytes, bool waitUnlocked) {
    if (bytes == 0) return;
    const Scope scope(*this);
    const auto end = address + bytes;
    if (completionDepth != 0) {
        // A store made by a completion action (a copied buffer's write-back) reached the flush hook
        // while a later batch still notes the range. Every batch in flight now was recorded after the
        // completing one (finish runs front to back), so in program order this store precedes their
        // writes: landing it first is the order hardware gives. Waiting for them would land the
        // completing batch's bytes over the later batch's.
        return;
    }
    if (waitUnlocked && syncThroughUnlocked(address, end)) return;
    if (open != nullptr && overlaps(*open, address, end)) {
        Sync();
        return;
    }
    std::uint64_t target = 0;
    for (auto it = inFlight.rbegin(); it != inFlight.rend(); ++it) {
        if (overlaps(**it, address, end)) {
            target = (*it)->serial;
            break;
        }
    }
    if (target == 0) return;
    // Batches are in flight in serial order. A completion's own guest access can sync re-entrantly and
    // finish the target first (a new batch may reuse its allocation, so the serial identifies it).
    FinishUpTo(target);
}

bool Recorder::syncThroughUnlocked(std::uint64_t address, std::uint64_t end) {
    // Only the outermost hold may release the lock: a nested hook (inside a caller's hold) must not
    // give up the caller's exclusion.
    if (timeline == VK_NULL_HANDLE || depth != 1 || threadHolds != 1) return false;
    std::uint64_t target = 0;
    if (open != nullptr && overlaps(*open, address, end)) {
        // Submitted here so the GPU can reach it; the open batch makes everything the target.
        Submit();
        target = submissions;
    } else {
        for (auto it = inFlight.rbegin(); it != inFlight.rend(); ++it) {
            if (overlaps(**it, address, end)) {
                target = (*it)->serial;
                break;
            }
        }
    }
    if (target == 0) return true;
    // Copied out: after the release `this` is only touched once the lock is retaken, and ~Recorder
    // waits for registered waiters before destroying anything.
    const auto device = context.device;
    const auto semaphore = timeline;
    const auto waitSemaphores = context.Function<PFN_vkWaitSemaphoresKHR>("vkWaitSemaphoresKHR");
    VkResult result = VK_SUCCESS;
    {
        const Waiter waiter{unlockedWaiters};
        --depth;
        mutex.unlock();
        result = WaitTimeline(device, semaphore, waitSemaphores, target);
        mutex.lock();
        ++depth;
        Check(result, "vkWaitSemaphoresKHR recorder");
        // Completions up to the target, under the lock again; the fences are signaled. Another thread
        // may have finished some meanwhile.
        FinishUpTo(target);
    }
    return true;
}

bool Recorder::Reap() {
    const Scope scope(*this);
    const auto status = context.Function<PFN_vkGetFenceStatus>("vkGetFenceStatus");
    while (!inFlight.empty()) {
        if (status(context.device, inFlight.front()->fence) != VK_SUCCESS) break;
        auto batch = std::move(inFlight.front());
        inFlight.pop_front();
        finish(std::move(batch), false);
    }
    return inFlight.empty();
}

void Recorder::finish(std::unique_ptr<Batch> batch, bool wait) {
    // The batch's writes stay published until its completions ran, on every exit path.
    struct Finishing {
        Recorder& recorder;
        const Batch* batch;
        ~Finishing() {
            auto& list = recorder.finishing;
            list.erase(std::remove(list.begin(), list.end(), batch), list.end());
            // The batch's completion labels have landed, or never will (fence failure).
            if (batch->completionLabelCount != 0) recorder.completionLabels_.fetch_sub(batch->completionLabelCount, std::memory_order_acq_rel);
            // An allocation failure in the rebuild keeps the previous snapshot, a superset (a stale
            // entry only causes a false hit, never a miss), rather than terminating.
            try {
                recorder.publishPendingWrites();
            } catch (...) {
            }
        }
    } finishingScope{*this, batch.get()};
    finishing.push_back(batch.get());
    if (wait) {
        const auto waitFences = context.Function<PFN_vkWaitForFences>("vkWaitForFences");
        auto result = waitFences(context.device, 1, &batch->fence, VK_TRUE, WaitSliceNs);
        for (int waited = 5; result == VK_TIMEOUT; waited += 5) {
            std::fprintf(stderr, "[gpu] recorded batch still running on the GPU after %d s\n", waited);
            result = waitFences(context.device, 1, &batch->fence, VK_TRUE, WaitSliceNs);
        }
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
            Check(context.Function<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(context.device), "vkDeviceWaitIdle after recorder fence failure");
        }
        if (result != VK_SUCCESS) {
            release(*batch);
            Check(result, "vkWaitForFences recorder");
        }
    }
    {
        struct InCompletion {
            InCompletion() { ++completionDepth; }
            ~InCompletion() { --completionDepth; }
        } inCompletion;
        // Completions store GPU results to guest memory; a failing one is reported, the rest still run.
        for (auto& action : batch->completions) {
            try {
                action();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[gpu] deferred write-back failed: %s\n", error.what());
            }
        }
    }
    // Completion closures and kept objects are destroyed when the outermost Scope of this thread ends,
    // never under a recorder hold (their destructors may be expensive and take their own locks).
    DeferredBatches().push_back({std::move(batch->kept), std::move(batch->completions)});
    batch->kept.clear();
    batch->completions.clear();
    // The batch's label entries leave the table (a later label to the same dword already replaced its
    // entry and belongs to another batch). Correctness never depended on this removal.
    if (!batch->labelDwords.empty()) {
        std::lock_guard tableLock(labelMutex);
        for (const auto dword : batch->labelDwords) {
            const auto found = labels.find(dword);
            if (found != labels.end() && found->second.batch == batch.get()) labels.erase(found);
        }
    }
    batch->labelDwords.clear();
    const auto serial = batch->serial;
    release(*batch);
    // Batches finish front to back, so the serial only grows.
    auto seen = completedSerial.load(std::memory_order_relaxed);
    while (serial > seen && !completedSerial.compare_exchange_weak(seen, serial, std::memory_order_acq_rel)) {
    }
}

void Recorder::release(Batch& batch) noexcept {
    // A completed (or never submitted) batch's objects are kept for reuse: the fence is signaled or
    // untouched, so resetting it cannot block, and the command buffer is no longer pending.
    if (batch.commands != VK_NULL_HANDLE && batch.fence != VK_NULL_HANDLE && spare.size() < SparePool && context.Function<PFN_vkResetFences>("vkResetFences")(context.device, 1, &batch.fence) == VK_SUCCESS) {
        spare.emplace_back(batch.commands, batch.fence);
        batch.commands = VK_NULL_HANDLE;
        batch.fence = VK_NULL_HANDLE;
        return;
    }
    if (batch.commands != VK_NULL_HANDLE) context.Function<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(context.device, context.pool, 1, &batch.commands);
    if (batch.fence != VK_NULL_HANDLE) context.Function<PFN_vkDestroyFence>("vkDestroyFence")(context.device, batch.fence, nullptr);
    batch.commands = VK_NULL_HANDLE;
    batch.fence = VK_NULL_HANDLE;
}

}
