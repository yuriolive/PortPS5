// GuestAllocations.cpp: the guest allocation registry (ranges, protections,
// pins) shared by libc, libkernel and the AGC driver.
//
// Every access goes through the tracking recursive mutex taken by Mutation (or
// internally by the noexcept read-only queries such as Cover/Acquire). The
// mutating exports throw std::runtime_error on contract violations and are
// only called from host code that catches it; the noexcept queries never throw
// so APS5_VABI callers (GuestMemoryValidation) can use them directly.

#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestMemoryTracking.hpp"
#include <algorithm>
#include <limits>
#include <iterator>
#include <map>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <link.h>
#include <unistd.h>
#endif

namespace GuestAllocations {
namespace {

struct Registry {
    std::recursive_mutex& mutex = GuestMemoryTracking::GuestMemoryTrackingMutex_nid_postfix();
    std::map<std::uint64_t, std::shared_ptr<const Range>> ranges;
    bool mainImageRegistered = false;
};

Registry& registry() {
    static Registry value;
    return value;
}

void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

}

void* GuestAllocationsBegin_nid_postfix() {
    return new std::unique_lock<std::recursive_mutex>(registry().mutex);
}

void GuestAllocationsEnd_nid_postfix(void* mutation) noexcept {
    delete static_cast<std::unique_lock<std::recursive_mutex>*>(mutation);
}

#ifdef _WIN32
void GuestAllocationsRegisterMainImage_nid_postfix(void*) {
    auto& state = registry();
    if (state.mainImageRegistered) return;
    const auto image = GetModuleHandleW(nullptr);
    require(image != nullptr, "cannot locate the main guest image");
    auto replacement = state.ranges;
    auto cursor = reinterpret_cast<std::uintptr_t>(image);
    bool registered = false;
    for (;;) {
        MEMORY_BASIC_INFORMATION memory{};
        require(VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) == sizeof(memory), "cannot query the main guest image");
        if (memory.AllocationBase != image) break;
        require(memory.Type == MEM_IMAGE && (memory.State == MEM_COMMIT || memory.State == MEM_RESERVE), "unsupported guest image mapping");
        require(reinterpret_cast<std::uintptr_t>(memory.BaseAddress) == cursor && memory.RegionSize != 0 && memory.RegionSize <= std::numeric_limits<std::uintptr_t>::max() - cursor, "invalid guest image range");
        if (memory.State == MEM_COMMIT) {
            require((memory.Protect & PAGE_GUARD) == 0, "guarded guest image pages are not supported");
            const auto protection = memory.Protect & 0xffu;
            const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            const bool readable = writable || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
            require(readable || protection == PAGE_NOACCESS || protection == PAGE_EXECUTE, "unsupported guest image protection");
            const auto next = replacement.lower_bound(cursor);
            require(next == replacement.end() || cursor + memory.RegionSize <= next->first, "guest image overlaps a registered allocation");
            if (next != replacement.begin()) {
                const auto& previous = *std::prev(next)->second;
                require(previous.address + previous.bytes <= cursor, "guest image overlaps a registered allocation");
            }
            replacement.emplace(cursor, std::make_shared<const Range>(Range{cursor, memory.RegionSize, readable, writable, cursor, memory.RegionSize, false}));
            registered = true;
        }
        cursor += memory.RegionSize;
    }
    require(registered, "main guest image has no committed pages");
    state.ranges.swap(replacement);
    state.mainImageRegistered = true;
}
#else
void GuestAllocationsRegisterMainImage_nid_postfix(void*) {
    auto& state = registry();
    if (state.mainImageRegistered) return;
    struct Page {
        bool readable = false;
        bool writable = false;
    };
    std::map<std::uint64_t, Page> pages;
    const auto pageSize = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
    std::pair<std::map<std::uint64_t, Page>*, std::uint64_t> collection{&pages, pageSize};
    dl_iterate_phdr([](dl_phdr_info* image, std::size_t, void* data) {
        auto& collected = *static_cast<std::pair<std::map<std::uint64_t, Page>*, std::uint64_t>*>(data);
        for (int index = 0; index < image->dlpi_phnum; ++index) {
            const auto& header = image->dlpi_phdr[index];
            if (header.p_type != PT_LOAD || header.p_memsz == 0) continue;
            const auto start = (image->dlpi_addr + header.p_vaddr) & ~(collected.second - 1);
            const auto end = (image->dlpi_addr + header.p_vaddr + header.p_memsz + collected.second - 1) & ~(collected.second - 1);
            for (auto page = start; page < end; page += collected.second) {
                auto& entry = (*collected.first)[page];
                entry.readable = entry.readable || (header.p_flags & (PF_R | PF_W)) != 0;
                entry.writable = entry.writable || (header.p_flags & PF_W) != 0;
            }
        }
        return 1;
    }, &collection);
    require(!pages.empty(), "main guest image has no loadable segments");
    auto replacement = state.ranges;
    for (auto page = pages.begin(); page != pages.end();) {
        auto last = page;
        while (std::next(last) != pages.end() && std::next(last)->first == last->first + pageSize && std::next(last)->second.readable == page->second.readable && std::next(last)->second.writable == page->second.writable) ++last;
        const auto address = page->first;
        const auto bytes = static_cast<std::size_t>(last->first + pageSize - address);
        const auto next = replacement.lower_bound(address);
        require(next == replacement.end() || address + bytes <= next->first, "guest image overlaps a registered allocation");
        if (next != replacement.begin()) {
            const auto& previous = *std::prev(next)->second;
            require(previous.address + previous.bytes <= address, "guest image overlaps a registered allocation");
        }
        replacement.emplace(address, std::make_shared<const Range>(Range{address, bytes, page->second.readable, page->second.writable, address, bytes, false}));
        page = std::next(last);
    }
    state.ranges.swap(replacement);
    state.mainImageRegistered = true;
}
#endif

void GuestAllocationsAdd_nid_postfix(void*, void* pointer, std::size_t bytes, bool readable, bool writable) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    require(address != 0 && bytes <= std::numeric_limits<std::uint64_t>::max() - address, "invalid guest allocation range");
    require(!writable || readable, "writable guest allocation must be readable");
    auto& ranges = registry().ranges;
    const auto next = ranges.lower_bound(address);
    require(next == ranges.end() || (next->first != address && address + bytes <= next->first), "overlapping guest allocation");
    if (next != ranges.begin()) {
        const auto& previous = *std::prev(next)->second;
        require(previous.address + previous.bytes <= address, "overlapping guest allocation");
    }
    ranges.emplace(address, std::make_shared<const Range>(Range{address, bytes, readable, writable, address, bytes}));
}

void GuestAllocationsRequireUnpinned_nid_postfix(void*, const void* pointer, std::size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    require(bytes <= std::numeric_limits<std::uint64_t>::max() - address, "guest allocation range overflow");
    GuestMemoryTracking::GuestMemoryTrackingInvalidate_nid_postfix(address, bytes);
    const auto end = address + bytes;
    for (const auto& [base, range] : registry().ranges) {
        if (base >= end && base != address) break;
        if ((address < base + range->bytes && base < end) || base == address) require(range.use_count() == 1, "guest allocation is owned by an active GPU command");
    }
}

void GuestAllocationsRequireAvailable_nid_postfix(void*, const void* pointer, std::size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    require(address != 0 && bytes != 0 && bytes <= std::numeric_limits<std::uint64_t>::max() - address, "invalid fixed guest mapping");
    for (const auto& [base, range] : registry().ranges) {
        if (base >= address + bytes) break;
        require(base + range->bytes <= address, "fixed mapping overlaps a registered guest allocation");
    }
}

Range GuestAllocationsFind_nid_postfix(void*, const void* pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const auto& [base, range] : registry().ranges) {
        if (range->allocationAddress == address) {
            require(range->releasable, "guest image memory is not a releasable allocation");
            return {address, range->allocationBytes, range->readable, range->writable, address, range->allocationBytes, range->releasable};
        }
    }
    throw std::runtime_error("guest allocation is not registered");
}

void GuestAllocationsRemove_nid_postfix(void* mutation, const void* pointer) {
    const auto range = GuestAllocationsFind_nid_postfix(mutation, pointer);
    GuestAllocationsRequireUnpinned_nid_postfix(mutation, pointer, range.bytes);
    std::erase_if(registry().ranges, [&](const auto& entry) { return entry.second->allocationAddress == range.address; });
}

namespace {

std::map<std::uint64_t, std::shared_ptr<const Range>> replaceRange(const void* pointer, std::size_t bytes, bool remove, bool readable, bool writable) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    require(bytes != 0 && bytes <= std::numeric_limits<std::uint64_t>::max() - address, "invalid guest protection or unmap range");
    require(!writable || readable, "writable guest allocation must be readable");
    const auto end = address + bytes;
    auto replacement = registry().ranges;
    auto cursor = address;
    for (const auto& [base, entry] : registry().ranges) {
        const auto& range = *entry;
        const auto finish = base + range.bytes;
        if (finish <= address) continue;
        if (base >= end) break;
        require(base <= cursor, "guest protection or unmap range has a hole");
        replacement.erase(base);
        const auto insert = [&](std::uint64_t first, std::uint64_t last, bool canRead, bool canWrite) {
            if (first < last) replacement.emplace(first, std::make_shared<const Range>(Range{first, static_cast<std::size_t>(last - first), canRead, canWrite, range.allocationAddress, range.allocationBytes, range.releasable}));
        };
        insert(base, std::max(base, address), range.readable, range.writable);
        if (!remove) insert(std::max(base, address), std::min(finish, end), readable, writable);
        insert(std::min(finish, end), finish, range.readable, range.writable);
        cursor = std::min(finish, end);
    }
    require(cursor == end, "guest protection or unmap range is not registered");
    return replacement;
}

}

void GuestAllocationsProtect_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, bool readable, bool writable, const std::function<void()>& apply) {
    GuestAllocationsRequireUnpinned_nid_postfix(mutation, pointer, bytes);
    auto replacement = replaceRange(pointer, bytes, false, readable, writable);
    apply();
    registry().ranges.swap(replacement);
}

/**
 * Walks the registered fragments (not the allocation extents) that overlap the
 * request, so a hole left inside an allocation by an earlier partial unmap, or
 * a gap between allocations, is skipped instead of being treated as a
 * registered range. All pieces are collected and checked for releasability
 * before the first host unmap, so a request that touches image memory fails
 * without unmapping anything. Each piece is then applied and committed to the
 * registry separately: if the host unmap of piece N throws, pieces 0..N-1 are
 * already gone from both the host and the registry.
 */
void GuestAllocationsUnmap_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, const std::function<void(const void*, std::size_t, const void*, bool)>& apply) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    require(bytes != 0 && bytes <= std::numeric_limits<std::uint64_t>::max() - address, "invalid guest unmap range");
    GuestAllocationsRequireUnpinned_nid_postfix(mutation, pointer, bytes);
    const auto end = address + bytes;
    struct Piece {
        std::uint64_t first;
        std::uint64_t last;
        std::uint64_t allocation;
    };
    std::vector<Piece> pieces;
    auto& ranges = registry().ranges;
    auto it = ranges.upper_bound(address);
    if (it != ranges.begin()) --it;  // the one fragment starting before `address` that may overlap it
    for (; it != ranges.end() && it->first < end; ++it) {
        const auto& range = *it->second;
        const auto finish = range.address + range.bytes;
        if (finish <= address) continue;
        require(range.releasable, "guest image memory cannot be unmapped");
        pieces.push_back({std::max<std::uint64_t>(range.address, address), std::min<std::uint64_t>(finish, end), range.allocationAddress});
    }
    require(!pieces.empty(), "unmap address is not registered");
    for (const auto& piece : pieces) {
        auto replacement = replaceRange(reinterpret_cast<const void*>(piece.first), piece.last - piece.first, true, false, false);
        bool last = true;
        for (const auto& [base, entry] : replacement) {
            if (entry->allocationAddress == piece.allocation) last = false;
        }
        apply(reinterpret_cast<const void*>(piece.first), static_cast<std::size_t>(piece.last - piece.first), reinterpret_cast<const void*>(piece.allocation), last);
        ranges.swap(replacement);
    }
}

Lease GuestAllocationsAcquire_nid_postfix() {
    std::lock_guard lock(registry().mutex);
    Lease result;
    for (const auto& [address, range] : registry().ranges) {
        if (range->readable && range->bytes != 0) result.push_back(range);
    }
    return result;
}

}
