/**
 * @file MemoryPool.hpp
 * @brief Internal declarations for tracking physical direct memory allocation extents.
 * 
 * Manages allocation tracking, free runs, and block metadata across the 16GB direct space.
 */

#ifndef CORE_LIBS_PRX_LIBKERNEL_DIRECTMEMORY_MEMORYPOOL_HPP
#define CORE_LIBS_PRX_LIBKERNEL_DIRECTMEMORY_MEMORYPOOL_HPP

#include <cstdint>
#include <cstddef>

struct DirectMemoryBlock {
    uint64_t start = 0;
    uint64_t end = 0;
    int memoryType = 0;
};

int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int memoryType = -1, int64_t* physOut = nullptr);
int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int64_t* physOut);
void DirectMemoryFree(int64_t start, size_t len);
bool DirectMemoryQueryBlock(uint64_t offset, DirectMemoryBlock* block);
size_t DirectMemoryFreeRun(uint64_t offset, uint64_t limit);

#endif

