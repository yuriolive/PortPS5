// core/libs/prx/libSceAgcDriver/Graphics/include/Recorder.hpp
//
// Command-buffer Recorder: batches GPU work recorded for guest commands into one open Vulkan command
// buffer, submits it without waiting, and runs completion actions (CPU write-backs, label stores) in
// submission order once the GPU finished. Spec: docs/spec/gpu-driver.md (Target design, "Recorder").
//
// Adapted from AnyPS5 commit 8a69fefe (PR #5, Graphics/include/Recorder.hpp). Differences:
//  - No GuestMemory::GpuMutex. The Recorder owns one recursive lock; every non-static member takes it,
//    so callers need no outer lock and completions may re-enter (a write-back reaching the flush hook).
//  - No environment-variable switches and no profiling counters; the behaviours the switches toggled are fixed at the
//    upstream defaults. Diagnostics belong in the typed [debug] config (docs/spec/configuration.md).
//  - Objects handed to Keep are destroyed after the outermost Recorder lock on the finishing thread was
//    released (no release thread), so their destructors never run under a Recorder hold.
//  - The flush hook is registered through IWriteTracker::SetFlushHook (Activate), not a GuestMemory global.
//
// Threading: all members are thread-safe. Static readers are lock-free (snapshot, atomics) or take only
// the label-table mutex; none takes the Recorder lock. Lock order: Recorder lock -> label mutex.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RECORDER_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RECORDER_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libc/include/WriteTracker.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

/**
 * @brief Accumulates GPU work across guest commands so the CPU does not wait for each one.
 *
 * Work records into one open batch (Commands). Submit sends it to the queue without waiting; Sync waits
 * for every batch and then runs their completion actions in order. Submissions are numbered (serials);
 * with a timeline semaphore each Submit signals its serial, so a thread can WaitSerial without the lock
 * and finish the completions afterwards (FinishUpTo). Guest ranges the recorded work will write are
 * noted, and a CPU access to such a range (the flush hook) syncs first.
 */
class Recorder {
public:
    /** @brief Construction options. */
    struct Options {
        /// Device enabled VK_KHR_timeline_semaphore / Vulkan 1.2 timeline semaphores (WaitSerial usable).
        bool timelineSemaphores = false;
        /// Tracker used by completion label stores to report GPU-written bytes (MarkWritten); may be null.
        PortPS5::GuestMemory::IWriteTracker* tracker = nullptr;
    };

    /**
     * @param context Device handles; the device, queue and pool must outlive the Recorder.
     * @param options See Options. A failed timeline-semaphore creation only disables WaitSerial.
     */
    explicit Recorder(const Context& context, const Options& options);
    explicit Recorder(const Context& context) : Recorder(context, Options{}) {}
    /** @brief Deactivates, syncs every batch, then frees the pooled command buffers and the timeline. */
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    /**
     * @brief RAII hold of the Recorder lock (recursive). Every member takes one internally; a caller that
     * records several commands into Commands() must keep a Scope for the whole recording so another
     * thread cannot Submit the batch underneath it. Kept objects of finished batches are destroyed when
     * the thread's outermost Scope ends.
     */
    class Scope {
    public:
        explicit Scope(const Recorder& recorder);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const Recorder& recorder;
    };

    /**
     * @return The open batch's command buffer, starting a batch when none is open. Throws on Vulkan
     * failure. The caller must hold a Scope while it records into the returned buffer.
     */
    VkCommandBuffer Commands();
    /** @return Whether a batch is open (unsubmitted). */
    bool Recording() const;
    /** @return Whether nothing is open and nothing is in flight. */
    bool Idle() const;
    /** @return Whether recorded work still has completion actions to run. */
    bool HasCompletions() const;

    /**
     * @brief Keeps @p object alive until the open batch completed.
     *
     * It is destroyed after the finishing thread released its outermost Recorder hold, so its destructor
     * must need neither the Recorder nor a particular thread.
     */
    void Keep(std::shared_ptr<void> object);
    /** @brief Runs @p action when the open batch completed, in submission order (CPU write-backs). */
    void OnComplete(std::function<void()> action);

    /** @brief Notes that the open batch will write [address, address + bytes). */
    void NotePendingWrite(std::uint64_t address, std::size_t bytes);
    /** @brief Notes several [begin, end) ranges and publishes the snapshot once. */
    void NotePendingWrites(std::span<const std::pair<std::uint64_t, std::uint64_t>> ranges);
    /** @return Whether the open or an in-flight batch writes the range. */
    bool PendingWriteOverlaps(std::uint64_t address, std::size_t bytes) const;
    /** @return Whether the OPEN (unsubmitted) batch writes the range: a wait on it must submit first. */
    bool OpenWriteOverlaps(std::uint64_t address, std::size_t bytes) const;

    /** @brief Ends and submits the open batch without waiting (no-op when none is open). */
    void Submit();
    /** @return Serial of the newest submitted batch after submitting the open one; 0 if none ever was. */
    std::uint64_t SubmitAndEpoch();
    /** @return Whether WaitSerial is usable (timeline semaphore exists). */
    bool HasTimeline() const { return timeline != VK_NULL_HANDLE; }
    /**
     * @brief Waits until every batch up to @p serial completed on the GPU, WITHOUT the Recorder lock.
     *
     * Touches only the immutable timeline semaphore; the caller keeps the Recorder alive. Requires
     * HasTimeline(); a no-op otherwise or for serial 0. Throws on device loss.
     */
    void WaitSerial(std::uint64_t serial);
    /** @brief Finishes (completions, release) in-flight batches up to @p serial, front first. */
    void FinishUpTo(std::uint64_t serial);
    /** @brief Submits and waits for every batch, running completions in order. */
    void Sync();
    /**
     * @brief Waits only for the batches up to the newest one that writes the range.
     *
     * Submits the open batch when it writes the range; later batches stay in flight. Inside a completion
     * action no wait is made: every batch still in flight was recorded after the completing one, so its
     * store is that batch's in-order write.
     * @param waitUnlocked When the caller's hold is the outermost one and a timeline exists, the GPU wait
     * runs with the Recorder lock released so other threads are not queued behind a CPU read's wait.
     */
    void SyncThrough(std::uint64_t address, std::size_t bytes, bool waitUnlocked = false);
    /** @brief Completes batches whose fences already signaled. @return Whether nothing is in flight. */
    bool Reap();
    /** @return Number of batches submitted so far (the newest serial). */
    std::uint64_t Submissions() const;
    /** @return Serial of the newest batch known finished (completions ran); 0 when none. */
    std::uint64_t CompletedSerial() const;

    /**
     * @brief Notes a GPU label (store recorded into the batch) per 4-byte dword.
     *
     * A WAIT_REG_MEM whose submission was stamped BEFORE the label was recorded may take the value from
     * the table instead of waiting for the GPU. Entries leave the table with their batch.
     * @param stamp Record-order stamp (the driver's event serial); @param queue recording queue id.
     */
    void NoteLabel(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue);
    /**
     * @return The 4- or 8-byte value the table holds when every dword is present with a stamp newer than
     * @p afterStamp; @p queue receives the recording queue of the first dword. nullopt otherwise.
     */
    std::optional<std::uint64_t> PendingLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue) const;
    /** @return Number of tracked label dwords. */
    std::size_t PendingLabels() const;
    /** @return Whether any tracked label dword lies inside [address, address + bytes). */
    bool PendingLabelIn(std::uint64_t address, std::size_t bytes) const;
    /**
     * @brief Stores a label behind pending completions, after every earlier write-back (submission order).
     *
     * Appended to the open batch, or to the newest in-flight batch when none is open. The range is noted
     * as a pending write and the label enters the table. @p storedOnGpu: the same bytes were also recorded
     * into the batch (host import), so the completion re-stores them only when a write-back noted by
     * NoteWrittenBack overlapped the range since; storing unconditionally could overwrite memory the game
     * already reused. A label the GPU has no view of always stores. Requires a batch (open or in flight).
     */
    void AfterCompletions(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue, bool storedOnGpu);
    /** @brief Records a CPU store of GPU results into guest memory on THIS recorder (see AfterCompletions). */
    void NoteWrittenBackOn(std::uint64_t address, std::size_t bytes);
    /** @brief NoteWrittenBackOn of the active recorder; prefer the instance form when the owner is known. */
    static void NoteWrittenBack(std::uint64_t address, std::size_t bytes);

    /** @return When the open batch received its first label (nullopt: none pending). Lock-free. */
    static std::optional<std::chrono::steady_clock::time_point> PendingLabelSince();
    /** @return Counter bumped by every note of a pending write; lets a poller learn a producer recorded. */
    static std::uint64_t WriteGeneration();
    /** @return Labels waiting in completion actions (the CPU must reap their batch for them to land). */
    static std::uint64_t PendingCompletionLabels();
    /** @return Dispatches and draws recorded since the last submit (see CountRecordedWork). */
    static std::uint64_t RecordedWorkSinceSubmit();
    /** @brief Counts one recorded dispatch or draw so callers can bound a batch's size. */
    static void CountRecordedWork();
    /** @return Whether the lock-free pending-write snapshot of the active recorder overlaps the range. */
    static bool SnapshotWriteOverlaps(std::uint64_t address, std::size_t bytes);
    /** @brief PendingLabel of the active recorder using only the label-table mutex. */
    static std::optional<std::uint64_t> LookupLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue);

    /** @return The active recorder (see Activate), or nullptr. Not safe to dereference across a teardown. */
    static Recorder* Active();
    /**
     * @brief Makes this the active recorder and registers the flush hook on the tracker.
     * @param tracker Tracker whose Collect must land pending GPU writes first; null skips registration.
     */
    void Activate(PortPS5::GuestMemory::IWriteTracker* tracker = nullptr);

private:
    struct Batch {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<void>> kept;
        std::vector<std::function<void()>> completions;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> writes;
        std::uint64_t serial = 0;  // 1-based; identifies a batch after its allocation was reused
        bool submitted = false;
        std::vector<std::uint64_t> labelDwords;
        std::uint32_t completionLabelCount = 0;
    };
    struct LabelEntry {
        std::uint32_t value;
        std::uint32_t queue;
        std::uint64_t stamp;
        const Batch* batch;
    };
    using WriteRanges = std::vector<std::pair<std::uint64_t, std::uint64_t>>;
    bool syncThroughUnlocked(std::uint64_t address, std::uint64_t end);
    void finish(std::unique_ptr<Batch> batch, bool wait);
    void release(Batch& batch) noexcept;
    static bool overlaps(const Batch& batch, std::uint64_t address, std::uint64_t end);
    void publishPendingWrites() const;
    bool noteWrite(std::uint64_t address, std::size_t bytes);
    void noteWriteOn(Batch& batch, std::uint64_t address, std::size_t bytes);
    void noteLabelOn(Batch& batch, std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue);
    bool writtenBackSince(std::uint64_t sequence, std::uint64_t begin, std::uint64_t end);
    std::optional<std::uint64_t> lookupLabel(std::uint64_t address, std::size_t bytes, std::uint64_t afterStamp, std::uint32_t& queue) const;
    bool snapshotCovers(std::uint64_t address, std::uint64_t end) const;
    void startBatch();
    static void FlushForAccess(void* context, std::uint64_t address, std::uint64_t bytes);

    Context context;
    Options options;
    VkSemaphore timeline = VK_NULL_HANDLE;
    // Recursive so completions may re-enter; `depth` counts this thread's nested holds and is only
    // touched while the mutex is held, so depth == 1 means "outermost hold" for the unlocked wait.
    mutable std::recursive_mutex mutex;
    mutable int depth = 0;
    // Threads inside a timeline wait with the lock released: ~Recorder waits for them to leave.
    std::atomic<int> unlockedWaiters{0};
    std::atomic<int> hookUsers{0};

    mutable std::mutex labelMutex;
    std::unordered_map<std::uint64_t, LabelEntry> labels;
    std::unique_ptr<Batch> open;
    std::deque<std::unique_ptr<Batch>> inFlight;
    // Batches popped from inFlight whose completions have not finished: their writes stay in the snapshot.
    std::vector<const Batch*> finishing;
    std::uint64_t submissions = 0;
    std::atomic<std::uint64_t> completedSerial{0};
    std::vector<std::pair<VkCommandBuffer, VkFence>> spare;

    std::mutex writtenBackMutex;
    std::deque<std::array<std::uint64_t, 3>> writtenBack;
    std::uint64_t writtenBackSequence = 0;

    mutable std::atomic<std::shared_ptr<const WriteRanges>> snapshot;
    std::atomic<std::int64_t> pendingLabelSince_{std::numeric_limits<std::int64_t>::min()};
    std::atomic<std::uint64_t> writeGeneration_{0};
    std::atomic<std::uint64_t> completionLabels_{0};
    std::atomic<std::uint64_t> workSinceSubmit_{0};

};

}

#endif
