// GoogleTest suite for the application heap's built-in fallback (prx/libc/src/ApplicationHeap.cpp).
//
// When a title's libc parameters carry an allocator replacement table whose slots are all empty (an SDK
// startup that does not replace malloc), the heap must fall back to the guest heap instead of failing with
// "incomplete allocator API" (AnyPS5 67fce999). These tests drive the real libc.prx implementation with
// hand-built metadata blocks (layout: process parameters 0x40 bytes, libc parameters 0x38, replacement table
// 0x78), so no game data is involved.
//
// The heap is process-global and initialises once, so every test calls InitializeDefaultHeap() first (it is
// idempotent). Exceptions thrown inside libc.prx can not be caught here, hence the death tests.
#include "prx/libc/include/ApplicationHeap.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>

namespace {

// Metadata blocks live for the whole process because the heap keeps reading the replacement table pointer.
std::array<std::byte, 0x40> g_process{};
std::array<std::byte, 0x38> g_libc{};
std::array<std::byte, 0x78> g_replacement{};

template<typename TValue, std::size_t TSize>
void Write(std::array<std::byte, TSize>& data, std::size_t offset, TValue value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

// Builds metadata with a replacement table of correct size/version but no allocator entries, then
// initializes the application heap from it.
void InitializeDefaultHeap() {
    Write(g_process, 0, std::uint64_t{0x40});
    Write(g_process, 8, std::uint32_t{0x4942524f});
    Write(g_process, 0x38, g_libc.data());
    Write(g_libc, 0, std::uint64_t{0x38});
    Write(g_libc, 0x30, g_replacement.data());
    Write(g_replacement, 0, std::uint64_t{0x78});
    Write(g_replacement, 8, std::uint64_t{2});
    ApplicationHeapInitialize_nid_no_patch(g_process.data());
}

// Runs `action`; if the heap reports misuse by throwing, print the exception text and abort so EXPECT_DEATH
// can match on what() (an exception escaping a death-test statement is otherwise reported as a test error).
template<typename TAction>
void DieOnThrow(TAction action) {
    try {
        action();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        std::abort();
    }
}

// Registers a table with only the malloc slot filled, which the heap must reject as incomplete. Kept out of
// the EXPECT_DEATH argument because the template commas would split the macro arguments.
void RegisterPartialTable() {
    std::array<void*, 10> partial{};
    partial[0] = reinterpret_cast<void*>(0x1);
    ApplicationHeapRegister_nid_no_patch(partial.data());
}

}  // namespace

// Invariant: with an empty replacement table, calloc zero-fills, realloc preserves the prefix when growing
// and shrinking, realloc(p, 0) frees and returns null, and free(nullptr) is harmless.
TEST(ApplicationHeapDefault, AllocateCallocReallocFree) {
    InitializeDefaultHeap();
    auto* pointer = static_cast<unsigned char*>(ApplicationHeapCalloc_nid_no_patch(7, 9));
    ASSERT_NE(pointer, nullptr);
    for (unsigned i = 0; i < 63; ++i) ASSERT_EQ(pointer[i], 0);
    std::memset(pointer, 0x5a, 63);
    pointer = static_cast<unsigned char*>(ApplicationHeapReallocate_nid_no_patch(pointer, 150));
    for (unsigned i = 0; i < 63; ++i) ASSERT_EQ(pointer[i], 0x5a);
    pointer = static_cast<unsigned char*>(ApplicationHeapReallocate_nid_no_patch(pointer, 11));
    for (unsigned i = 0; i < 11; ++i) ASSERT_EQ(pointer[i], 0x5a);
    EXPECT_EQ(ApplicationHeapReallocate_nid_no_patch(pointer, 0), nullptr);
    pointer = static_cast<unsigned char*>(ApplicationHeapReallocate_nid_no_patch(nullptr, 32));
    ASSERT_NE(pointer, nullptr);
    ApplicationHeapFree_nid_no_patch(pointer);
    ApplicationHeapFree_nid_no_patch(nullptr);
}

// Invariant: aligned allocation through the fallback honours the requested alignment for memalign-style and
// posix_memalign-style entry points.
TEST(ApplicationHeapDefault, AlignedAllocation) {
    InitializeDefaultHeap();
    for (std::size_t alignment : {16, 64, 4096}) {
        void* aligned = ApplicationHeapAlign_nid_no_patch(alignment, 37);
        ASSERT_NE(aligned, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % alignment, 0u);
        ApplicationHeapFree_nid_no_patch(aligned);
    }
    void* aligned = nullptr;
    ASSERT_EQ(ApplicationHeapPosixAlign_nid_no_patch(&aligned, 256, 99), 0);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % 256, 0u);
    ApplicationHeapFree_nid_no_patch(aligned);
}

// Invariant: initialization is idempotent (a second call must not re-register or fail), and misuse still
// fails loudly: calloc overflow, bad alignment and a partially filled replacement table are fatal.
TEST(ApplicationHeapDefault, MisuseIsFatalAndInitIsIdempotent) {
    InitializeDefaultHeap();
    InitializeDefaultHeap();
    EXPECT_DEATH(DieOnThrow([] { InitializeDefaultHeap(); ApplicationHeapCalloc_nid_no_patch(std::numeric_limits<std::size_t>::max(), 2); }), "calloc size overflow");
    EXPECT_DEATH(DieOnThrow([] { InitializeDefaultHeap(); ApplicationHeapAlign_nid_no_patch(3, 16); }), "invalid alignment");
    EXPECT_DEATH(DieOnThrow(RegisterPartialTable), "incomplete allocator API");
}
