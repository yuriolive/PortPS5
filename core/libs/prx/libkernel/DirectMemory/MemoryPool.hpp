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

/**
 * @brief Allocates physical direct memory within a search window with an explicit memory type.
 * @param searchStart Search window lower bound.
 * @param searchEnd Search window upper bound.
 * @param len Size in bytes to allocate.
 * @param alignment Physical alignment requirement.
 * @param memoryType PS5 memory type / attribute flags.
 * @param physOut Output pointer receiving the allocated physical address.
 * @return 0 on success, or SCE error code on failure.
 */
int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int memoryType, int64_t* physOut);

/**
 * @brief Allocates physical direct memory within a search window using default memory type (-1).
 * @param searchStart Search window lower bound.
 * @param searchEnd Search window upper bound.
 * @param len Size in bytes to allocate.
 * @param alignment Physical alignment requirement.
 * @param physOut Output pointer receiving the allocated physical address.
 * @return 0 on success, or SCE error code on failure.
 */
int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int64_t* physOut);

/**
 * @brief Releases a physical direct memory range back to the physical pool.
 * @param start Physical address to free.
 * @param len Size in bytes to free.
 */
void DirectMemoryFree(int64_t start, size_t len);

/**
 * @brief Queries metadata of an allocated direct memory block containing the offset.
 * @param offset Physical offset to query.
 * @param block Output receiving the block range and memory type.
 * @return true if the offset is inside an allocated block, false otherwise.
 */
bool DirectMemoryQueryBlock(uint64_t offset, DirectMemoryBlock* block);

/**
 * @brief Computes contiguous free byte count starting from an offset up to a limit.
 * @param offset Starting physical address.
 * @param limit Upper boundary physical address.
 * @return Number of contiguous free bytes.
 */
size_t DirectMemoryFreeRun(uint64_t offset, uint64_t limit);

/**
 * @brief Finds the allocated run containing an offset, optionally the next one, merging same-type neighbours.
 * @param offset Physical offset to query.
 * @param findNext When true and offset is free, answer with the next allocated block.
 * @param block Output receiving the first block's start, the merged run end and the memory type.
 * @return true if a run was found, false otherwise.
 */
bool DirectMemoryQueryRun(uint64_t offset, bool findNext, DirectMemoryBlock* block);

/**
 * @brief Finds the largest contiguous free run inside [searchStart, searchEnd).
 * @param searchStart Window start in bytes.
 * @param searchEnd Window end in bytes (clamped to the aperture).
 * @param alignment Power-of-two alignment, at least one page, the run start must satisfy.
 * @param startOut Receives the aligned start of the run, 0 if none.
 * @param sizeOut Receives the run size in bytes, 0 if none.
 */
void DirectMemoryLargestFreeRun(uint64_t searchStart, uint64_t searchEnd, size_t alignment, int64_t* startOut, size_t* sizeOut);

#endif

