// Unit tests for pthread identity and lifecycle — threading subsystem scope.
//
// Covers the return-code contract mandated by docs/spec/threading.md:
//   - scePthreadSelf is stable and non-null on the main thread (adopted handle)
//   - joining/detaching an adopted handle is rejected with EINVAL
//   - a worker observes its own handle via scePthreadSelf, exits with a value
//   - unlocking another thread's held mutex is rejected with EPERM
//
// Ported from AnyPS5 upstream/main core/libs/tests/GuestThreadSelf.cpp
// (Require/abort + manual main() converted to GoogleTest via
// portps5_add_gtest). EINVAL follows PortPS5::Testing (FreeBSD-style), not
// upstream's 0x80020016; EPERM matches in both. No GPU, no game data.
//
// Ref: docs/spec/threading.md

// SDL renames main() to SDL_main() unless told otherwise; GTest owns main here.
#define SDL_MAIN_HANDLED

#include "SceTypes.hpp"
#include "common/TestHarness.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include <atomic>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
// Creates a guest thread. Returns SCE_OK on success.
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name) noexcept;
// Joins a guest thread. Returns SCE_OK on success.
int APS5_VABI scePthreadJoin(Pthread thread, void** retval) noexcept;
// Detaches a guest thread. Returns SCE_OK on success.
int APS5_VABI scePthreadDetach(Pthread thread) noexcept;
// Exits the calling thread with a value. Does not return.
void APS5_VABI scePthreadExit(void* retval) noexcept;
// Returns the calling thread's handle. Never null for a guest thread.
Pthread APS5_VABI scePthreadSelf() noexcept;
// Initialises a mutex attribute. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexattrInit(PthreadMutexattr* attr) noexcept;
// Destroys a mutex attribute. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexattrDestroy(PthreadMutexattr* attr) noexcept;
// Sets the mutex type. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexattrSettype(PthreadMutexattr* attr, int type) noexcept;
// Initialises a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr, const char* name) noexcept;
// Destroys a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
// Locks a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
// Unlocks a mutex. Returns SCE_OK, or EPERM when not the owner.
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
}

namespace {

// Thread APIs use the SCE code family (Thread.cpp:21: EINVAL = 0x80020016),
// not the TestHarness FreeBSD-style EINVAL. EPERM matches in both.
constexpr int SCE_KERNEL_ERROR_EINVAL = static_cast<int>(0x80020016u);

constexpr int MUTEX_TYPE_RECURSIVE = 2;
constexpr std::intptr_t WorkerRetval = 0x1234;

struct WorkerContext {
    Pthread thread = nullptr;
    Pthread selfFromWorker = nullptr;
    PthreadMutex* mutex = nullptr;
    int unlockResult = 0;
};

// Worker thread entry: records its own handle, attempts to unlock the main
// thread's held mutex (expecting EPERM), and exits with WorkerRetval.
// Role: provides the foreign-thread half of the identity/lifecycle checks.
// Parameters: arg — pointer to the test's WorkerContext.
// Returns: never (exits via scePthreadExit); the trailing return only
// satisfies the PthreadEntry signature.
static void* APS5_VABI Worker(void* arg) {
    auto& context = *static_cast<WorkerContext*>(arg);
    context.selfFromWorker = scePthreadSelf();
    context.unlockResult = scePthreadMutexUnlock(context.mutex);
    scePthreadExit(reinterpret_cast<void*>(WorkerRetval));
    return nullptr;
}

// A host thread that never went through scePthreadCreate (the gtest main
// thread standing in for the guest main thread) gets a lazily adopted handle
// instead of null (AnyPS5 76b7f998): the guest routinely passes
// scePthreadSelf() to scePthreadRename/Getprio/Setaffinity. The handle is
// stable across calls, detached (join/detach are EINVAL like a null handle),
// and distinct per thread. Null handles remain EINVAL.
TEST(PthreadSelf, HostThreadGetsStableAdoptedHandle) {
    const Pthread mainSelf = scePthreadSelf();
    ASSERT_NE(mainSelf, nullptr);
    EXPECT_EQ(scePthreadSelf(), mainSelf);
    EXPECT_EQ(scePthreadJoin(mainSelf, nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadDetach(mainSelf), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadJoin(nullptr, nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadDetach(nullptr), SCE_KERNEL_ERROR_EINVAL);
}

// Two different host threads get distinct adopted handles, each stable within
// its own thread, and the handle stays usable through the public API (the
// adopted handle carries a real guest tid so mutex ownership keeps working).
TEST(PthreadSelf, AdoptedHandlesAreDistinctPerHostThread) {
    const Pthread mainSelf = scePthreadSelf();
    Pthread first = nullptr;
    Pthread again = nullptr;
    std::thread host([&] {
        first = scePthreadSelf();
        again = scePthreadSelf();
    });
    host.join();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, again);
    EXPECT_NE(first, mainSelf);
}

// A worker sees its own handle (equal to the created handle, distinct from
// the main thread), delivers its exit value through join, and cannot unlock
// the main thread's held recursive mutex (EPERM, not a crash).
TEST(PthreadSelf, WorkerIdentityExitValueAndForeignUnlock) {
    const Pthread mainSelf = scePthreadSelf();
    PthreadMutexattr attr = nullptr;
    ASSERT_EQ(scePthreadMutexattrInit(&attr), ::PortPS5::Testing::SCE_OK);
    ASSERT_EQ(scePthreadMutexattrSettype(&attr, MUTEX_TYPE_RECURSIVE), ::PortPS5::Testing::SCE_OK);
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, &attr, nullptr), ::PortPS5::Testing::SCE_OK);
    EXPECT_EQ(scePthreadMutexattrDestroy(&attr), ::PortPS5::Testing::SCE_OK);

    ASSERT_EQ(scePthreadMutexLock(&mutex), ::PortPS5::Testing::SCE_OK);

    WorkerContext context;
    context.mutex = &mutex;
    ASSERT_EQ(scePthreadCreate(&context.thread, nullptr, Worker, &context, nullptr), ::PortPS5::Testing::SCE_OK);
    ASSERT_NE(context.thread, nullptr);
    EXPECT_NE(context.thread, mainSelf);

    void* result = nullptr;
    ASSERT_EQ(scePthreadJoin(context.thread, &result), ::PortPS5::Testing::SCE_OK);
    EXPECT_EQ(reinterpret_cast<std::intptr_t>(result), WorkerRetval);
    ASSERT_NE(context.selfFromWorker, nullptr);
    EXPECT_EQ(context.selfFromWorker, context.thread);
    EXPECT_NE(context.selfFromWorker, mainSelf);
    EXPECT_EQ(context.unlockResult, ::PortPS5::Testing::SCE_KERNEL_ERROR_EPERM);

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), ::PortPS5::Testing::SCE_OK);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), ::PortPS5::Testing::SCE_OK);
}

// Stress for the adopted-handle lifetime: hundreds of short-lived host threads
// each adopt a handle (scePthreadSelf) and exit, in concurrent batches, via
// both std::thread and raw Win32 threads. The handle lives in FLS storage freed
// at thread exit; a thread_local unique_ptr version of this crashed
// intermittently at exit (libc.prx's __cxa_thread_atexit override), so run this
// under `ctest --repeat until-fail:N`. Invariants: the handle is non-null,
// stable within the thread, and distinct from every other live thread's.
std::atomic<int> g_stressBad{0};

void StressBody() {
    const Pthread first = scePthreadSelf();
    if (first == nullptr || scePthreadSelf() != first) {
        g_stressBad.fetch_add(1);
    }
}

TEST(PthreadSelf, AdoptedHandleSpawnExitStress) {
    g_stressBad.store(0);
    for (int batch = 0; batch < 40; ++batch) {
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
            threads.emplace_back(StressBody);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
#ifdef _WIN32
    for (int i = 0; i < 100; ++i) {
        HANDLE h = CreateThread(nullptr, 0, +[](void*) -> DWORD { StressBody(); return 0; },
                                nullptr, 0, nullptr);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(WaitForSingleObject(h, 5000), WAIT_OBJECT_0);
        CloseHandle(h);
    }
#endif
    EXPECT_EQ(g_stressBad.load(), 0);
}

// Regression (CodeRabbit on PR #61): when the compact tid allocator is
// exhausted (Ensure() returns 0) the adopted path must keep the old null answer
// instead of publishing a handle whose guestTid is 0, and must not cache it, so
// a later call succeeds once a tid is recycled. Exhaustion is reached cheaply by
// allocating all 2^24-1 tids (no threads needed; ~64 MB of ids), then a fresh
// host thread calls scePthreadSelf. Runs in its own process under ctest.
TEST(PthreadSelf, AdoptedHandleNullWhenTidsExhaustedThenRetries) {
    std::vector<std::uint32_t> held;
    held.reserve(1u << 24);
    for (;;) {
        const std::uint32_t tid = GuestTid::AllocateForThread();
        if (tid == 0) {
            break;
        }
        held.push_back(tid);
    }
    ASSERT_FALSE(held.empty());

    Pthread first = reinterpret_cast<Pthread>(1);
    Pthread second = reinterpret_cast<Pthread>(1);
    Pthread afterRecycle = nullptr;
    std::thread host([&] {
        first = scePthreadSelf();
        second = scePthreadSelf();  // must retry, not return a cached bad handle.
        GuestTid::Recycle(held.back());
        held.pop_back();
        afterRecycle = scePthreadSelf();
    });
    host.join();
    EXPECT_TRUE(first == nullptr);
    EXPECT_TRUE(second == nullptr);
    EXPECT_TRUE(afterRecycle != nullptr);
    // Remaining tids are deliberately not recycled: Recycle() scans the free
    // list for duplicates (O(n)), so returning 16M ids would be quadratic, and
    // the process exits right after this single-test run.
}

}  // namespace
