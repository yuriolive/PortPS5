// tests/memory/DirectMemoryPoolTests.cpp
// Verification test suite for DirectMemory physical pool, queries, free runs,
// and SceKernelMemoryPool APIs (Batch, Commit, Decommit, Expand, Reserve, Stats).

#include "common/TestHarness.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"
#include "prx/libkernel/DirectMemory/MemoryPool.hpp"
#include "SceTypes.hpp"

#include <cstdint>
#include <vector>

namespace {

using namespace PortPS5::Testing;

TEST(DirectMemoryPool, AllocTracksBlockAndQueries) {
    constexpr size_t size = 64 * 1024; // 64 KB = 4 pages
    constexpr size_t alignment = 16 * 1024;
    int64_t physAddr = -1;

    // Allocate physical block with explicit memory type 2
    int ret = DirectMemoryAlloc(0, 1024 * 1024 * 1024, size, alignment, 2, &physAddr);
    EXPECT_EQ(ret, 0);
    ASSERT_GE(physAddr, 0);

    // Query exact offset
    DirectMemoryBlock block{};
    bool found = DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr), &block);
    EXPECT_TRUE(found);
    EXPECT_EQ(block.start, static_cast<uint64_t>(physAddr));
    EXPECT_EQ(block.end, static_cast<uint64_t>(physAddr + size));
    EXPECT_EQ(block.memoryType, 2);

    // Query offset in the middle of block
    found = DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr + 32 * 1024), &block);
    EXPECT_TRUE(found);
    EXPECT_EQ(block.start, static_cast<uint64_t>(physAddr));
    EXPECT_EQ(block.end, static_cast<uint64_t>(physAddr + size));

    // Release direct memory
    DirectMemoryFree(physAddr, size);

    // After free, query should fail or not cover the offset
    found = DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr), &block);
    EXPECT_FALSE(found);
}

TEST(DirectMemoryPool, PartialFreeSplitsBlock) {
    constexpr size_t size = 64 * 1024; // 4 pages
    int64_t physAddr = -1;

    int ret = DirectMemoryAlloc(0, 1024 * 1024 * 1024, size, PS5_PAGE_SIZE, 5, &physAddr);
    EXPECT_EQ(ret, 0);
    ASSERT_GE(physAddr, 0);

    // Free the second page (offset + 16 KB, length 16 KB)
    DirectMemoryFree(physAddr + PS5_PAGE_SIZE, PS5_PAGE_SIZE);

    DirectMemoryBlock block{};
    // First page should still exist
    EXPECT_TRUE(DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr), &block));
    EXPECT_EQ(block.start, static_cast<uint64_t>(physAddr));
    EXPECT_EQ(block.end, static_cast<uint64_t>(physAddr + PS5_PAGE_SIZE));
    EXPECT_EQ(block.memoryType, 5);

    // Freed middle page should not be queried
    EXPECT_FALSE(DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr + PS5_PAGE_SIZE), &block));

    // Remaining pages (2 pages at offset + 32 KB) should exist
    EXPECT_TRUE(DirectMemoryQueryBlock(static_cast<uint64_t>(physAddr + 2 * PS5_PAGE_SIZE), &block));
    EXPECT_EQ(block.start, static_cast<uint64_t>(physAddr + 2 * PS5_PAGE_SIZE));
    EXPECT_EQ(block.end, static_cast<uint64_t>(physAddr + size));

    // Clean up remaining parts
    DirectMemoryFree(physAddr, PS5_PAGE_SIZE);
    DirectMemoryFree(physAddr + 2 * PS5_PAGE_SIZE, 2 * PS5_PAGE_SIZE);
}

TEST(DirectMemoryPool, MemoryPoolBatchAndStats) {
    void* addr = nullptr;
    constexpr size_t reserveSize = 2 * 1024 * 1024; // 2 MB = 1 pool block

    int ret = sceKernelMemoryPoolReserve(nullptr, reserveSize, reserveSize, 0, &addr);
    EXPECT_EQ(ret, 0);
    ASSERT_NE(addr, nullptr);

    KernelMemoryPoolBatchEntry entries[2]{};
    // Op 1: Commit 64 KB with read-write protection (prot 3)
    entries[0].op = 1;
    entries[0].commit.addr = addr;
    entries[0].commit.len = 64 * 1024;
    entries[0].commit.prot = 3;

    // Op 3: Protect 32 KB as read-only (prot 1)
    entries[1].op = 3;
    entries[1].protect.addr = addr;
    entries[1].protect.len = 32 * 1024;
    entries[1].protect.prot = 1;

    int done = 0;
    ret = sceKernelMemoryPoolBatch(entries, 2, &done, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(done, 2);

    KernelMemoryPoolBlockStats stats{};
    ret = sceKernelMemoryPoolGetBlockStats(&stats, sizeof(stats));
    EXPECT_EQ(ret, 0);

    // Decommit and release
    EXPECT_EQ(sceKernelMemoryPoolDecommit(addr, 64 * 1024, 0), 0);
    EXPECT_EQ(sceKernelMunmap(reinterpret_cast<uint64_t>(addr), reserveSize), 0);
}

} // namespace
