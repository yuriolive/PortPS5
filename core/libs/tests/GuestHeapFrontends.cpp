// GoogleTest suite for the libc entry points that allocate through the application heap:
// strndup, asprintf (AllocatingStrings.cpp), the operator new/delete family + new_handler, aligned_alloc and
// reallocalign (HeapExtras.cpp).
//
// The application heap is pointed at a deterministic test allocator (a bump arena with a size header and a
// failure switch) registered once for the whole process by a global Environment. That lets the tests prove
// (a) results really come from the application heap (their addresses lie inside the test arena), (b) deletes
// reach the allocator's free, and (c) exhaustion is reported the documented way: nullptr + ENOMEM for the
// C-style and nothrow paths, and a logged abort (death test on the log text) for throwing `operator new`.
//
// Exceptions thrown inside libc.prx can not be caught by this binary, so every fatal path is a death test.
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
int* APS5_VABI __error_nid_postfix();
// Declaration of `strndup_nid_postfix`; its contract is documented at the definition.
char* APS5_VABI strndup_nid_postfix(const char*, std::size_t);
// Declaration of `asprintf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI asprintf_nid_postfix(char**, const char*, ...);
// Declaration of `_Znwm_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _Znwm_nid_postfix(std::size_t);
// Declaration of `_Znam_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _Znam_nid_postfix(std::size_t);
// Declaration of `_ZnwmRKSt9nothrow_t_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _ZnwmRKSt9nothrow_t_nid_postfix(std::size_t, const void*);
// Declaration of `_ZnamRKSt9nothrow_t_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _ZnamRKSt9nothrow_t_nid_postfix(std::size_t, const void*);
// Declaration of `_ZnwmSt11align_val_t_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _ZnwmSt11align_val_t_nid_postfix(std::size_t, std::size_t);
// Declaration of `_ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI _ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix(std::size_t, std::size_t, const void*);
// Declaration of `_ZdlPv_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdlPv_nid_postfix(void*);
// Declaration of `_ZdaPv_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdaPv_nid_postfix(void*);
// Declaration of `_ZdlPvm_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdlPvm_nid_postfix(void*, std::size_t);
// Declaration of `_ZdlPvRKSt9nothrow_t_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdlPvRKSt9nothrow_t_nid_postfix(void*, const void*);
// Declaration of `_ZdlPvSt11align_val_t_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdlPvSt11align_val_t_nid_postfix(void*, std::size_t);
// Declaration of `_ZdlPvmSt11align_val_t_nid_postfix`; its contract is documented at the definition.
void APS5_VABI _ZdlPvmSt11align_val_t_nid_postfix(void*, std::size_t, std::size_t);
using GuestNewHandler = void (APS5_VABI *)();
// Declaration of `_ZSt15set_new_handlerPFvvE_nid_postfix`; its contract is documented at the definition.
GuestNewHandler APS5_VABI _ZSt15set_new_handlerPFvvE_nid_postfix(GuestNewHandler);
// Declaration of `_ZSt15get_new_handlerv_nid_postfix`; its contract is documented at the definition.
GuestNewHandler APS5_VABI _ZSt15get_new_handlerv_nid_postfix();
// Declaration of `aligned_alloc_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI aligned_alloc_nid_postfix(std::size_t, std::size_t);
// Declaration of `reallocalign_nid_postfix`; its contract is documented at the definition.
void* APS5_VABI reallocalign_nid_postfix(void*, std::size_t, std::size_t);
extern unsigned char _ZSt7nothrow_nid_postfix;
}

namespace {

constexpr int Einval = 22;
constexpr int Enomem = 12;

// Bump arena: each block is preceded by a 16-byte header holding its size. Nothing is reused; `frees`
// counts releases so tests can see which delete overloads reached the allocator.
alignas(4096) std::array<unsigned char, 1 << 20> g_arena;
std::size_t g_used = 0;
std::atomic<bool> g_fail{false};
std::atomic<int> g_frees{0};

bool InArena(const void* pointer) {
    const auto* bytes = static_cast<const unsigned char*>(pointer);
    return bytes >= g_arena.data() && bytes < g_arena.data() + g_arena.size();
}

void* ArenaAlign(std::size_t alignment, std::size_t bytes) {
    if (g_fail) return nullptr;
    alignment = alignment < 16 ? 16 : alignment;
    const auto base = reinterpret_cast<std::uintptr_t>(g_arena.data()) + g_used + 16;
    const auto aligned = (base + alignment - 1) & ~(alignment - 1);
    const auto end = aligned + bytes - reinterpret_cast<std::uintptr_t>(g_arena.data());
    if (end > g_arena.size()) return nullptr;
    std::memcpy(reinterpret_cast<void*>(aligned - 16), &bytes, sizeof(bytes));
    g_used = static_cast<std::size_t>(end);
    return reinterpret_cast<void*>(aligned);
}

/// Test allocator callbacks (System V ABI, as a title's replacement table would provide).
void* APS5_VABI ArenaAllocate(std::size_t bytes) { return ArenaAlign(16, bytes); }
/// Counts a release; null is ignored like free(nullptr).
void APS5_VABI ArenaFree(void* pointer) { if (pointer) ++g_frees; }
/// Zeroed bump allocation.
void* APS5_VABI ArenaCalloc(std::size_t count, std::size_t bytes) {
    void* pointer = ArenaAlign(16, count * bytes);
    if (pointer) std::memset(pointer, 0, count * bytes);
    return pointer;
}
/// Allocate-copy-abandon realloc using the size header.
void* APS5_VABI ArenaRealloc(void* pointer, std::size_t bytes) {
    void* moved = ArenaAlign(16, bytes);
    if (moved && pointer) {
        std::size_t old = 0;
        std::memcpy(&old, static_cast<unsigned char*>(pointer) - 16, sizeof(old));
        std::memcpy(moved, pointer, old < bytes ? old : bytes);
    }
    return moved;
}
/// Aligned bump allocation.
void* APS5_VABI ArenaMemalign(std::size_t alignment, std::size_t bytes) { return ArenaAlign(alignment, bytes); }
/// posix_memalign flavour: 0 on success, 12 when the arena is exhausted or failure injection is on.
int APS5_VABI ArenaPosixMemalign(void** result, std::size_t alignment, std::size_t bytes) {
    *result = ArenaAlign(alignment, bytes);
    return *result ? 0 : 12;
}

// Registers the test allocator as the process-wide application heap before any test runs.
class HeapEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::array<void*, 10> api{};
        api[0] = reinterpret_cast<void*>(ArenaAllocate);
        api[1] = reinterpret_cast<void*>(ArenaFree);
        api[2] = reinterpret_cast<void*>(ArenaCalloc);
        api[3] = reinterpret_cast<void*>(ArenaRealloc);
        api[4] = reinterpret_cast<void*>(ArenaMemalign);
        api[5] = reinterpret_cast<void*>(ArenaMemalign);
        api[6] = reinterpret_cast<void*>(ArenaPosixMemalign);
        ApplicationHeapRegister_nid_no_patch(api.data());
    }
};
const auto* const g_environment = ::testing::AddGlobalTestEnvironment(new HeapEnvironment);

// Resets failure injection and the handler so tests stay independent.
class HeapFrontend : public ::testing::Test {
protected:
    void SetUp() override { g_fail = false; _ZSt15set_new_handlerPFvvE_nid_postfix(nullptr); g_frees = 0; }
    void TearDown() override { g_fail = false; _ZSt15set_new_handlerPFvvE_nid_postfix(nullptr); }
};

std::atomic<int> g_handlerCalls{0};
// new_handler that "frees memory" by lifting the injected failure, so the retry succeeds.
void APS5_VABI RecoveringHandler() { ++g_handlerCalls; g_fail = false; }

}  // namespace

// Invariant: strndup copies at most `limit` bytes, always terminates, allocates from the application heap, and
// reports null source (EINVAL) and exhaustion (ENOMEM) through nullptr + guest errno.
TEST_F(HeapFrontend, StrndupContract) {
    char* copy = strndup_nid_postfix("hello", 3);
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, "hel");
    EXPECT_TRUE(InArena(copy));
    EXPECT_STREQ(strndup_nid_postfix("hi", 100), "hi");
    EXPECT_STREQ(strndup_nid_postfix("hi", 0), "");
    const char unterminated[3] = {'a', 'b', 'c'};
    EXPECT_STREQ(strndup_nid_postfix(unterminated, 3), "abc");  // never reads past `limit`
    *__error_nid_postfix() = 0;
    EXPECT_EQ(strndup_nid_postfix(nullptr, 4), nullptr);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    g_fail = true;
    EXPECT_EQ(strndup_nid_postfix("x", 1), nullptr);
    EXPECT_EQ(*__error_nid_postfix(), Enomem);
}

// Invariant: asprintf stores a heap string and returns its length; null destination/format give -1/EINVAL with
// *destination nulled; exhaustion gives -1/ENOMEM with *destination nulled.
TEST_F(HeapFrontend, AsprintfContract) {
    char* text = reinterpret_cast<char*>(0x1);
    EXPECT_EQ(asprintf_nid_postfix(&text, "%d-%s-%.2f", 42, "x", 1.5), 9);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "42-x-1.50");
    EXPECT_TRUE(InArena(text));
    *__error_nid_postfix() = 0;
    EXPECT_EQ(asprintf_nid_postfix(nullptr, "x"), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    text = reinterpret_cast<char*>(0x1);
    EXPECT_EQ(asprintf_nid_postfix(&text, nullptr), -1);
    EXPECT_EQ(text, nullptr);
    g_fail = true;
    text = reinterpret_cast<char*>(0x1);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(asprintf_nid_postfix(&text, "%s", "abc"), -1);
    EXPECT_EQ(text, nullptr);
    EXPECT_EQ(*__error_nid_postfix(), Enomem);
#ifdef _WIN32
    // The Windows formatter rejects unsupported conversions by throwing internally; asprintf must turn that
    // into -1/EINVAL instead of letting the exception reach the guest.
    g_fail = false;
    *__error_nid_postfix() = 0;
    EXPECT_EQ(asprintf_nid_postfix(&text, "%q", 1), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(text, nullptr);
#endif
}

// Invariant: every operator new form returns a usable non-null pointer from the application heap (size 0
// included) and every operator delete form, including nullptr, releases through the allocator's free.
TEST_F(HeapFrontend, NewAndDeleteFamilies) {
    void* zero = _Znwm_nid_postfix(0);
    ASSERT_NE(zero, nullptr);
    EXPECT_TRUE(InArena(zero));
    _ZdlPv_nid_postfix(zero);
    EXPECT_EQ(g_frees.load(), 1);
    _ZdlPv_nid_postfix(nullptr);
    EXPECT_EQ(g_frees.load(), 1);
    void* array = _Znam_nid_postfix(24);
    _ZdaPv_nid_postfix(array);
    void* sized = _Znwm_nid_postfix(8);
    _ZdlPvm_nid_postfix(sized, 8);
    void* nothrow = _ZnwmRKSt9nothrow_t_nid_postfix(8, &_ZSt7nothrow_nid_postfix);
    ASSERT_NE(nothrow, nullptr);
    _ZdlPvRKSt9nothrow_t_nid_postfix(nothrow, &_ZSt7nothrow_nid_postfix);
    void* aligned = _ZnwmSt11align_val_t_nid_postfix(40, 256);
    ASSERT_NE(aligned, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) & 255, 0u);
    _ZdlPvSt11align_val_t_nid_postfix(aligned, 256);
    void* sizedAligned = _ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix(40, 128, &_ZSt7nothrow_nid_postfix);
    ASSERT_NE(sizedAligned, nullptr);
    _ZdlPvmSt11align_val_t_nid_postfix(sizedAligned, 40, 128);
    EXPECT_EQ(g_frees.load(), 6);
}

// Invariant (regression): nothrow forms really are nothrow. Upstream's version called the throwing allocator,
// so exhaustion escaped as an exception into guest code. Here exhaustion and invalid alignment give nullptr.
TEST_F(HeapFrontend, NothrowNewReturnsNullInsteadOfThrowing) {
    g_fail = true;
    EXPECT_EQ(_ZnwmRKSt9nothrow_t_nid_postfix(8, &_ZSt7nothrow_nid_postfix), nullptr);
    EXPECT_EQ(_ZnamRKSt9nothrow_t_nid_postfix(8, &_ZSt7nothrow_nid_postfix), nullptr);
    EXPECT_EQ(_ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix(8, 64, &_ZSt7nothrow_nid_postfix), nullptr);
    g_fail = false;
    EXPECT_EQ(_ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix(8, 3, &_ZSt7nothrow_nid_postfix), nullptr);  // not a power of two
}

// Invariant: set_new_handler returns the previous handler and get_new_handler observes it; a throwing new
// that fails calls the handler and retries (here the handler lifts the failure, so the retry succeeds).
TEST_F(HeapFrontend, NewHandlerIsHonoured) {
    EXPECT_EQ(_ZSt15get_new_handlerv_nid_postfix(), nullptr);
    EXPECT_EQ(_ZSt15set_new_handlerPFvvE_nid_postfix(RecoveringHandler), nullptr);
    EXPECT_EQ(_ZSt15get_new_handlerv_nid_postfix(), RecoveringHandler);
    g_handlerCalls = 0;
    g_fail = true;
    void* pointer = _Znwm_nid_postfix(32);
    EXPECT_NE(pointer, nullptr);
    EXPECT_EQ(g_handlerCalls.load(), 1);
    EXPECT_EQ(_ZSt15set_new_handlerPFvvE_nid_postfix(nullptr), RecoveringHandler);
}

// Invariant: aligned_alloc / reallocalign(nullptr, ...) honour alignment and report EINVAL (bad alignment) or
// ENOMEM (exhausted) via nullptr + guest errno instead of throwing.
TEST_F(HeapFrontend, AlignedAllocContract) {
    void* pointer = aligned_alloc_nid_postfix(64, 100);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) & 63, 0u);
    void* viaRealloc = reallocalign_nid_postfix(nullptr, 100, 128);
    ASSERT_NE(viaRealloc, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(viaRealloc) & 127, 0u);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(aligned_alloc_nid_postfix(3, 8), nullptr);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(aligned_alloc_nid_postfix(0, 8), nullptr);
    g_fail = true;
    *__error_nid_postfix() = 0;
    EXPECT_EQ(aligned_alloc_nid_postfix(16, 8), nullptr);
    EXPECT_EQ(*__error_nid_postfix(), Enomem);
}

// Invariant: a throwing operator new with no new_handler and an exhausted heap ends in the logged abort
// (never a host exception); reallocalign on an existing block is an unsupported-state abort.
TEST_F(HeapFrontend, FatalPathsAbortWithLoggedReason) {
    EXPECT_DEATH({ g_fail = true; _Znwm_nid_postfix(8); }, "operator new: out of memory and no new_handler installed");
    EXPECT_DEATH({ g_fail = true; _ZnwmSt11align_val_t_nid_postfix(8, 64); }, "operator new: out of memory");
    EXPECT_DEATH({ int value = 0; reallocalign_nid_postfix(&value, 8, 16); }, "reallocalign of an existing block");
}

// Invariant (review regression): a non-power-of-two alignment can never be fixed by a new_handler, so throwing
// aligned new must abort at once even when a handler is installed (it used to call the handler forever).
TEST_F(HeapFrontend, InvalidAlignmentAbortsEvenWithNewHandler) {
    EXPECT_DEATH({ _ZSt15set_new_handlerPFvvE_nid_postfix(RecoveringHandler); _ZnwmSt11align_val_t_nid_postfix(8, 24); },
                 "alignment is not a power of two");
}
