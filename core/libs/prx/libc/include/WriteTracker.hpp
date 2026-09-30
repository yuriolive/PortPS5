// core/libs/prx/libc/include/WriteTracker.hpp
// IWriteTracker interface plus WriteWatchTracker and PageStateTable: the
// driver-facing CPU write-tracking half of guest-memory
// (docs/spec/guest-memory.md, Target design "Write tracking"). Host-only
// header: no guest-called exports, no APS5_VABI, no throws.
#ifndef CORE_LIBS_PRX_LIBC_WRITETRACKER_HPP
#define CORE_LIBS_PRX_LIBC_WRITETRACKER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <vector>

// CPU write tracking for guest memory (docs/spec/guest-memory.md).
//
// IWriteTracker is the driver-facing interface of the guest-memory subsystem
// and is defined only here. Block generations for CPU writes (Collect) and
// GPU writes (MarkWritten) live in the tracker; which recorded batch wrote
// which range (writer intervals) lives in the driver's BufferCache
// (docs/spec/gpu-driver.md). NullTracker reports "unknown" for everything so
// driver code can link and run before the real trackers land.
namespace PortPS5 {
namespace GuestMemory {

// Half-open byte range [begin, begin + bytes).
struct AddrRange {
    std::uint64_t begin = 0;
    std::uint64_t bytes = 0;
};

// Lock-free page-state read; one byte per 4 KiB page in the real table.
enum class PageState : std::uint8_t {
    NotGuest = 0,
    Uncommitted,
    ReadOnly,
    ReadWrite,
};

// Opaque pin on registry ranges; pins++ per overlapped Range while held.
struct PinToken {
    std::uint64_t value = 0;
    constexpr bool operator==(const PinToken&) const = default;
};

// Lands pending GPU writes to [address, address + bytes) before a CPU read
// of a tracked range proceeds. Runs with no tracker lock held.
using FlushHook = void (*)(void* context, std::uint64_t address, std::uint64_t bytes);

struct IWriteTracker {
    virtual ~IWriteTracker() = default;
    // Stamps CPU-dirty blocks, returning the newest block generation in the
    // range; 0 means unknown, and the caller compares bytes instead.
    virtual std::uint64_t Collect(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual PinToken Pin(std::span<const AddrRange> ranges) = 0;
    virtual void Unpin(PinToken token) noexcept = 0;
    virtual PageState PageStateAt(std::uint64_t address) const = 0;
    // Reports GPU-written bytes, returning the new generation.
    virtual std::uint64_t MarkWritten(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual void SetFlushHook(FlushHook hook, void* context) = 0;
};

// Always reports "unknown": for non-Windows builds and tracker-unavailable
// paths, and as a stand-in while the real trackers land.
class NullTracker final : public IWriteTracker {
public:
    std::uint64_t Collect(std::uint64_t, std::uint64_t) override { return 0; }
    PinToken Pin(std::span<const AddrRange>) override { return {}; }
    void Unpin(PinToken) noexcept override {}
    PageState PageStateAt(std::uint64_t) const override { return PageState::NotGuest; }
    std::uint64_t MarkWritten(std::uint64_t, std::uint64_t) override { return 0; }
    void SetFlushHook(FlushHook, void*) override {}
};

// Tracking geometry. Block generations are per 64 KiB block (spec choice:
// per-job label slots sit 0x20 apart, so finer blocks only pay off where
// profiles show false invalidation); the page-state table is one byte per
// 4 KiB page (docs/spec/guest-memory.md, Target design "Write tracking").
inline constexpr std::uint64_t kWriteWatchBlockBytes = 65536ULL;
inline constexpr std::uint64_t kPageStatePageBytes = 4096ULL;
// Tracker metadata is sharded per 1 GiB of guest address so Collect on one
// range never serializes against an unrelated range (spec: one tracker mutex
// was the bottleneck in AnyPS5 main (merged PR #5)).
inline constexpr std::uint64_t kTrackerShardBytes = 1073741824ULL;

/**
 * @brief Registry-owned page-state snapshot: one byte per 4 KiB page.
 *
 * The registry (which knows every protection) writes entries under its
 * exclusive lock; the driver reads them lock-free with relaxed atomics, so
 * VirtualQuery and generation re-checks disappear from the hot path
 * (docs/spec/guest-memory.md, Target design "Page-state table"). Shards are
 * allocated lazily on first Update and published with release semantics, so
 * At() never takes a lock.
 */
class PageStateTable {
public:
    PageStateTable() = default;
    PageStateTable(const PageStateTable&) = delete;
    PageStateTable& operator=(const PageStateTable&) = delete;
    ~PageStateTable();

    /**
     * @brief Covers [base, base + bytes) with per-4 KiB page state.
     * @param base First byte of the tracked window (arena or image base).
     * @param bytes Window length in bytes.
     * @return True on success; false when the window is empty or wraps, or
     * when the shard directory cannot be allocated. A second Init aborts:
     * the table is sized once at startup, mirroring the fixed arena.
     */
    bool Init(std::uint64_t base, std::uint64_t bytes) noexcept;

    /**
     * @brief Records @p state for every 4 KiB page in [address, address + bytes).
     * @param address First byte of the range (need not be page-aligned; the
     * covered pages are).
     * @param bytes Range length; zero is a no-op.
     * @param state State to store.
     * Call only from the registry's exclusive-lock path. Out-of-window
     * ranges abort via Unsupported: the registry only ever passes tracked
     * ranges, so anything else is a caller bug, not guest input.
     */
    void Update(std::uint64_t address, std::uint64_t bytes, PageState state) noexcept;

    /**
     * @brief Lock-free read of one page's state.
     * @param address Any guest address.
     * @return The stored state; PageState::Uncommitted for in-window pages
     * never Updated (conservative: the caller waits or compares instead of
     * assuming writable); PageState::NotGuest outside the window.
     */
    PageState At(std::uint64_t address) const noexcept;

private:
    struct Shard;
    // Returns the shard, allocating and Uncommitted-filling it on first use.
    // Returns nullptr on allocation failure. Caller holds mutex_.
    Shard* EnsureShard(std::uint64_t index) noexcept;
    static std::uint64_t ShardIndex(std::uint64_t address, std::uint64_t base) noexcept;

    std::uint64_t base_ = 0;
    std::uint64_t end_ = 0;  // one past the window; base_ when uninitialized
    std::atomic<Shard*>* shards_ = nullptr;
    std::uint64_t shardCount_ = 0;
    mutable std::mutex mutex_;  // serializes EnsureShard only; At never locks
};

/**
 * @brief IWriteTracker over MEM_WRITE_WATCH arena memory (spec "WriteWatchTracker").
 *
 * Collect performs one resetting GetWriteWatch pass per call and stamps
 * 64 KiB blocks with 64-bit generations from a single global monotonic
 * counter (never wraps in practice; AnyPS5 main's (merged PR #5) 32-bit counter could wrap in
 * ~12 h). Ranges the walk cannot cover (uncommitted pages, non-write-watch
 * memory, non-Windows builds, out-of-window addresses) report 0 ("unknown":
 * the caller compares bytes) and bump UnknownWalks for telemetry.
 */
class WriteWatchTracker final : public IWriteTracker {
public:
    WriteWatchTracker() = default;
    WriteWatchTracker(const WriteWatchTracker&) = delete;
    WriteWatchTracker& operator=(const WriteWatchTracker&) = delete;
    ~WriteWatchTracker();

    /**
     * @brief Arms the tracker over [base, base + bytes), optionally backed by a page table.
     * @param base First tracked byte (arena base).
     * @param bytes Window length in bytes.
     * @param table Registry-owned page-state table for PageStateAt; may be
     * nullptr (reads then report Uncommitted in-window). Must outlive the
     * tracker; set once before the tracker is shared across threads.
     * @return True on success; false on invalid window or allocation
     * failure, in which case every method reports unknown. A second Init
     * aborts (single sizing at startup, mirroring the fixed arena).
     */
    bool Init(std::uint64_t base, std::uint64_t bytes, const PageStateTable* table = nullptr) noexcept;

    /**
     * @brief Stamps CPU-dirty 64 KiB blocks in [address, address + bytes).
     * @param address First byte of the range.
     * @param bytes Range length.
     * @return Newest block generation in the range; 0 when the range is
     * empty, wraps, lies outside the window, or the write-watch walk fails
     * (uncommitted pages, non-write-watch memory, non-Windows builds). The
     * registered flush hook, if any, runs for the range first, with no
     * tracker lock held.
     */
    std::uint64_t Collect(std::uint64_t address, std::uint64_t bytes) noexcept override;

    /**
     * @brief Records an explicit pin over @p ranges, returning its token.
     * @param ranges Ranges the driver submission references; kept by count
     * so balance is checkable (registry-side per-Range pins++ arrive with
     * the registry interval-map rewrite; until then the token record is the
     * pin).
     * @return Fresh nonzero token. Host-metadata allocation failure aborts
     * via Unsupported: unlike guest ENOMEM there is no guest-meaningful
     * code for it.
     */
    PinToken Pin(std::span<const AddrRange> ranges) noexcept override;

    /**
     * @brief Releases a token from Pin.
     * @param token Token to release; the null token is a no-op.
     * Unknown or already-released tokens abort via Unsupported: pin
     * accounting corruption would otherwise retire GPU work early.
     */
    void Unpin(PinToken token) noexcept override;

    /**
     * @brief Page state for @p address via the backing table (or window check).
     * @param address Any guest address.
     * @return Table state when bound, else Uncommitted in-window and
     * NotGuest outside it.
     */
    PageState PageStateAt(std::uint64_t address) const noexcept override;

    /**
     * @brief Records GPU-written bytes (M3 block-generation path; see spec).
     * @param address First byte; @param bytes length.
     * @return New generation covering the range; 0 for empty, wrapping, or
     * out-of-window ranges. Bumps the single global counter shared with
     * Collect so CPU/GPU novelty share one ordering.
     */
    std::uint64_t MarkWritten(std::uint64_t address, std::uint64_t bytes) noexcept override;

    /**
     * @brief Registers the driver's flush hook (spec "Flush hook").
     * @param hook Lands pending GPU writes over a range before a CPU read;
     * @param context Opaque driver context passed back to @p hook.
     * Runs with no tracker lock held, before the write-watch pass.
     */
    void SetFlushHook(FlushHook hook, void* context) noexcept override;

    /**
     * @brief Count of Collect calls that reported unknown (walk failures).
     * @return Monotonic counter for telemetry (results-JSON class inputs).
     */
    std::uint64_t UnknownWalks() const noexcept;

private:
    struct GenShard;
    bool InWindow(std::uint64_t address, std::uint64_t bytes) const noexcept;
    // Fills @p locks with every shard in [first, last] in ascending index
    // order; the locks are held until @p locks is destroyed, i.e. across the
    // caller's whole walk+stamp. Ascending order is global, so two
    // Collects/MarkWrittens can never deadlock: each blocks only on a higher
    // shard while holding lower ones, and the holder of the higher shard
    // never waits on a lower one. False when lock storage cannot be
    // allocated (caller reports unknown); mutex::lock itself does not throw.
    bool LockShardsAscending(std::uint64_t first, std::uint64_t last,
                             std::vector<std::unique_lock<std::mutex>>& locks) noexcept;
    // Helpers below require the caller to hold ALL touched shards' locks
    // (see LockShardsAscending): the resetting GetWriteWatch walk and the
    // stamping of what it returned must be atomic with respect to other
    // Collects, or a concurrent walk could reset-then-miss a dirty page and
    // report "unchanged" for a write that already landed (stale re-upload).
    // Newest stored generation in [address, address + bytes); 0 when none.
    std::uint64_t MaxGenLocked(std::uint64_t address, std::uint64_t bytes) noexcept;
    // Stamps every 64 KiB block intersecting the range; returns newest stamp
    // and sets complete=false (returning 0) when the generation map cannot
    // be allocated.
    std::uint64_t StampRangeLocked(std::uint64_t address, std::uint64_t bytes, bool& complete) noexcept;
#ifdef _WIN32
    // One bounded resetting GetWriteWatch pass over [base, base + bytes),
    // stamping every reported dirty page's block into newest. False means
    // unknown (dirty-page buffer OOM, walk failure, generation-map OOM).
    // Caller holds the touched shards' locks (see above).
    bool CollectChunkWindows(std::uint64_t base, std::uint64_t bytes, std::uint64_t& newest) noexcept;
#endif

    std::uint64_t base_ = 0;
    std::uint64_t end_ = 0;
    const PageStateTable* table_ = nullptr;
    GenShard* genShards_ = nullptr;
    std::uint64_t genShardCount_ = 0;
    std::mutex hookMutex_;
    FlushHook hook_ = nullptr;
    void* hookContext_ = nullptr;
    std::mutex pinMutex_;
    std::map<std::uint64_t, std::uint64_t> pins_;  // token value -> range count
    std::atomic<std::uint64_t> nextPin_{1};        // 0 is reserved for null
    std::atomic<std::uint64_t> unknownWalks_{0};
    // Single global generation counter shared by every shard. Per-shard
    // counters would break multi-shard ranges: a fresh stamp in a quiet
    // shard could read lower than a stale stamp in a busy one, hiding a
    // write from the "unchanged since g" check. One monotonic counter keeps
    // every novelty globally ordered (starts at 1; 0 stays "unknown").
    std::atomic<std::uint64_t> generation_{1};
};

}  // namespace GuestMemory
}  // namespace PortPS5

#endif
