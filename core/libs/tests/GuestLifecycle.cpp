// GoogleTest suite for the libc process/thread lifecycle exports: cxa_atexit / __cxa_atexit, cxa_finalize,
// LibcInternalExtCxaThreadAtexit and init_env (core/libs/prx/libc/src/RuntimeSupport.cpp, CxxAbiSupport.cpp).
//
// Ported from AnyPS5 commits 3bf11558, 40eeb4be and 9f81bddf and converted to GoogleTest. All handlers used
// here are APS5_VABI (System V) functions because that is what guest code registers; a handler compiled with
// the default Windows convention would read its argument from the wrong register, which these tests detect
// by checking the argument each handler receives.
//
// init_env raises a host C++ exception when the test executable carries no relinked process metadata.
// Exceptions thrown inside libc.prx can not be caught by the test binary (separate unwinder), so that case
// is asserted with a death test matched on the exception's what() text.
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

struct InitEnvParams {
    int argc;
    std::uint32_t pad;
    const char* argv[3];
};

extern "C" {
// Declaration of `cxa_atexit_nid_postfix`; its contract is documented at the definition.
int APS5_VABI cxa_atexit_nid_postfix(void (APS5_VABI *)(void*), void*, void*);
// Declaration of `__cxa_atexit_nid_postfix`; its contract is documented at the definition.
int APS5_VABI __cxa_atexit_nid_postfix(void (APS5_VABI *)(void*), void*, void*);
// Declaration of `cxa_finalize_nid_postfix`; its contract is documented at the definition.
void APS5_VABI cxa_finalize_nid_postfix(void*);
// Declaration of `__cxa_finalize_nid_postfix`; its contract is documented at the definition.
void APS5_VABI __cxa_finalize_nid_postfix(void*);
// Declaration of `LibcInternalExtCxaThreadAtexit_nid_postfix`; its contract is documented at the definition.
int APS5_VABI LibcInternalExtCxaThreadAtexit_nid_postfix(void (APS5_VABI *)(void*), void*, void*);
// Declaration of `__cxa_thread_atexit_impl_nid_postfix`; its contract is documented at the definition.
int APS5_VABI __cxa_thread_atexit_impl_nid_postfix(void (APS5_VABI *)(void*), void*, void*);
// Declaration of `init_env_nid_postfix`; its contract is documented at the definition.
void APS5_VABI init_env_nid_postfix(const InitEnvParams*);
}

namespace {

// Records the order in which handlers ran and the argument each one received.
std::vector<int> g_order;
std::vector<void*> g_args;

/// Exit handlers that record their order and received argument.
void APS5_VABI RecordOne(void* arg) { g_order.push_back(1); g_args.push_back(arg); }
/// See RecordOne.
void APS5_VABI RecordTwo(void* arg) { g_order.push_back(2); g_args.push_back(arg); }
/// See RecordOne.
void APS5_VABI RecordThree(void* arg) { g_order.push_back(3); g_args.push_back(arg); }

// Handler that registers another handler for the same dso while finalize is running.
void APS5_VABI RegisterFromHandler(void* dso) {
    g_order.push_back(10);
    cxa_atexit_nid_postfix(RecordThree, nullptr, dso);
}

std::atomic<int> g_threadCleanups{0};
std::atomic<void*> g_threadArg{nullptr};
/// Thread-exit destructor that records how often it ran and the argument it received.
void APS5_VABI ThreadDone(void* arg) { g_threadArg = arg; ++g_threadCleanups; }

class Lifecycle : public ::testing::Test {
protected:
    void SetUp() override { g_order.clear(); g_args.clear(); }
};

}  // namespace

// Invariant: finalize(dso) runs only that dso's handlers, newest first, passing each its own argument, and
// leaves other dsos' handlers registered until their own finalize.
TEST_F(Lifecycle, FinalizeRunsMatchingHandlersNewestFirst) {
    void* first = reinterpret_cast<void*>(0x1001);
    void* second = reinterpret_cast<void*>(0x1002);
    int tokenA = 0, tokenB = 0, tokenC = 0;
    ASSERT_EQ(cxa_atexit_nid_postfix(RecordOne, &tokenA, first), 0);
    ASSERT_EQ(__cxa_atexit_nid_postfix(RecordTwo, &tokenB, first), 0);  // the un-aliased export shares the registry
    ASSERT_EQ(cxa_atexit_nid_postfix(RecordThree, &tokenC, second), 0);

    cxa_finalize_nid_postfix(first);
    EXPECT_EQ(g_order, (std::vector<int>{2, 1}));
    EXPECT_EQ(g_args, (std::vector<void*>{&tokenB, &tokenA}));

    cxa_finalize_nid_postfix(reinterpret_cast<void*>(0x1003));  // unknown dso: nothing runs
    EXPECT_EQ(g_order.size(), 2u);

    __cxa_finalize_nid_postfix(second);  // libSceLibcInternal-style entry point, same registry
    EXPECT_EQ(g_order, (std::vector<int>{2, 1, 3}));

    cxa_finalize_nid_postfix(first);  // already drained: handlers must not run twice
    EXPECT_EQ(g_order.size(), 3u);
}

// Invariant: finalize(nullptr) runs handlers of every dso (process-exit behaviour) newest first.
TEST_F(Lifecycle, FinalizeNullRunsEverything) {
    ASSERT_EQ(cxa_atexit_nid_postfix(RecordOne, nullptr, reinterpret_cast<void*>(0x2001)), 0);
    ASSERT_EQ(cxa_atexit_nid_postfix(RecordTwo, nullptr, reinterpret_cast<void*>(0x2002)), 0);
    cxa_finalize_nid_postfix(nullptr);
    EXPECT_EQ(g_order, (std::vector<int>{2, 1}));
}

// Invariant: a handler may register another handler during finalize without deadlocking, and the new
// handler also runs before finalize returns (the registry lock is not held across handler calls).
TEST_F(Lifecycle, HandlerMayRegisterDuringFinalize) {
    void* dso = reinterpret_cast<void*>(0x3001);
    ASSERT_EQ(cxa_atexit_nid_postfix(RegisterFromHandler, dso, dso), 0);
    cxa_finalize_nid_postfix(dso);
    EXPECT_EQ(g_order, (std::vector<int>{10, 3}));
}

// Invariant: a thread-exit destructor registered through either export runs on thread exit with the exact
// argument the guest passed (proves the System V calling convention is honoured through the trampoline),
// and a null destructor is rejected with EINVAL (22).
TEST(ThreadAtexit, RunsGuestDestructorWithItsArgument) {
    int marker = 0;
    g_threadCleanups = 0;
    g_threadArg = nullptr;
    std::thread worker([&] {
        void* image =
#ifdef _WIN32
            reinterpret_cast<void*>(GetModuleHandleW(nullptr));
#else
            &marker;
#endif
        EXPECT_EQ(LibcInternalExtCxaThreadAtexit_nid_postfix(ThreadDone, &marker, image), 0);
        EXPECT_EQ(LibcInternalExtCxaThreadAtexit_nid_postfix(nullptr, &marker, image), 22);
    });
    worker.join();
    EXPECT_EQ(g_threadCleanups.load(), 1);
    EXPECT_EQ(g_threadArg.load(), &marker);

    g_threadCleanups = 0;
    std::thread second([&] {
        EXPECT_EQ(__cxa_thread_atexit_impl_nid_postfix(ThreadDone, &marker, nullptr), 0);
    });
    second.join();
    EXPECT_EQ(g_threadCleanups.load(), 1);
}

// Invariant: init_env is no longer an unimplemented stub. Without relinked process metadata it must fail
// with the application-heap diagnostic (real initialization), never with a "not implemented" abort.
TEST(InitEnv, DelegatesToApplicationHeapInitialization) {
    // Turn the host exception into an abort that prints its what() text, so the death test can match it.
    const auto initializeOrDie = [] {
        try {
            init_env_nid_postfix(nullptr);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            std::abort();
        }
    };
    EXPECT_DEATH(initializeOrDie(), "application heap:");
}
