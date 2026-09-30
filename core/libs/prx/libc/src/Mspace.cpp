// sceLibcMspace* replacement allocator.
//
// Subsystem: libc.prx (docs/spec/libc.md, "Mspace"). An mspace is a guest-supplied memory region that the
// guest carves up with malloc-style calls. Ported from AnyPS5 (upstream commits 3058980d, 02b431eb, 8f982168).
//
// Design: the allocator metadata lives on the HOST heap, never inside the guest region. The guest region is
// only ever handed out to callers, so guest writes can not corrupt allocator state, and a handle is just the
// region base address. Each arena keeps
//   * `chunks`: an ordered map start -> {end, used, requested} that tiles [base + ArenaHeaderBytes, end)
//     without gaps, so neighbours are found in O(log n) and free neighbours coalesce on release;
//   * `free`: an ordered set of (size, start) for best-fit lookup in O(log n).
// Every chunk boundary is 16-byte aligned (Granule).
//
// Threading: one process-global mutex (`arenaMutex`) guards every arena. The MspaceThreadUnsafe creation flag
// is accepted and ignored (locking anyway is always safe).
//
// Errors: failures return nullptr / an errno value and set the guest errno through __error(); nothing here
// throws, because this code is reached from guest frames.
#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
extern "C" int* APS5_VABI __error_nid_postfix();

namespace {

constexpr unsigned MspaceThreadUnsafe = 1;
// Every allocation start and size is a multiple of this (FreeBSD malloc alignment guarantee).
constexpr std::uintptr_t Granule = 16;
// Bytes at the start of a region the allocator never hands out. The historical header lived in-band; we keep
// the reservation so that an allocation never starts at the handle address itself (handle == region base).
constexpr std::uintptr_t ArenaHeaderBytes = 64;
constexpr int GuestEinval = 22;
constexpr int GuestEnomem = 12;

struct Chunk {
    std::uintptr_t end;       // One past the last byte of the chunk; the next chunk starts here.
    bool used;
    std::size_t requested;    // Size the caller asked for; bounds how many bytes realloc must copy.
};

struct Arena {
    std::uintptr_t base;
    std::uintptr_t end;
    std::map<std::uintptr_t, Chunk> chunks;
    std::set<std::pair<std::size_t, std::uintptr_t>> free;
    std::size_t inUse = 0;
    std::size_t peakInUse = 0;
};

std::mutex arenaMutex;
std::map<std::uintptr_t, std::unique_ptr<Arena>> arenas;

void Error(int value) { *__error_nid_postfix() = value; }

std::uintptr_t AlignUp(std::uintptr_t value, std::uintptr_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Rounds a request up to Granule. Returns false when the rounding (or the request itself) would overflow,
// which must be reported as ENOMEM: without this check a request near SIZE_MAX wraps to a tiny size.
bool RoundRequest(std::size_t size, std::uintptr_t& rounded) {
    const auto request = std::max<std::size_t>(size, 1);
    if (request > std::numeric_limits<std::uintptr_t>::max() - (Granule - 1)) return false;
    rounded = AlignUp(request, Granule);
    return true;
}

Arena* Find(void* handle) {
    const auto found = arenas.find(reinterpret_cast<std::uintptr_t>(handle));
    if (found != arenas.end()) return found->second.get();
    Error(GuestEinval);
    return nullptr;
}

// Unregisters every arena whose handle lies in [start, end). A nested mspace lives inside its parent's
// allocation, so it stops existing when that allocation (or the parent arena) is freed or destroyed; leaving it
// registered would make later creates over the reused memory fail as "overlapping". Caller holds arenaMutex.
// The arena being modified is never erased by this: its own handle is below the range it manages.
void DropArenasIn(std::uintptr_t start, std::uintptr_t end) {
    arenas.erase(arenas.lower_bound(start), arenas.lower_bound(end));
}

void AddFree(Arena& arena, std::uintptr_t start, std::uintptr_t end) {
    arena.chunks[start] = {end, false, 0};
    arena.free.emplace(end - start, start);
}

void RemoveFree(Arena& arena, std::map<std::uintptr_t, Chunk>::iterator chunk) {
    arena.free.erase({chunk->second.end - chunk->first, chunk->first});
}

// Best-fit allocation of `size` bytes at `alignment` (raised to at least Granule). Caller holds arenaMutex.
void* Allocate(Arena* arena, std::size_t size, std::size_t alignment) {
    if (!arena) return nullptr;
    alignment = std::max<std::size_t>(alignment, Granule);
    std::uintptr_t needed = 0;
    if (!RoundRequest(size, needed) || needed > std::numeric_limits<std::uintptr_t>::max() - alignment) {
        Error(GuestEnomem);
        return nullptr;
    }
    for (auto candidate = arena->free.lower_bound({needed, 0}); candidate != arena->free.end(); ++candidate) {
        const auto start = candidate->second;
        const auto chunk = arena->chunks.find(start);
        const auto end = chunk->second.end;
        const auto aligned = AlignUp(start, alignment);
        // `aligned < start` catches wrap-around for huge alignments.
        if (aligned < start || aligned > end || end - aligned < needed) continue;
        RemoveFree(*arena, chunk);
        arena->chunks.erase(chunk);
        if (aligned > start) AddFree(*arena, start, aligned);
        const auto finish = aligned + needed;
        if (finish < end) AddFree(*arena, finish, end);
        arena->chunks[aligned] = {finish, true, size};
        arena->inUse += needed;
        arena->peakInUse = std::max(arena->peakInUse, arena->inUse);
        return reinterpret_cast<void*>(aligned);
    }
    Error(GuestEnomem);
    return nullptr;
}

// Looks up a live allocation by its exact start address; sets EINVAL when `pointer` is not one.
bool FindUsed(Arena* arena, const void* pointer, std::map<std::uintptr_t, Chunk>::iterator& chunk) {
    if (arena && pointer) {
        chunk = arena->chunks.find(reinterpret_cast<std::uintptr_t>(pointer));
        if (chunk != arena->chunks.end() && chunk->second.used) return true;
    }
    Error(GuestEinval);
    return false;
}

// Frees a chunk and merges it with free neighbours so fragmentation heals.
void Release(Arena& arena, std::map<std::uintptr_t, Chunk>::iterator chunk) {
    auto start = chunk->first;
    auto end = chunk->second.end;
    DropArenasIn(start, end);  // nested mspaces inside the released block die with it
    arena.inUse -= end - start;
    if (chunk != arena.chunks.begin()) {
        const auto previous = std::prev(chunk);
        if (!previous->second.used) {
            start = previous->first;
            RemoveFree(arena, previous);
            arena.chunks.erase(previous);
        }
    }
    const auto next = std::next(chunk);
    if (next != arena.chunks.end() && !next->second.used) {
        end = next->second.end;
        RemoveFree(arena, next);
        arena.chunks.erase(next);
    }
    arena.chunks.erase(chunk);
    AddFree(arena, start, end);
}

// True when [start, start + size) lies wholly inside one live allocation of `arena`. Used to allow an mspace
// to be created inside another mspace's allocation (nested mspaces) while still rejecting partial overlap.
bool InsideAllocation(const Arena& arena, std::uintptr_t start, std::size_t size) {
    auto found = arena.chunks.upper_bound(start);
    if (found == arena.chunks.begin()) return false;
    --found;
    return found->second.used && size <= found->second.end - start && start - found->first <= found->second.end - found->first - size;
}

// Guest-visible layout of the struct filled by sceLibcMspaceMallocStats*.
struct MallocManagedSize {
    std::uint16_t size;
    std::uint16_t version;
    std::uint32_t reserved;
    std::size_t maxSystemSize;
    std::size_t currentSystemSize;
    std::size_t maxInuseSize;
    std::size_t currentInuseSize;
};
static_assert(sizeof(MallocManagedSize) == 0x28);

int FillStats(void* handle, MallocManagedSize* stats) {
    if (!stats || stats->size < sizeof(MallocManagedSize)) return GuestEinval;
    std::lock_guard lock(arenaMutex);
    auto* arena = Find(handle);
    if (!arena) return GuestEinval;
    const auto system = arena->end - arena->base;
    stats->maxSystemSize = system;
    stats->currentSystemSize = system;
    stats->maxInuseSize = arena->peakInUse;
    stats->currentInuseSize = arena->inUse;
    return 0;
}

bool ValidAlignment(std::size_t alignment) { return alignment != 0 && (alignment & (alignment - 1)) == 0; }

}

extern "C" {
// Creates an mspace over [base, base + size). Returns the handle (== base), or nullptr with EINVAL for a
// null/misaligned/too-small region, unknown flags, or a region that partially overlaps an existing mspace.
void* APS5_VABI sceLibcMspaceCreate_nid_postfix(const char* name, void* base,
                                              std::size_t size, unsigned flags) {
    (void)name;
    const auto start = reinterpret_cast<std::uintptr_t>(base);
    if (!base || (start & (Granule - 1)) || size < ArenaHeaderBytes + 2 * Granule ||
        size > std::numeric_limits<std::uintptr_t>::max() - start || (flags & ~MspaceThreadUnsafe) != 0) {
        Error(GuestEinval);
        return nullptr;
    }
    std::lock_guard lock(arenaMutex);
    for (const auto& [otherBase, other] : arenas) {
        if (start < other->end && otherBase < start + size && !InsideAllocation(*other, start, size)) {
            Error(GuestEinval);
            return nullptr;
        }
    }
    auto arena = std::make_unique<Arena>();
    arena->base = start;
    arena->end = start + size;
    const auto first = start + ArenaHeaderBytes;
    const auto last = start + (size & ~(Granule - 1));
    AddFree(*arena, first, last);
    arenas.emplace(start, std::move(arena));
    return base;
}

// Destroys an mspace. Returns 0, or -1 with EINVAL for an unknown handle. Outstanding allocations are simply
// forgotten (the guest owns the backing memory).
int APS5_VABI sceLibcMspaceDestroy_nid_postfix(void* handle) {
    std::lock_guard lock(arenaMutex);
    const auto found = arenas.find(reinterpret_cast<std::uintptr_t>(handle));
    if (found != arenas.end()) {
        DropArenasIn(found->first, found->second->end);  // the arena itself plus every nested arena
        return 0;
    }
    Error(GuestEinval);
    return -1;
}

/// malloc from the mspace `handle`: 16-byte aligned, nullptr + ENOMEM when full, nullptr + EINVAL for an unknown handle.
void* APS5_VABI sceLibcMspaceMalloc_nid_postfix(void* handle, std::size_t size) {
    std::lock_guard lock(arenaMutex);
    return Allocate(Find(handle), size, Granule);
}

/// free: null is a no-op; a pointer that is not a live allocation of `handle` is ignored with errno EINVAL.
void APS5_VABI sceLibcMspaceFree_nid_postfix(void* handle, void* pointer) {
    if (!pointer) return;
    std::lock_guard lock(arenaMutex);
    auto* arena = Find(handle);
    std::map<std::uintptr_t, Chunk>::iterator chunk;
    if (FindUsed(arena, pointer, chunk)) Release(*arena, chunk);
}

/// calloc: zeroed allocation of count*size bytes; nullptr + ENOMEM on overflow or exhaustion.
void* APS5_VABI sceLibcMspaceCalloc_nid_postfix(void* handle, std::size_t count, std::size_t size) {
    if (size && count > std::numeric_limits<std::size_t>::max() / size) {
        Error(GuestEnomem);
        return nullptr;
    }
    std::lock_guard lock(arenaMutex);
    void* result = Allocate(Find(handle), count * size, Granule);
    if (result) std::memset(result, 0, count * size);
    return result;
}

// realloc semantics: null pointer == malloc, size 0 frees, failure leaves the original block intact.
// Growth first tries to absorb a free neighbour so the block does not move.
void* APS5_VABI sceLibcMspaceRealloc_nid_postfix(void* handle, void* pointer, std::size_t size) {
    std::lock_guard lock(arenaMutex);
    auto* arena = Find(handle);
    if (!pointer) return Allocate(arena, size, Granule);
    std::map<std::uintptr_t, Chunk>::iterator chunk;
    if (!FindUsed(arena, pointer, chunk)) return nullptr;
    if (!size) { Release(*arena, chunk); return nullptr; }
    std::uintptr_t needed = 0;
    if (!RoundRequest(size, needed)) { Error(GuestEnomem); return nullptr; }
    const auto start = chunk->first;
    const auto capacity = chunk->second.end - start;
    if (needed <= capacity) {
        chunk->second.requested = size;
        return pointer;
    }
    const auto next = std::next(chunk);
    if (next != arena->chunks.end() && !next->second.used && next->first == chunk->second.end && next->second.end - start >= needed) {
        const auto nextEnd = next->second.end;
        RemoveFree(*arena, next);
        arena->chunks.erase(next);
        const auto finish = start + needed;
        if (finish < nextEnd) AddFree(*arena, finish, nextEnd);
        arena->inUse += needed - capacity;
        arena->peakInUse = std::max(arena->peakInUse, arena->inUse);
        arena->chunks[start] = {finish, true, size};
        return pointer;
    }
    const auto previousSize = chunk->second.requested;
    void* result = Allocate(arena, size, Granule);
    if (result) {
        std::memcpy(result, pointer, previousSize);
        Release(*arena, arena->chunks.find(start));
    }
    return result;
}

/// posix_memalign: stores an aligned block in *result and returns 0; EINVAL (bad alignment/null result/unknown
/// handle, *result untouched) or ENOMEM.
int APS5_VABI sceLibcMspacePosixMemalign_nid_postfix(void* handle, void** result,
                                                   std::size_t alignment, std::size_t size) {
    if (!result || alignment < sizeof(void*) || (alignment & (alignment - 1))) return GuestEinval;
    std::lock_guard lock(arenaMutex);
    auto* arena = Find(handle);
    if (!arena) return GuestEinval;
    void* pointer = Allocate(arena, size, alignment);
    if (!pointer) return GuestEnomem;
    *result = pointer;
    return 0;
}

/// memalign: power-of-two alignment (smaller values are promoted to 16); nullptr + EINVAL for bad alignment,
/// nullptr + ENOMEM when nothing fits.
void* APS5_VABI sceLibcMspaceMemalign_nid_postfix(void* handle, std::size_t alignment, std::size_t size) {
    if (!ValidAlignment(alignment)) {
        Error(GuestEinval);
        return nullptr;
    }
    std::lock_guard lock(arenaMutex);
    return Allocate(Find(handle), size, alignment);
}

// reallocalign: like realloc, but the result is also aligned to `alignment` (power of two). A block that is
// already aligned and large enough is resized in place; otherwise it is moved.
void* APS5_VABI sceLibcMspaceReallocalign_nid_postfix(void* handle, void* pointer, std::size_t size, std::size_t alignment) {
    if (!ValidAlignment(alignment)) {
        Error(GuestEinval);
        return nullptr;
    }
    const auto effective = std::max<std::size_t>(alignment, Granule);
    std::lock_guard lock(arenaMutex);
    auto* arena = Find(handle);
    if (!pointer) return Allocate(arena, size, effective);
    std::map<std::uintptr_t, Chunk>::iterator chunk;
    if (!FindUsed(arena, pointer, chunk)) return nullptr;
    if (!size) { Release(*arena, chunk); return nullptr; }
    std::uintptr_t needed = 0;
    if (!RoundRequest(size, needed)) { Error(GuestEnomem); return nullptr; }
    const auto start = chunk->first;
    if (needed <= chunk->second.end - start && (start & (effective - 1)) == 0) {
        chunk->second.requested = size;
        return pointer;
    }
    const auto previousSize = chunk->second.requested;
    void* result = Allocate(arena, size, effective);
    if (result) {
        std::memcpy(result, pointer, std::min(previousSize, size));
        Release(*arena, arena->chunks.find(start));
    }
    return result;
}

/// malloc_stats: fills the 0x28-byte stats struct (system size = region size, current/peak in-use bytes).
/// Returns 0, or EINVAL for a null/short struct or unknown handle.
int APS5_VABI sceLibcMspaceMallocStats_nid_postfix(void* handle, MallocManagedSize* stats) {
    return FillStats(handle, stats);
}

/// malloc_stats_fast: identical to malloc_stats (every query is already O(1)).
int APS5_VABI sceLibcMspaceMallocStatsFast_nid_postfix(void* handle, MallocManagedSize* stats) {
    return FillStats(handle, stats);
}

// Returns the usable capacity of a live allocation (its 16-byte-rounded chunk size), 0 + EINVAL otherwise.
std::size_t APS5_VABI sceLibcMspaceMallocUsableSize_nid_postfix(const void* pointer) {
    if (!pointer) return 0;
    std::lock_guard lock(arenaMutex);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    // Walk candidate arenas from the innermost (highest base <= address) outwards, since nested mspaces
    // can place an inner arena inside an allocation of an outer one.
    for (auto arena = arenas.upper_bound(address); arena != arenas.begin();) {
        --arena;
        if (address >= arena->second->end) continue;
        const auto found = arena->second->chunks.find(address);
        if (found != arena->second->chunks.end() && found->second.used) return found->second.end - found->first;
    }
    Error(GuestEinval);
    return 0;
}
}
