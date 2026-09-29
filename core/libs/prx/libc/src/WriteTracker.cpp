// core/libs/prx/libc/src/WriteTracker.cpp
// WriteWatchTracker and PageStateTable: the driver-facing CPU write-tracking
// half of the guest-memory subsystem (docs/spec/guest-memory.md, Target
// design "Write tracking" and "Page-state table").
//
// Subsystem: guest-memory (libc). Host-only: no guest-called exports, no
// APS5_VABI, no exceptions cross these functions (all entry points are
// noexcept; host allocation failure reaches Unsupported, never a throw).
// Threading: per-1 GiB-shard locks for block generations; the page-state
// table publishes shards with release/acquire so At() never locks; the flush
// hook runs with no tracker lock held and must not reenter the tracker.

#include "prx/libc/include/WriteTracker.hpp"
#include "prx/libc/include/General.hpp"

#include <new>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PortPS5::GuestMemory {
namespace {

// Pages per 1 GiB shard in the page-state table (1 GiB / 4 KiB).
constexpr std::uint64_t kStatePagesPerShard = 262144ULL;
// 64 KiB blocks per 1 GiB shard in the generation map (1 GiB / 64 KiB).
constexpr std::uint64_t kGenBlocksPerShard = 16384ULL;
// One GetWriteWatch pass is bounded to this many bytes so the dirty-page
// buffer stays small no matter how large the collected range is. Chunking is
// exact (each chunk resets and reports only its own pages), never lossy.
constexpr std::uint64_t kCollectChunkBytes = 67108864ULL;

}  // namespace

struct PageStateTable::Shard {
    std::atomic<std::uint8_t> pages[kStatePagesPerShard];
};

PageStateTable::~PageStateTable() {
    // Why manual teardown, not containers: shard storage is lazily
    // allocated raw memory published lock-free; the destructor runs
    // single-threaded at teardown, so plain deletes are exact.
    if (shards_ != nullptr) {
        for (std::uint64_t i = 0; i < shardCount_; ++i) {
            delete shards_[i].load(std::memory_order_relaxed);
        }
        delete[] shards_;
    }
}

bool PageStateTable::Init(std::uint64_t base, std::uint64_t bytes) noexcept {
    // A fixed window mirrors the fixed arena: sized once at startup. Repeat
    // Init would orphan lazily allocated shards, so it aborts like any other
    // host-side lifecycle bug (guest input can never reach here).
    if (shards_ != nullptr) {
        Unsupported("PageStateTable double Init");
    }
    if (bytes == 0 || base > UINT64_MAX - bytes) {
        return false;
    }
    const std::uint64_t count = (bytes + kTrackerShardBytes - 1) / kTrackerShardBytes;
    // Value-init publishes null shard pointers; At() treats null as
    // Uncommitted until the registry's first Update fills them.
    std::atomic<Shard*>* shards = new (std::nothrow) std::atomic<Shard*>[count]();
    if (shards == nullptr) {
        return false;
    }
    base_ = base;
    end_ = base + bytes;
    shards_ = shards;
    shardCount_ = count;
    return true;
}

std::uint64_t PageStateTable::ShardIndex(std::uint64_t address, std::uint64_t base) noexcept {
    return (address - base) / kTrackerShardBytes;
}

PageStateTable::Shard* PageStateTable::EnsureShard(std::uint64_t index) noexcept {
    // Caller holds mutex_: exactly one thread fills and publishes a shard.
    Shard* shard = shards_[index].load(std::memory_order_relaxed);
    if (shard != nullptr) {
        return shard;
    }
    Shard* fresh = new (std::nothrow) Shard;
    if (fresh == nullptr) {
        return nullptr;
    }
    // Fill before publish: readers use acquire loads, so every byte they can
    // observe is already Uncommitted, never indeterminate memory.
    for (std::uint64_t i = 0; i < kStatePagesPerShard; ++i) {
        fresh->pages[i].store(static_cast<std::uint8_t>(PageState::Uncommitted), std::memory_order_relaxed);
    }
    shards_[index].store(fresh, std::memory_order_release);
    return fresh;
}

void PageStateTable::Update(std::uint64_t address, std::uint64_t bytes, PageState state) noexcept {
    if (bytes == 0) {
        return;
    }
    // Out-of-window updates are registry bugs, not guest input: the registry
    // only tracks arena and image pages, so anything else would corrupt the
    // table the driver reads lock-free. Fail loudly instead.
    if (shards_ == nullptr || address < base_ || address > UINT64_MAX - bytes || address + bytes > end_) {
        Unsupported("PageStateTable update outside tracked window");
    }
    const std::uint8_t value = static_cast<std::uint8_t>(state);
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t first = ShardIndex(address, base_);
    std::uint64_t last = ShardIndex(address + bytes - 1, base_);
    for (std::uint64_t shard = first; shard <= last; ++shard) {
        Shard* target = EnsureShard(shard);
        if (target == nullptr) {
            Unsupported("PageStateTable shard allocation failed");
        }
        const std::uint64_t shardBase = base_ + shard * kTrackerShardBytes;
        const std::uint64_t from = address > shardBase ? address : shardBase;
        const std::uint64_t to = address + bytes < shardBase + kTrackerShardBytes ? address + bytes : shardBase + kTrackerShardBytes;
        // Page granularity: any touched 4 KiB page takes the new state, so a
        // sub-page protect still shows on the containing page (conservative).
        const std::uint64_t firstPage = (from - shardBase) / kPageStatePageBytes;
        const std::uint64_t lastPage = (to - 1 - shardBase) / kPageStatePageBytes;
        for (std::uint64_t page = firstPage; page <= lastPage; ++page) {
            target->pages[page].store(value, std::memory_order_relaxed);
        }
    }
}

PageState PageStateTable::At(std::uint64_t address) const noexcept {
    // Lock-free by construction: shards_ is set once at Init before sharing,
    // shard pointers publish with release, and page bytes are relaxed
    // atomics. A null shard means "never Updated": Uncommitted, so callers
    // wait or compare instead of assuming writable.
    if (shards_ == nullptr || address < base_ || address >= end_) {
        return PageState::NotGuest;
    }
    const Shard* shard = shards_[ShardIndex(address, base_)].load(std::memory_order_acquire);
    if (shard == nullptr) {
        return PageState::Uncommitted;
    }
    const std::uint64_t page = ((address - base_) % kTrackerShardBytes) / kPageStatePageBytes;
    return static_cast<PageState>(shard->pages[page].load(std::memory_order_relaxed));
}

struct WriteWatchTracker::GenShard {
    std::mutex mutex;
    std::uint64_t* gens = nullptr;
};

WriteWatchTracker::~WriteWatchTracker() {
    if (genShards_ != nullptr) {
        for (std::uint64_t i = 0; i < genShardCount_; ++i) {
            delete[] genShards_[i].gens;
        }
        delete[] genShards_;
    }
}

bool WriteWatchTracker::Init(std::uint64_t base, std::uint64_t bytes, const PageStateTable* table) noexcept {
    if (genShards_ != nullptr) {
        Unsupported("WriteWatchTracker double Init");
    }
    if (bytes == 0 || base > UINT64_MAX - bytes) {
        return false;
    }
    const std::uint64_t count = (bytes + kTrackerShardBytes - 1) / kTrackerShardBytes;
    GenShard* shards = new (std::nothrow) GenShard[count];
    if (shards == nullptr) {
        return false;
    }
    base_ = base;
    end_ = base + bytes;
    // The table is registry-owned and must outlive the tracker; the pointer
    // is read without a lock, so it has to be set before sharing.
    table_ = table;
    genShards_ = shards;
    genShardCount_ = count;
    return true;
}

bool WriteWatchTracker::InWindow(std::uint64_t address, std::uint64_t bytes) const noexcept {
    return genShards_ != nullptr && bytes != 0 && address >= base_ && address <= UINT64_MAX - bytes && address + bytes <= end_;
}

std::uint64_t WriteWatchTracker::MaxGenLocked(std::uint64_t address, std::uint64_t bytes) noexcept {
    // Caller holds every touched shard's lock and guarantees an in-window,
    // non-wrapping range.
    std::uint64_t newest = 0;
    const std::uint64_t firstBlock = (address - base_) / kWriteWatchBlockBytes;
    const std::uint64_t lastBlock = (address + bytes - 1 - base_) / kWriteWatchBlockBytes;
    for (std::uint64_t block = firstBlock; block <= lastBlock; ++block) {
        const GenShard& slot = genShards_[block / kGenBlocksPerShard];
        // A null map means "never stamped": contributes 0, which keeps the
        // unknown verdict for untouched ranges (caller compares bytes).
        if (slot.gens != nullptr) {
            const std::uint64_t gen = slot.gens[block % kGenBlocksPerShard];
            newest = newest > gen ? newest : gen;
        }
    }
    return newest;
}

std::uint64_t WriteWatchTracker::StampRangeLocked(std::uint64_t address, std::uint64_t bytes, bool& complete) noexcept {
    // Caller holds every touched shard's lock and guarantees an in-window,
    // non-wrapping range. Every intersecting 64 KiB block (partial blocks
    // included: conservative, never misses a write) takes the next global
    // generation.
    complete = true;
    std::uint64_t newest = 0;
    const std::uint64_t firstBlock = (address - base_) / kWriteWatchBlockBytes;
    const std::uint64_t lastBlock = (address + bytes - 1 - base_) / kWriteWatchBlockBytes;
    for (std::uint64_t block = firstBlock; block <= lastBlock; ++block) {
        GenShard& slot = genShards_[block / kGenBlocksPerShard];
        if (slot.gens == nullptr) {
            // Zero-init: unstamped blocks read 0 ("unknown") until first use.
            slot.gens = new (std::nothrow) std::uint64_t[kGenBlocksPerShard]();
            if (slot.gens == nullptr) {
                complete = false;
                return 0;
            }
        }
        // 64-bit global counter never wraps in practice (spec failure mode:
        // PR #5's 32-bit generation wrapped in ~12 h at 1e5 collects/s). One
        // counter for all shards keeps multi-shard ranges sound: any fresh
        // stamp exceeds every prior stamp, so MaxGenLocked can never hide a
        // write behind a stale generation from another shard.
        const std::uint64_t gen = generation_.fetch_add(1, std::memory_order_relaxed) + 1;
        slot.gens[block % kGenBlocksPerShard] = gen;
        newest = newest > gen ? newest : gen;
    }
    return newest;
}

#ifdef _WIN32
// One bounded GetWriteWatch pass: resets the watched bits for [base,
// base + bytes) and stamps every reported dirty page's block. Returns false
// (unknown) when the buffer cannot be allocated or the walk fails, which is
// exactly the uncommitted-page and non-write-watch-memory case from the spec.
bool WriteWatchTracker::CollectChunkWindows(std::uint64_t base, std::uint64_t bytes, std::uint64_t& newest) noexcept {
    // Caller holds the touched shards' locks and guarantees an in-window,
    // non-wrapping, nonempty range. Size the buffer from the page-aligned
    // bounds: ceil(bytes/4096) undercounts by one page when base is
    // unaligned (e.g. Collect(base+100, 4096) touches 2 pages), and a short
    // buffer drops dirty pages. Page-number arithmetic cannot overflow.
    const std::uint64_t firstPage = base / kPageStatePageBytes;
    const std::uint64_t lastPage = (base + bytes - 1) / kPageStatePageBytes;
    const std::uint64_t pageCount = lastPage - firstPage + 1;
    // ULONG_PTR is address-sized; the count is bounded by the chunk size, so
    // the cast below cannot truncate.
    ULONG_PTR* dirty = new (std::nothrow) ULONG_PTR[static_cast<std::size_t>(pageCount)];
    if (dirty == nullptr) {
        return false;
    }
    ULONG_PTR count = static_cast<ULONG_PTR>(pageCount);
    ULONG granularity = 0;
    // RESET in the same call: a single pass both reports and clears (the
    // spec's "single resetting GetWriteWatch pass"). The touched shards'
    // locks are held across this walk and the stamping below, so a
    // concurrent Collect can neither reset-then-miss nor miss-then-double:
    // every dirty page the walk returns gets stamped before any other walk
    // on these shards runs, and a later walk sees only post-stamp state.
    const DWORD result = GetWriteWatch(WRITE_WATCH_FLAG_RESET, reinterpret_cast<PVOID>(static_cast<std::uintptr_t>(base)),
                                       static_cast<SIZE_T>(bytes), reinterpret_cast<PVOID*>(dirty), &count, &granularity);
    (void)granularity;  // page addresses are absolute; mapping needs no stride
    bool ok = (result == 0);
    if (ok) {
        for (ULONG_PTR i = 0; i < count; ++i) {
            const std::uint64_t page = static_cast<std::uint64_t>(dirty[i]);
            bool complete = true;
            // One 4 KiB page stamps its whole 64 KiB block (spec granularity).
            const std::uint64_t stamped = StampRangeLocked(page, kPageStatePageBytes, complete);
            if (!complete) {
                ok = false;
                break;
            }
            newest = newest > stamped ? newest : stamped;
        }
    }
    delete[] dirty;
    return ok;
}
#endif

bool WriteWatchTracker::LockShardsAscending(std::uint64_t first, std::uint64_t last,
                                             std::vector<std::unique_lock<std::mutex>>& locks) noexcept {
    // Caller guarantees an initialized tracker and first <= last <
    // genShardCount_. Sequential ascending acquisition is deadlock-free (see
    // header): no path ever acquires a lower shard while holding a higher
    // one. Only the lock-storage allocation can fail (mutex::lock does not
    // throw); failure reports unknown instead of terminating.
    try {
        locks.reserve(static_cast<std::size_t>(last - first + 1));
        for (std::uint64_t shard = first; shard <= last; ++shard) {
            locks.emplace_back(genShards_[shard].mutex);
        }
    } catch (const std::bad_alloc&) {
        locks.clear();
        return false;
    }
    return true;
}

std::uint64_t WriteWatchTracker::Collect(std::uint64_t address, std::uint64_t bytes) noexcept {
    FlushHook hook = nullptr;
    void* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(hookMutex_);
        hook = hook_;
        context = hookContext_;
    }
    // The flush hook lands pending GPU writes before any CPU read of the
    // range proceeds; it runs with no tracker lock held and must not call
    // back into the tracker (reentrancy would deadlock on shard mutexes).
    if (hook != nullptr) {
        hook(context, address, bytes);
    }
    if (!InWindow(address, bytes)) {
        unknownWalks_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    // Hold every touched shard across the whole walk+stamp+read (see
    // LockShardsAscending): the resetting walk and the stamping of what it
    // returned must be atomic against concurrent Collects, or a reset in one
    // thread could hide a landed write from another thread's walk.
    const std::uint64_t firstShard = (address - base_) / kTrackerShardBytes;
    const std::uint64_t lastShard = (address + bytes - 1 - base_) / kTrackerShardBytes;
    std::vector<std::unique_lock<std::mutex>> shardLocks;
    if (!LockShardsAscending(firstShard, lastShard, shardLocks)) {
        unknownWalks_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
#ifdef _WIN32
    std::uint64_t newest = 0;
    for (std::uint64_t offset = 0; offset < bytes;) {
        std::uint64_t chunk = bytes - offset;
        chunk = chunk > kCollectChunkBytes ? kCollectChunkBytes : chunk;
        if (!CollectChunkWindows(address + offset, chunk, newest)) {
            unknownWalks_.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        offset += chunk;
    }
    // Merge walk stamps with pre-existing generations: a clean range still
    // reports its newest stamp, so "unchanged since g" is provable without
    // fresh dirt. All-zero ranges stay 0 (unknown: compare bytes).
    const std::uint64_t stored = MaxGenLocked(address, bytes);
    return stored > newest ? stored : newest;
#else
    // Non-Windows builds have no write-watch source; the page-state table
    // (platform-independent) still answers, but Collect reports unknown.
    unknownWalks_.fetch_add(1, std::memory_order_relaxed);
    return 0;
#endif
}

PinToken WriteWatchTracker::Pin(std::span<const AddrRange> ranges) noexcept {
    // Explicit pin accounting: each Pin mints a token whose record lives
    // until Unpin, so balance is checkable (double release fails loudly).
    // Per-Range pins++ inside the registry's interval map arrives with the
    // registry SRWLOCK rewrite (spec "Registry"); until then the token
    // record IS the pin the driver holds across a submission.
    std::uint64_t id = nextPin_.fetch_add(1, std::memory_order_relaxed);
    while (id == 0) {  // 0 is reserved for the null token; skip it on wrap
        id = nextPin_.fetch_add(1, std::memory_order_relaxed);
    }
    try {
        std::lock_guard<std::mutex> lock(pinMutex_);
        pins_.emplace(id, static_cast<std::uint64_t>(ranges.size()));
    } catch (const std::bad_alloc&) {
        // No guest-meaningful error code exists for host pin metadata OOM.
        Unsupported("WriteWatchTracker pin record allocation failed");
    }
    return PinToken{id};
}

void WriteWatchTracker::Unpin(PinToken token) noexcept {
    if (token.value == 0) {
        return;  // null token: matches NullTracker's inert Unpin
    }
    std::lock_guard<std::mutex> lock(pinMutex_);
    const auto it = pins_.find(token.value);
    if (it == pins_.end()) {
        // Unknown or already-released token: pin accounting is corrupt, and
        // a GPU submission may retire while still referenced. Abort loudly
        // instead of risking use-after-unpin on guest memory.
        Unsupported("WriteWatchTracker Unpin of unknown or released token");
    }
    pins_.erase(it);
}

PageState WriteWatchTracker::PageStateAt(std::uint64_t address) const noexcept {
    if (table_ != nullptr) {
        return table_->At(address);
    }
    // Unbound table: window membership is all we know. In-window reads as
    // Uncommitted (conservative: wait or compare), outside as NotGuest.
    if (genShards_ == nullptr || address < base_ || address >= end_) {
        return PageState::NotGuest;
    }
    return PageState::Uncommitted;
}

std::uint64_t WriteWatchTracker::MarkWritten(std::uint64_t address, std::uint64_t bytes) noexcept {
    // GPU-write path for M3 block-generation tracking (spec): reports which
    // bytes the GPU wrote by bumping the single global counter shared with
    // Collect, so CPU/GPU novelty share one ordering. No flush hook here:
    // the hook lands pending writes before CPU reads, while this *is* the
    // write being reported.
    if (!InWindow(address, bytes)) {
        return 0;
    }
    // Same shard discipline as Collect (see LockShardsAscending): stamping
    // must serialize against concurrent walks so a walk never observes a
    // half-stamped range.
    const std::uint64_t firstShard = (address - base_) / kTrackerShardBytes;
    const std::uint64_t lastShard = (address + bytes - 1 - base_) / kTrackerShardBytes;
    std::vector<std::unique_lock<std::mutex>> shardLocks;
    if (!LockShardsAscending(firstShard, lastShard, shardLocks)) {
        return 0;
    }
    bool complete = true;
    const std::uint64_t newest = StampRangeLocked(address, bytes, complete);
    return complete ? newest : 0;
}

void WriteWatchTracker::SetFlushHook(FlushHook hook, void* context) noexcept {
    std::lock_guard<std::mutex> lock(hookMutex_);
    hook_ = hook;
    hookContext_ = context;
}

std::uint64_t WriteWatchTracker::UnknownWalks() const noexcept {
    return unknownWalks_.load(std::memory_order_relaxed);
}

}  // namespace PortPS5::GuestMemory
