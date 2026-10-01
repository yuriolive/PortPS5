/**
 * @file MemoryPool.cpp
 * @brief Implementation of direct memory physical range allocation and query bitmap tracking.
 * 
 * Provides thread-safe tracking of allocated ranges and free extents for guest physical direct memory.
 */

#define _GLIBCXX_HAS_GTHREADS 0
#include "MemoryPool.hpp"
#include "DirectMemory.hpp"
#include <algorithm>
#include <map>
#include <mutex>

static constexpr size_t NUM_PAGES = DIRECT_MEMORY_SIZE / PS5_PAGE_SIZE;

// Physical memory pool tracking physical direct memory allocations and extents.
struct PhysicalMemoryPool {
    /**
     * @brief Returns the singleton instance of the physical direct memory pool.
     * @return Reference to the physical memory pool instance.
     */
    static PhysicalMemoryPool& Instance() {
        static PhysicalMemoryPool inst;
        return inst;
    }

    /**
     * @brief Allocates contiguous physical direct memory pages within a search range.
     * @param searchStart Lower search bound.
     * @param searchEnd Upper search bound.
     * @param len Allocation length in bytes.
     * @param alignment Required physical alignment.
     * @param memoryType PS5 memory type / attribute flags.
     * @param physOut Output pointer receiving allocated physical address.
     * @return 0 on success, or SCE error code on failure.
     */
    int Alloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int memoryType, int64_t* physOut) {
        if (!physOut) return SCE_KERNEL_ERROR_EINVAL;
        size_t align = (alignment == 0) ? PS5_PAGE_SIZE : alignment;
        // A non-power-of-two alignment would make the mask arithmetic below produce misaligned offsets.
        if (align < PS5_PAGE_SIZE || (align & (align - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;
        if (searchStart < 0 || searchEnd <= searchStart || len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;
        std::lock_guard<std::mutex> lock(_mutex);
        const uint64_t limit = std::min<uint64_t>(static_cast<uint64_t>(searchEnd), DIRECT_MEMORY_SIZE);
        const uint64_t start = static_cast<uint64_t>(searchStart);
        // Every comparison below is written as `len <= limit - cur` so that no sum can wrap: the old
        // `cur + len <= end` let a length near 2^64 wrap, after which the bitmap scan ran out of bounds.
        if (len > limit || start > limit - len) return SCE_KERNEL_ERROR_EAGAIN;
        // Round the search start up to the alignment (start <= limit - len <= DIRECT_MEMORY_SIZE, so this cannot wrap
        // unless align itself is enormous, which the overflow check below catches).
        uint64_t cur = start;
        if (const uint64_t rem = cur % align; rem != 0 && __builtin_add_overflow(cur, align - rem, &cur)) return SCE_KERNEL_ERROR_EAGAIN;
        while (cur <= limit && len <= limit - cur) {
            if (_isFree(cur, len)) {
                _mark(cur, len, true);
                _ranges[cur] = {cur, cur + len, memoryType};
                *physOut = static_cast<int64_t>(cur);
                return 0;
            }
            if (__builtin_add_overflow(cur, static_cast<uint64_t>(align), &cur)) break;
        }
        return SCE_KERNEL_ERROR_EAGAIN;
    }

    /**
     * @brief Releases physical direct memory pages and updates range tracking.
     * @param start Physical address to free.
     * @param len Size in bytes to free.
     */
    void Free(uint64_t start, size_t len) {
        std::lock_guard<std::mutex> lock(_mutex);
        // Defence in depth: the export validates, but the page bitmap is fixed-size, so an
        // out-of-range or misaligned request from any caller is ignored rather than written.
        if ((start & (PS5_PAGE_SIZE - 1)) != 0 || (len & (PS5_PAGE_SIZE - 1)) != 0 || start > DIRECT_MEMORY_SIZE || len > DIRECT_MEMORY_SIZE - start) return;
        _mark(start, len, false);

        // Walk every _ranges entry that overlaps [start, end), trimming or erasing each one.
        const uint64_t end = start + len;
        auto it = _ranges.upper_bound(start);
        if (it != _ranges.begin() && std::prev(it)->second.end > start) {
            --it;
        }
        while (it != _ranges.end() && it->second.start < end) {
            DirectMemoryBlock b = it->second;
            it = _ranges.erase(it);
            if (b.start < start) {
                _ranges[b.start] = {b.start, start, b.memoryType};
            }
            if (b.end > end) {
                _ranges[end] = {end, b.end, b.memoryType};
            }
        }
    }

    /**
     * @brief Queries metadata for an allocated physical direct memory block containing offset.
     * @param offset Physical offset to query.
     * @param block Output pointer receiving the block descriptor.
     * @return true if the offset is inside an allocated block, false otherwise.
     */
    bool Query(uint64_t offset, DirectMemoryBlock* block) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _ranges.upper_bound(offset);
        if (it == _ranges.begin()) return false;
        --it;
        if (offset >= it->second.end) return false;
        if (block) *block = it->second;
        return true;
    }

    /**
     * @brief Computes contiguous free bytes starting from offset up to limit.
     * @param offset Starting physical address.
     * @param limit Upper boundary physical address.
     * @return Contiguous free byte count.
     */
    size_t FreeRun(uint64_t offset, uint64_t limit) {
        std::lock_guard<std::mutex> lock(_mutex);
        uint64_t cur = offset & ~static_cast<uint64_t>(PS5_PAGE_SIZE - 1);
        size_t run = 0;
        while (cur + PS5_PAGE_SIZE <= limit && cur + PS5_PAGE_SIZE <= DIRECT_MEMORY_SIZE
               && _isFree(cur, PS5_PAGE_SIZE)) {
            cur += PS5_PAGE_SIZE;
            run += PS5_PAGE_SIZE;
        }
        return run;
    }

    /**
     * @brief Finds the allocated run containing offset (or following it) and merges adjacent same-type blocks.
     * @param offset Physical offset to query.
     * @param findNext When true and offset is free, answer with the next allocated block.
     * @param block Output receiving start of the first block, merged end and memory type.
     * @return true if a run was found, false if offset (and, with findNext, everything after it) is free.
     */
    bool QueryRun(uint64_t offset, bool findNext, DirectMemoryBlock* block) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _ranges.upper_bound(offset);
        if (it != _ranges.begin() && std::prev(it)->second.end > offset) {
            --it;
        } else if (!findNext || it == _ranges.end()) {
            return false;
        }
        DirectMemoryBlock run = it->second;
        // The console tracks runs of one memory type, not individual allocation calls.
        for (++it; it != _ranges.end() && it->second.start == run.end && it->second.memoryType == run.memoryType; ++it) {
            run.end = it->second.end;
        }
        if (block) *block = run;
        return true;
    }

    /**
     * @brief Finds the largest contiguous free run inside a search window.
     * @param searchStart Window start (rounded up to a page).
     * @param searchEnd Window end (clamped to the aperture, rounded down to a page).
     * @param alignment Power-of-two alignment (at least a page) the run start must satisfy.
     * @param startOut Receives the aligned start of the run, or 0 if there is none.
     * @param sizeOut Receives the size of the run in bytes, or 0 if there is none.
     */
    void LargestFreeRun(uint64_t searchStart, uint64_t searchEnd, size_t alignment, int64_t* startOut, size_t* sizeOut) {
        std::lock_guard<std::mutex> lock(_mutex);
        *startOut = 0;
        *sizeOut = 0;
        const uint64_t limit = std::min<uint64_t>(searchEnd, DIRECT_MEMORY_SIZE) & ~static_cast<uint64_t>(PS5_PAGE_SIZE - 1);
        if (searchStart > limit) return;
        uint64_t page = (searchStart + PS5_PAGE_SIZE - 1) / PS5_PAGE_SIZE;  // searchStart <= limit <= DIRECT_MEMORY_SIZE: no wrap
        const uint64_t lastPage = limit / PS5_PAGE_SIZE;
        while (page < lastPage) {
            if (_used[page]) {
                ++page;
                continue;
            }
            const uint64_t runFirst = page;
            while (page < lastPage && !_used[page]) ++page;
            const uint64_t runStart = runFirst * PS5_PAGE_SIZE;
            const uint64_t runEnd = page * PS5_PAGE_SIZE;
            const uint64_t aligned = (runStart + alignment - 1) & ~static_cast<uint64_t>(alignment - 1);
            if (aligned < runEnd && runEnd - aligned > *sizeOut) {
                *startOut = static_cast<int64_t>(aligned);
                *sizeOut = static_cast<size_t>(runEnd - aligned);
            }
        }
    }

private:
    /**
     * @brief Checks if a contiguous physical range is completely free.
     * @param offset Starting physical address.
     * @param len Length in bytes.
     * @return true if free, false if any page is allocated.
     */
    bool _isFree(uint64_t offset, size_t len) const {
        size_t first = offset / PS5_PAGE_SIZE;
        size_t count = len / PS5_PAGE_SIZE;
        for (size_t i = 0; i < count; ++i)
            if (_used[first + i]) return false;
        return true;
    }

    /**
     * @brief Marks a physical page range as used or free in the allocation bitmap.
     * @param offset Starting physical address.
     * @param len Length in bytes.
     * @param used True to mark allocated, false to mark free.
     */
    void _mark(uint64_t offset, size_t len, bool used) {
        size_t first = offset / PS5_PAGE_SIZE;
        size_t count = len / PS5_PAGE_SIZE;
        for (size_t i = 0; i < count; ++i)
            _used[first + i] = used;
    }

    std::mutex _mutex;
    bool _used[NUM_PAGES] = {};
    std::map<uint64_t, DirectMemoryBlock> _ranges;
};

/**
 * @brief Implementation of direct memory allocation with explicit memory type.
 *
 * Allocates contiguous physical direct memory pages within [searchStart, searchEnd).
 */
int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int memoryType, int64_t* physOut) {
    return PhysicalMemoryPool::Instance().Alloc(searchStart, searchEnd, len, alignment, memoryType, physOut);
}

/**
 * @brief Implementation of direct memory allocation with default memory type.
 *
 * Allocates contiguous physical direct memory pages using the default unconstrained memory type.
 */
int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int64_t* physOut) {
    return PhysicalMemoryPool::Instance().Alloc(searchStart, searchEnd, len, alignment, -1, physOut);
}

/**
 * @brief Implementation of direct memory release.
 *
 * Frees physical direct memory pages across [start, start + len) and trims overlapping block ranges.
 */
void DirectMemoryFree(int64_t start, size_t len) {
    PhysicalMemoryPool::Instance().Free(static_cast<uint64_t>(start), len);
}

/**
 * @brief Implementation of direct memory block querying.
 *
 * Looks up allocated range boundaries and memory type attributes for the given physical offset.
 */
bool DirectMemoryQueryBlock(uint64_t offset, DirectMemoryBlock* block) {
    return PhysicalMemoryPool::Instance().Query(offset, block);
}

/**
 * @brief Implementation of contiguous free extent calculation.
 *
 * Computes contiguous unallocated bytes beginning at offset up to the specified limit.
 */
size_t DirectMemoryFreeRun(uint64_t offset, uint64_t limit) {
    return PhysicalMemoryPool::Instance().FreeRun(offset, limit);
}


/**
 * @brief Implementation of allocated-run querying.
 *
 * Delegates to the pool under a single lock so the merge of adjacent same-type blocks is consistent.
 */
bool DirectMemoryQueryRun(uint64_t offset, bool findNext, DirectMemoryBlock* block) {
    return PhysicalMemoryPool::Instance().QueryRun(offset, findNext, block);
}

/**
 * @brief Implementation of the largest-free-run search used by sceKernelAvailableDirectMemorySize.
 */
void DirectMemoryLargestFreeRun(uint64_t searchStart, uint64_t searchEnd, size_t alignment, int64_t* startOut, size_t* sizeOut) {
    PhysicalMemoryPool::Instance().LargestFreeRun(searchStart, searchEnd, alignment, startOut, sizeOut);
}
