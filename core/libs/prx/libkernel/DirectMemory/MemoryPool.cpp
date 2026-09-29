/**
 * @file MemoryPool.cpp
 * @brief Implementation of direct memory physical range allocation and query bitmap tracking.
 * 
 * Provides thread-safe tracking of allocated ranges and free extents for guest physical direct memory.
 */

#define _GLIBCXX_HAS_GTHREADS 0
#include "MemoryPool.hpp"
#include "DirectMemory.hpp"
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
        std::lock_guard<std::mutex> lock(_mutex);
        size_t align = (alignment == 0) ? PS5_PAGE_SIZE : alignment;
        uint64_t start = static_cast<uint64_t>(searchStart);
        uint64_t end = static_cast<uint64_t>(searchEnd);
        uint64_t cur = (start + align - 1) & ~(align - 1);
        while (cur + len <= end && cur + len <= DIRECT_MEMORY_SIZE) {
            if (_isFree(cur, len)) {
                _mark(cur, len, true);
                _ranges[cur] = {cur, cur + len, memoryType};
                *physOut = static_cast<int64_t>(cur);
                return 0;
            }
            cur += align;
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

