// tests/memory/VirtualMemoryAllocationTests.cpp
// Verification test suite for PortPS5 virtual and direct memory management:
// 16 KB page alignment, direct memory allocation/free, flexible memory mapping,
// memory protection transitions (PROT_READ, PROT_WRITE, PROT_EXEC), and unmapping.
// Reference: KytyPS5 VirtualMemoryAllocationTests & FreeBSD 12 vm subsystem.

#include "common/TestHarness.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace PortPS5::Testing;

// Verifies that total direct memory size reported matches the PS5 specification (13,824 MB).
TEST(VirtualMemoryAllocation, GetDirectMemorySize) {
    size_t totalBytes = sceKernelGetDirectMemorySize();
    EXPECT_EQ(totalBytes, 13824ULL * 1024 * 1024);
}

// Verifies direct memory allocation and release with proper 16 KB page alignment.
TEST(VirtualMemoryAllocation, AllocateAndReleaseDirectMemory) {
    constexpr size_t allocSize = 64 * 1024; // 64 KB (4 x 16 KB pages)
    constexpr size_t alignment = 16 * 1024; // 16 KB alignment
    int64_t physAddr = -1;

    // Allocate 64 KB of main direct memory
    int ret = sceKernelAllocateMainDirectMemory(allocSize, alignment, 0, &physAddr);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(physAddr, 0);
    // Address must be aligned to 16 KB
    EXPECT_EQ(physAddr & (PS5_PAGE_SIZE - 1), 0);

    // Release the allocated direct memory range
    ret = sceKernelReleaseDirectMemory(physAddr, allocSize);
    EXPECT_EQ(ret, 0);
}

// Verifies that invalid allocation arguments (non-page-aligned lengths, invalid search bounds) return EINVAL.
TEST(VirtualMemoryAllocation, AllocateDirectMemoryInvalidArguments) {
    int64_t physAddr = -1;

    // Length 0 must return SCE_KERNEL_ERROR_EINVAL
    EXPECT_EQ(sceKernelAllocateDirectMemory(0, 1024 * 1024, 0, PS5_PAGE_SIZE, 0, &physAddr), ::SCE_KERNEL_ERROR_EINVAL);

    // Length not a multiple of 16 KB page size must return SCE_KERNEL_ERROR_EINVAL
    EXPECT_EQ(sceKernelAllocateDirectMemory(0, 1024 * 1024, 4096, PS5_PAGE_SIZE, 0, &physAddr), ::SCE_KERNEL_ERROR_EINVAL);

    // Inverted search range (search_start >= search_end) must return SCE_KERNEL_ERROR_EINVAL
    EXPECT_EQ(sceKernelAllocateDirectMemory(1024 * 1024, 1024 * 1024, PS5_PAGE_SIZE, PS5_PAGE_SIZE, 0, &physAddr), ::SCE_KERNEL_ERROR_EINVAL);

    // Null output pointer must return SCE_KERNEL_ERROR_EINVAL
    EXPECT_EQ(sceKernelAllocateDirectMemory(0, 1024 * 1024, PS5_PAGE_SIZE, PS5_PAGE_SIZE, 0, nullptr), ::SCE_KERNEL_ERROR_EINVAL);
}

// Verifies flexible memory mapping (anonymous memory), read/write access, and unmapping.
TEST(VirtualMemoryAllocation, MapFlexibleMemoryReadWrite) {
    constexpr size_t mapSize = 64 * 1024; // 4 pages
    void* addr = nullptr;

    // Map flexible memory with Read+Write permissions (prot 0x02 | 0x01 = 0x03, or PROT_CPU_RW)
    int ret = sceKernelMapFlexibleMemory(&addr, mapSize, 3, 0);
    EXPECT_EQ(ret, 0);
    ASSERT_NE(addr, nullptr);

    // Verify 16 KB page alignment of allocated virtual address
    EXPECT_EQ(reinterpret_cast<uintptr_t>(addr) & (PS5_PAGE_SIZE - 1), 0);

    // Write a known byte pattern across all 4 pages
    auto* bytes = static_cast<uint8_t*>(addr);
    for (size_t i = 0; i < mapSize; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 7) & 0xFF);
    }

    // Verify written data integrity
    for (size_t i = 0; i < mapSize; ++i) {
        EXPECT_EQ(bytes[i], static_cast<uint8_t>((i * 7) & 0xFF));
    }

    // Unmap the virtual memory range
    EXPECT_EQ(sceKernelMunmap(reinterpret_cast<uint64_t>(addr), mapSize), 0);
}

// Verifies mapping direct memory into virtual address space, data writing, and unmapping.
TEST(VirtualMemoryAllocation, MapDirectMemoryReadWrite) {
    constexpr size_t allocSize = 32 * 1024; // 2 x 16 KB pages
    constexpr size_t alignment = 16 * 1024;
    int64_t physAddr = -1;

    // Allocate direct physical memory
    ASSERT_EQ(sceKernelAllocateMainDirectMemory(allocSize, alignment, 0, &physAddr), 0);
    ASSERT_GE(physAddr, 0);

    // Map the physical memory to an arbitrary virtual address
    void* virtAddr = nullptr;
    int ret = sceKernelMapDirectMemory(&virtAddr, allocSize, 3, 0, physAddr, alignment);
    EXPECT_EQ(ret, 0);
    ASSERT_NE(virtAddr, nullptr);

    // Write and verify payload
    auto* data = static_cast<uint32_t*>(virtAddr);
    data[0] = 0x12345678;
    data[1] = 0x9ABCDEF0;
    EXPECT_EQ(data[0], 0x12345678);
    EXPECT_EQ(data[1], 0x9ABCDEF0);

    // Unmap virtual view and release physical backing
    EXPECT_EQ(sceKernelMunmap(reinterpret_cast<uint64_t>(virtAddr), allocSize), 0);
    EXPECT_EQ(sceKernelReleaseDirectMemory(physAddr, allocSize), 0);
}

// Verifies memory protection transitions using sceKernelMprotect (RW to ReadOnly, etc.).
TEST(VirtualMemoryAllocation, MemoryProtectionTransition) {
    constexpr size_t mapSize = 16 * 1024; // 1 page
    void* addr = nullptr;

    // Map memory as Read-Write (prot = 3)
    ASSERT_EQ(sceKernelMapFlexibleMemory(&addr, mapSize, 3, 0), 0);
    ASSERT_NE(addr, nullptr);

    auto* ptr = static_cast<volatile uint32_t*>(addr);
    *ptr = 0xDEADBEEF;
    EXPECT_EQ(*ptr, 0xDEADBEEF);

    // Transition protection to Read-Only (prot = 1)
    EXPECT_EQ(sceKernelMprotect(addr, mapSize, 1), 0);
    EXPECT_EQ(*ptr, 0xDEADBEEF);

    // Transition back to Read-Write (prot = 3)
    EXPECT_EQ(sceKernelMprotect(addr, mapSize, 3), 0);
    *ptr = 0xCAFEBABE;
    EXPECT_EQ(*ptr, 0xCAFEBABE);

    EXPECT_EQ(sceKernelMunmap(reinterpret_cast<uint64_t>(addr), mapSize), 0);
}

// Verifies querying available direct memory size within an address range.
TEST(VirtualMemoryAllocation, AvailableDirectMemoryQuery) {
    int64_t physOut = -1;
    size_t sizeOut = 0;

    int ret = sceKernelAvailableDirectMemorySize(0, 1024 * 1024 * 1024, PS5_PAGE_SIZE, &physOut, &sizeOut);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(physOut, 0);
    EXPECT_GT(sizeOut, 0);
}

// Verifies virtual address reservation with sceKernelReserveVirtualRange.
TEST(VirtualMemoryAllocation, ReserveVirtualRange) {
    constexpr size_t reserveSize = 256 * 1024; // 256 KB
    constexpr size_t alignment = 64 * 1024;   // 64 KB
    void* addr = nullptr;

    int ret = sceKernelReserveVirtualRange(&addr, reserveSize, 0, alignment);
    EXPECT_EQ(ret, 0);
    ASSERT_NE(addr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(addr) & (alignment - 1), 0);

    // Clean up reservation
    EXPECT_EQ(sceKernelMunmap(reinterpret_cast<uint64_t>(addr), reserveSize), 0);
}

} // namespace
