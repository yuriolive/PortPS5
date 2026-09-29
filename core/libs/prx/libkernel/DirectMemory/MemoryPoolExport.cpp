/**
 * @file MemoryPoolExport.cpp
 * @brief Exports and implementation of PS5 kernel memory pool lifecycle and batch APIs.
 * 
 * Provides sceKernelMemoryPoolBatch, Commit, Decommit, Expand, Reserve, and GetBlockStats
 * implementations adhering to System V ABI without throwing host exceptions.
 */

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <map>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "DirectMemory.hpp"
#include "MemoryPool.hpp"

namespace {

constexpr size_t kPoolBlockSize = 2ULL * 1024 * 1024;

// Global state tracking memory pool expanded and committed capacity.
struct PoolState {
    std::mutex mutex;
    size_t expandedBytes = 0;
    size_t committedBytes = 0;
    std::map<uintptr_t, size_t> committedRanges;
};

/**
 * @brief Returns the global state for memory pool allocations and committed bytes.
 * @return Reference to the PoolState singleton.
 */
PoolState& Pool() {
    static PoolState state;
    return state;
}

/**
 * @brief Checks if an address or length is aligned to the PS5 page boundary.
 * @param v Value to check.
 * @return true if page-aligned, false otherwise.
 */
bool PageAligned(uint64_t v) { return (v & (PS5_PAGE_SIZE - 1)) == 0; }

/**
 * @brief Commits backing memory pages for a memory pool reservation range.
 * @param addr Virtual address to commit.
 * @param len Size in bytes.
 * @param prot Protection flags.
 * @return 0 on success, or SCE error code on failure.
 */
int PoolCommit(void* addr, uint64_t len, int prot) {
    if (!addr || len == 0 || !PageAligned(reinterpret_cast<uintptr_t>(addr)) || !PageAligned(len)) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const int ret = DoMprotect(addr, static_cast<size_t>(len), prot);
    if (ret == 0) {
        PoolState& pool = Pool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        const uintptr_t start = reinterpret_cast<uintptr_t>(addr);
        const uintptr_t end = start + static_cast<size_t>(len);

        // Calculate overlap with existing committed ranges to only increment newly committed bytes.
        size_t overlap = 0;
        auto it = pool.committedRanges.upper_bound(start);
        if (it != pool.committedRanges.begin() && std::prev(it)->first + std::prev(it)->second > start) {
            --it;
        }
        uintptr_t mergedStart = start;
        uintptr_t mergedEnd = end;
        while (it != pool.committedRanges.end() && it->first < end) {
            uintptr_t rStart = it->first;
            uintptr_t rEnd = rStart + it->second;
            uintptr_t oStart = std::max(start, rStart);
            uintptr_t oEnd = std::min(end, rEnd);
            if (oStart < oEnd) {
                overlap += (oEnd - oStart);
            }
            mergedStart = std::min(mergedStart, rStart);
            mergedEnd = std::max(mergedEnd, rEnd);
            it = pool.committedRanges.erase(it);
        }
        pool.committedRanges[mergedStart] = mergedEnd - mergedStart;
        const size_t newBytes = static_cast<size_t>(len) - overlap;
        pool.committedBytes += newBytes;
    }
    return ret;
}

/**
 * @brief Decommits virtual memory pages from a memory pool reservation range.
 * @param addr Virtual address to decommit.
 * @param len Size in bytes.
 * @return 0 on success, or SCE error code on failure.
 */
int PoolDecommit(void* addr, uint64_t len) {
    if (!addr || len == 0 || !PageAligned(reinterpret_cast<uintptr_t>(addr)) || !PageAligned(len)) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const int ret = DoMprotect(addr, static_cast<size_t>(len), 0);
    if (ret == 0) {
        PoolState& pool = Pool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        const uintptr_t start = reinterpret_cast<uintptr_t>(addr);
        const uintptr_t end = start + static_cast<size_t>(len);

        size_t decommitted = 0;
        auto it = pool.committedRanges.upper_bound(start);
        if (it != pool.committedRanges.begin() && std::prev(it)->first + std::prev(it)->second > start) {
            --it;
        }
        while (it != pool.committedRanges.end() && it->first < end) {
            uintptr_t rStart = it->first;
            uintptr_t rEnd = rStart + it->second;
            uintptr_t oStart = std::max(start, rStart);
            uintptr_t oEnd = std::min(end, rEnd);
            if (oStart < oEnd) {
                decommitted += (oEnd - oStart);
            }
            it = pool.committedRanges.erase(it);
            if (rStart < start) {
                pool.committedRanges[rStart] = start - rStart;
            }
            if (rEnd > end) {
                pool.committedRanges[end] = rEnd - end;
            }
        }
        pool.committedBytes -= (decommitted < pool.committedBytes) ? decommitted : pool.committedBytes;
    }
    return ret;
}

}  // namespace

extern "C" {

/**
 * @brief sceKernelMemoryPoolBatch implementation.
 *
 * Invoked by guest code using System V ABI calling convention to perform batched
 * commit, decommit, and memory protection operations on memory pool reservations.
 * Returns 0 on success, or SCE_KERNEL_ERROR_EINVAL on invalid batch entry.
 */
int APS5_VABI sceKernelMemoryPoolBatch(const KernelMemoryPoolBatchEntry* entries, int num_entries, int* num_entries_out, int flags) {
    (void)flags;
    if (!entries || num_entries < 0) return SCE_KERNEL_ERROR_EINVAL;
    int done = 0;
    int ret = 0;
    for (; done < num_entries; ++done) {
        const KernelMemoryPoolBatchEntry& e = entries[done];
        switch (e.op) {
            case 1:
                ret = PoolCommit(e.commit.addr, e.commit.len, e.commit.prot);
                break;
            case 2:
                ret = PoolDecommit(e.decommit.addr, e.decommit.len);
                break;
            case 3:
                ret = DoMprotect(e.protect.addr, static_cast<size_t>(e.protect.len), e.protect.prot);
                break;
            case 4:
                ret = DoMprotect(e.type_protect.addr, static_cast<size_t>(e.type_protect.len), e.type_protect.prot);
                break;
            default:
                ret = SCE_KERNEL_ERROR_EINVAL;
                break;
        }
        if (ret != 0) break;
    }
    if (num_entries_out) *num_entries_out = done;
    return ret;
}

/**
 * @brief sceKernelMemoryPoolCommit implementation.
 *
 * Invoked by guest code using System V ABI calling convention to commit physical memory
 * backing for an address range within a previously reserved memory pool region.
 * Returns 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolCommit(void* addr, size_t len, int type, int prot, int flags) {
    (void)type;
    (void)flags;
    return PoolCommit(addr, len, prot);
}

/**
 * @brief sceKernelMemoryPoolDecommit implementation.
 *
 * Invoked by guest code using System V ABI calling convention to decommit memory pages
 * from a memory pool range, freeing backing memory and reducing committed count.
 * Returns 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolDecommit(void* addr, size_t len, int flags) {
    (void)flags;
    return PoolDecommit(addr, len);
}

/**
 * @brief sceKernelMemoryPoolExpand implementation.
 *
 * Invoked by guest code using System V ABI calling convention to expand physical backing
 * capacity of the memory pool by allocating direct memory within [search_start, search_end).
 * Returns 0 on success, or SCE_KERNEL_ERROR_EINVAL / SCE_KERNEL_ERROR_EAGAIN on failure.
 */
int APS5_VABI sceKernelMemoryPoolExpand(int64_t search_start, int64_t search_end, size_t len, size_t alignment, int64_t* phys_addr_out) {
    if (search_start < 0 || search_end <= search_start || len == 0 || !PageAligned(len) || !phys_addr_out
        || (alignment != 0 && !PageAligned(alignment))) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const int ret = DirectMemoryAlloc(search_start, search_end, len, alignment, -1, phys_addr_out);
    if (ret == 0) {
        PoolState& pool = Pool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.expandedBytes += len;
    }
    return ret;
}

/**
 * @brief sceKernelMemoryPoolGetBlockStats implementation.
 *
 * Invoked by guest code using System V ABI calling convention to query current allocated
 * and available block statistics for the memory pool based on committed and expanded bytes.
 * Returns 0 on success, or SCE_KERNEL_ERROR_EINVAL if output is null or buffer is undersized.
 */
int APS5_VABI sceKernelMemoryPoolGetBlockStats(KernelMemoryPoolBlockStats* output, size_t output_size) {
    if (!output || output_size < sizeof(KernelMemoryPoolBlockStats)) return SCE_KERNEL_ERROR_EINVAL;
    PoolState& pool = Pool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    const size_t total = (pool.expandedBytes + kPoolBlockSize - 1) / kPoolBlockSize;
    const size_t used = (pool.committedBytes + kPoolBlockSize - 1) / kPoolBlockSize;
    output->available_flushed_blocks = static_cast<int32_t>(total > used ? total - used : 0);
    output->available_cached_blocks = 0;
    output->allocated_flushed_blocks = static_cast<int32_t>(used);
    output->allocated_cached_blocks = 0;
    return 0;
}

/**
 * @brief sceKernelMemoryPoolReserve implementation.
 *
 * Invoked by guest code using System V ABI calling convention to reserve a virtual address
 * range for memory pool usage without committed backing pages.
 * Returns 0 on success, or SCE_KERNEL_ERROR_EINVAL on invalid parameters.
 */
int APS5_VABI sceKernelMemoryPoolReserve(void* addr_in, size_t len, size_t alignment, int flags, void** addr_out) {
    (void)addr_in;
    (void)flags;
    if (!addr_out || len == 0 || !PageAligned(len) || (alignment != 0 && !PageAligned(alignment))) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return DoReserveVirtual(addr_out, len, alignment);
}

}

