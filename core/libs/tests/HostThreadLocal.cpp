// Unit tests for host thread-local storage — libc scope.
//
// Verifies HostThreadLocal<T, Tag> lifetime: every touch on a fresh thread
// constructs exactly one Value per tag, and thread exit destroys them (the
// destroyed counter advances by the touch count). Covers std::thread,
// Win32 CreateThread, and std::async workers.
//
// Ported from AnyPS5 upstream/main core/libs/tests/HostThreadLocal.cpp
// (abort() + manual main() converted to GoogleTest via portps5_add_gtest).
// No GPU and no game data needed.
//
// Ref: docs/spec/threading.md

// SDL renames main() to SDL_main() unless told otherwise; GTest owns main here.
#define SDL_MAIN_HANDLED

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void TouchHostThreadLocal();
extern "C" unsigned DestroyedHostThreadLocals();
extern "C" unsigned HostThreadLocalViolations();

namespace {

// Each joined std::thread that touches twice must destroy exactly two Values.
// Failure mode: a leak (counter stalls) or cross-thread aliasing (violations).
TEST(HostThreadLocal, StdThreadTouchDestroyBalance) {
    for (unsigned i = 0; i < 64; ++i) {
        std::thread worker([] {
            TouchHostThreadLocal();
            TouchHostThreadLocal();
        });
        worker.join();
        EXPECT_EQ(DestroyedHostThreadLocals(), (i + 1) * 2);
    }
    EXPECT_EQ(HostThreadLocalViolations(), 0u);
}

#ifdef _WIN32
// Raw Win32 threads (no C++ runtime thread object) get the same guarantee.
// Baseline is relative so the case also passes under --gtest_filter.
TEST(HostThreadLocal, Win32ThreadTouchDestroyBalance) {
    const unsigned before = DestroyedHostThreadLocals();
    for (unsigned i = 0; i < 64; ++i) {
        const auto worker = CreateThread(
            nullptr, 0, +[](void*) -> DWORD {
                TouchHostThreadLocal();
                return 0;
            },
            nullptr, 0, nullptr);
        ASSERT_NE(worker, nullptr);
        ASSERT_EQ(WaitForSingleObject(worker, 5000), WAIT_OBJECT_0);
        CloseHandle(worker);
        EXPECT_EQ(DestroyedHostThreadLocals(), before + (i + 1) * 2);
    }
    EXPECT_EQ(HostThreadLocalViolations(), 0u);
}
#endif

// Four async workers touching twice each add exactly eight destructions per
// round, whatever the pool reuse pattern is.
TEST(HostThreadLocal, AsyncWorkerTouchDestroyBalance) {
    for (unsigned i = 0; i < 16; ++i) {
        const auto before = DestroyedHostThreadLocals();
        std::array<std::future<void>, 4> workers;
        for (auto& worker : workers) {
            worker = std::async(std::launch::async, [] {
                TouchHostThreadLocal();
                TouchHostThreadLocal();
            });
        }
        for (auto& worker : workers) worker.get();
        EXPECT_EQ(DestroyedHostThreadLocals(), before + 8);
    }
    EXPECT_EQ(HostThreadLocalViolations(), 0u);
}

}  // namespace
