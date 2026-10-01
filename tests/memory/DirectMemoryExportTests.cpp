// tests/memory/DirectMemoryExportTests.cpp
// Verifies the guest-visible direct-memory exports against FreeBSD/PS5 semantics:
// sceKernelDirectMemoryQuery (real extent and memory type, find-next, EACCES for
// free memory), sceKernelAvailableDirectMemorySize (largest free run in the
// window) and the hardening of allocate/release against overflowing or
// out-of-range arguments. Expected values were cross-checked against the
// shadPS4 behaviour (GPL-2.0-or-later, behaviour only; no code copied).
//
// Each test releases what it allocates. ctest runs every test in its own
// process, so the global physical pool starts empty for each of them.

#include "common/TestHarness.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"

#include <cstdint>
#include <limits>

namespace {

// Guest layout of the query result (explicit widths; the export copies exactly this).
struct QueryInfo {
    int64_t start;
    int64_t end;
    int32_t memoryType;
};

constexpr int kFindNext = 1;  // SCE_KERNEL_DMQ_FIND_NEXT
constexpr int kEacces = static_cast<int>(0x8002000Du);

int64_t Allocate(size_t pages, int type, int64_t searchStart = 0, int64_t searchEnd = static_cast<int64_t>(DIRECT_MEMORY_SIZE)) {
    int64_t phys = -1;
    EXPECT_EQ(sceKernelAllocateDirectMemory(searchStart, searchEnd, pages * PS5_PAGE_SIZE, PS5_PAGE_SIZE, type, &phys), 0);
    return phys;
}

// Invariant: a query inside an allocation reports the allocation's real extent
// and the memory type it was allocated with (not a one-page, fixed-type guess).
TEST(DirectMemoryExport, QueryReportsRealExtentAndType) {
    const int64_t phys = Allocate(4, 3);
    ASSERT_GE(phys, 0);
    QueryInfo info{};
    ASSERT_EQ(sceKernelDirectMemoryQuery(phys + 2 * PS5_PAGE_SIZE, 0, &info, sizeof(info)), 0);
    EXPECT_EQ(info.start, phys);
    EXPECT_EQ(info.end, phys + static_cast<int64_t>(4 * PS5_PAGE_SIZE));
    EXPECT_EQ(info.memoryType, 3);
    sceKernelReleaseDirectMemory(phys, 4 * PS5_PAGE_SIZE);
}

// Invariant: a different memory type is reported back unchanged.
TEST(DirectMemoryExport, QueryReportsTheAllocatedType) {
    const int64_t phys = Allocate(1, 1);
    ASSERT_GE(phys, 0);
    QueryInfo info{};
    ASSERT_EQ(sceKernelDirectMemoryQuery(phys, 0, &info, sizeof(info)), 0);
    EXPECT_EQ(info.memoryType, 1);
    sceKernelReleaseDirectMemory(phys, PS5_PAGE_SIZE);
}

// Invariant: unallocated direct memory, and offsets past the aperture, answer
// EACCES (0x8002000D); the output struct is not claimed to describe anything.
TEST(DirectMemoryExport, QueryOfFreeOrOutOfRangeMemoryIsEacces) {
    const int64_t phys = Allocate(1, 3);
    ASSERT_GE(phys, 0);
    sceKernelReleaseDirectMemory(phys, PS5_PAGE_SIZE);
    QueryInfo info{};
    EXPECT_EQ(sceKernelDirectMemoryQuery(phys, 0, &info, sizeof(info)), kEacces);
    EXPECT_EQ(sceKernelDirectMemoryQuery(static_cast<int64_t>(DIRECT_MEMORY_SIZE), 0, &info, sizeof(info)), kEacces);
    EXPECT_EQ(sceKernelDirectMemoryQuery(std::numeric_limits<int64_t>::max(), 0, &info, sizeof(info)), kEacces);
}

// Invariant: bad arguments stay EINVAL (null info, negative offset, short info).
TEST(DirectMemoryExport, QueryRejectsBadArguments) {
    QueryInfo info{};
    EXPECT_EQ(sceKernelDirectMemoryQuery(0, 0, nullptr, sizeof(info)), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelDirectMemoryQuery(-1, 0, &info, sizeof(info)), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelDirectMemoryQuery(0, 0, &info, sizeof(info) - 1), ::SCE_KERNEL_ERROR_EINVAL);
}

// Invariant: FIND_NEXT skips free pages to the next allocated block; with no
// block at or after the offset it answers EACCES like a plain miss.
TEST(DirectMemoryExport, QueryFindNextSkipsFreeMemory) {
    const int64_t first = Allocate(4, 3);
    ASSERT_GE(first, 0);
    sceKernelReleaseDirectMemory(first, 2 * PS5_PAGE_SIZE);  // leave pages 2..3 allocated
    QueryInfo info{};
    EXPECT_EQ(sceKernelDirectMemoryQuery(first, 0, &info, sizeof(info)), kEacces);
    ASSERT_EQ(sceKernelDirectMemoryQuery(first, kFindNext, &info, sizeof(info)), 0);
    EXPECT_EQ(info.start, first + static_cast<int64_t>(2 * PS5_PAGE_SIZE));
    EXPECT_EQ(info.end, first + static_cast<int64_t>(4 * PS5_PAGE_SIZE));
    sceKernelReleaseDirectMemory(first + 2 * PS5_PAGE_SIZE, 2 * PS5_PAGE_SIZE);
    EXPECT_EQ(sceKernelDirectMemoryQuery(first, kFindNext, &info, sizeof(info)), kEacces);
}

// Invariant: adjacent blocks of the same type report one merged extent (the
// console tracks runs, not individual allocation calls); a different type
// ends the run.
TEST(DirectMemoryExport, QueryMergesAdjacentBlocksOfTheSameType) {
    const int64_t a = Allocate(2, 3);
    ASSERT_GE(a, 0);
    const int64_t b = Allocate(2, 3, a + 2 * static_cast<int64_t>(PS5_PAGE_SIZE), a + 4 * static_cast<int64_t>(PS5_PAGE_SIZE));
    const int64_t c = Allocate(2, 1, a + 4 * static_cast<int64_t>(PS5_PAGE_SIZE), a + 6 * static_cast<int64_t>(PS5_PAGE_SIZE));
    ASSERT_EQ(b, a + static_cast<int64_t>(2 * PS5_PAGE_SIZE));
    ASSERT_EQ(c, a + static_cast<int64_t>(4 * PS5_PAGE_SIZE));
    QueryInfo info{};
    ASSERT_EQ(sceKernelDirectMemoryQuery(a, 0, &info, sizeof(info)), 0);
    EXPECT_EQ(info.start, a);
    EXPECT_EQ(info.end, a + static_cast<int64_t>(4 * PS5_PAGE_SIZE));  // a+b merged, c (type 1) excluded
    // The run starts at the block containing the offset and extends forward only (shadPS4 does the
    // same: start = the found block's base, end = last adjacent same-type block). Pinned so a later
    // "merge backwards" change is a deliberate decision, not an accident.
    ASSERT_EQ(sceKernelDirectMemoryQuery(b, 0, &info, sizeof(info)), 0);
    EXPECT_EQ(info.start, b);
    EXPECT_EQ(info.end, a + static_cast<int64_t>(4 * PS5_PAGE_SIZE));
    sceKernelReleaseDirectMemory(a, 6 * PS5_PAGE_SIZE);
}

// Invariant: the available size is the largest free run inside the search
// window, not the distance from the first free page to the window end. Window
// of 10 pages with page 3 taken: runs are 3 pages and 6 pages, so 6 at page 4.
TEST(DirectMemoryExport, AvailableSizeIsTheLargestFreeRun) {
    const int64_t base = Allocate(10, 3);
    ASSERT_GE(base, 0);
    sceKernelReleaseDirectMemory(base, 10 * PS5_PAGE_SIZE);
    const int64_t taken = Allocate(1, 3, base + 3 * static_cast<int64_t>(PS5_PAGE_SIZE), base + 4 * static_cast<int64_t>(PS5_PAGE_SIZE));
    ASSERT_EQ(taken, base + static_cast<int64_t>(3 * PS5_PAGE_SIZE));
    int64_t start = -1;
    size_t size = 0;
    ASSERT_EQ(sceKernelAvailableDirectMemorySize(base, base + static_cast<int64_t>(10 * PS5_PAGE_SIZE), 0, &start, &size), 0);
    EXPECT_EQ(size, 6 * PS5_PAGE_SIZE);
    EXPECT_EQ(start, base + static_cast<int64_t>(4 * PS5_PAGE_SIZE));
    sceKernelReleaseDirectMemory(taken, PS5_PAGE_SIZE);
}

// Invariant: a fully allocated window reports size 0 successfully (nothing
// available) instead of an error and without touching the output pair.
TEST(DirectMemoryExport, AvailableSizeOfFullWindowIsZero) {
    const int64_t block = Allocate(2, 3);
    ASSERT_GE(block, 0);
    int64_t start = -1;
    size_t size = 99;
    ASSERT_EQ(sceKernelAvailableDirectMemorySize(block, block + static_cast<int64_t>(2 * PS5_PAGE_SIZE), 0, &start, &size), 0);
    EXPECT_EQ(size, 0u);
    sceKernelReleaseDirectMemory(block, 2 * PS5_PAGE_SIZE);
}

// Invariant: allocation arithmetic cannot wrap. A length close to 2^64 used to
// make cur + len wrap below the window end and drive the bitmap scan out of
// bounds; it must fail cleanly. Non-power-of-two alignment is EINVAL.
TEST(DirectMemoryExport, AllocateRejectsOverflowingAndMisalignedArguments) {
    int64_t phys = -1;
    const size_t huge = std::numeric_limits<size_t>::max() & ~(PS5_PAGE_SIZE - 1);
    EXPECT_NE(sceKernelAllocateDirectMemory(0, std::numeric_limits<int64_t>::max(), huge, PS5_PAGE_SIZE, 3, &phys), 0);
    EXPECT_NE(sceKernelAllocateDirectMemory(std::numeric_limits<int64_t>::max() - 1, std::numeric_limits<int64_t>::max(), PS5_PAGE_SIZE, PS5_PAGE_SIZE, 3, &phys), 0);
    EXPECT_EQ(sceKernelAllocateDirectMemory(0, static_cast<int64_t>(DIRECT_MEMORY_SIZE), PS5_PAGE_SIZE, 3 * PS5_PAGE_SIZE, 3, &phys), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelAllocateDirectMemory(0, static_cast<int64_t>(DIRECT_MEMORY_SIZE), DIRECT_MEMORY_SIZE + PS5_PAGE_SIZE, PS5_PAGE_SIZE, 3, &phys) != 0, true);
}

// Invariant: release validates its range. A start/length past the aperture
// used to write outside the page bitmap; unaligned or wrapping ranges are EINVAL.
TEST(DirectMemoryExport, ReleaseRejectsOutOfRangeAndMisalignedRanges) {
    EXPECT_EQ(sceKernelReleaseDirectMemory(0, std::numeric_limits<size_t>::max() & ~(PS5_PAGE_SIZE - 1)), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelReleaseDirectMemory(static_cast<int64_t>(DIRECT_MEMORY_SIZE), PS5_PAGE_SIZE), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelReleaseDirectMemory(static_cast<int64_t>(DIRECT_MEMORY_SIZE - PS5_PAGE_SIZE), 2 * PS5_PAGE_SIZE), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelReleaseDirectMemory(1, PS5_PAGE_SIZE), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelReleaseDirectMemory(0, PS5_PAGE_SIZE + 1), ::SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelReleaseDirectMemory(-16, PS5_PAGE_SIZE), ::SCE_KERNEL_ERROR_EINVAL);
}

}  // namespace
