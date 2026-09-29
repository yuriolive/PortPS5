/**
 * @file MemoryPoolExport.cpp
 * @brief Exports and implementation of PS5 kernel memory pool lifecycle and batch APIs.
 * 
 * Provides sceKernelMemoryPoolBatch, Commit, Decommit, Expand, Reserve, and GetBlockStats
 * implementations adhering to System V ABI without throwing host exceptions.
 */

#include <cstdint>
#include <cstddef>
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
};

PoolState& Pool() {
    static PoolState state;
    return state;
}

bool PageAligned(uint64_t v) { return (v & (PS5_PAGE_SIZE - 1)) == 0; }

int PoolCommit(void* addr, uint64_t len, int prot) {
    if (!addr || len == 0 || !PageAligned(reinterpret_cast<uintptr_t>(addr)) || !PageAligned(len)) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const int ret = DoMprotect(addr, static_cast<size_t>(len), prot);
    if (ret == 0) {
        PoolState& pool = Pool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.committedBytes += static_cast<size_t>(len);
    }
    return ret;
}

int PoolDecommit(void* addr, uint64_t len) {
    if (!addr || len == 0 || !PageAligned(reinterpret_cast<uintptr_t>(addr)) || !PageAligned(len)) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    const int ret = DoMprotect(addr, static_cast<size_t>(len), 0);
    if (ret == 0) {
        PoolState& pool = Pool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.committedBytes -= (static_cast<size_t>(len) < pool.committedBytes) ? static_cast<size_t>(len) : pool.committedBytes;
    }
    return ret;
}

}  // namespace

extern "C" {

/**
 * @brief Performs batched commit, decommit, and protection operations on memory pool mappings.
 * @param entries Array of batch command entries.
 * @param num_entries Count of entries in the array.
 * @param num_entries_out Output counter of successfully executed operations.
 * @param flags Batch flags.
 * @return 0 on success, or SCE error code on failure.
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
 * @brief Commits virtual memory within a memory pool reservation.
 * @param addr Virtual address to commit.
 * @param len Size in bytes to commit.
 * @param type Pool type.
 * @param prot Protection flags.
 * @param flags Commit flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolCommit(void* addr, size_t len, int type, int prot, int flags) {
    (void)type;
    (void)flags;
    return PoolCommit(addr, len, prot);
}

/**
 * @brief Decommits virtual memory within a memory pool reservation.
 * @param addr Virtual address to decommit.
 * @param len Size in bytes to decommit.
 * @param flags Decommit flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolDecommit(void* addr, size_t len, int flags) {
    (void)flags;
    return PoolDecommit(addr, len);
}

/**
 * @brief Expands the physical memory pool capacity by allocating direct physical blocks.
 * @param search_start Search window start.
 * @param search_end Search window end.
 * @param len Size in bytes to allocate.
 * @param alignment Physical alignment.
 * @param phys_addr_out Output receiving allocated physical address.
 * @return 0 on success, or SCE error code on failure.
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
 * @brief Queries current allocated and available block statistics for the memory pool.
 * @param output Output buffer receiving block statistics.
 * @param output_size Size of the output buffer.
 * @return 0 on success, or SCE error code on failure.
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
 * @brief Reserves a virtual address range for memory pool usage without committed backing.
 * @param addr_in Optional base address hint.
 * @param len Size in bytes to reserve.
 * @param alignment Required alignment.
 * @param flags Reservation flags.
 * @param addr_out Output pointer receiving reserved base address.
 * @return 0 on success, or SCE error code on failure.
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

