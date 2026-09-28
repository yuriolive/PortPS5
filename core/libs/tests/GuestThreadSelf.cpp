// Unit tests for pthread identity and lifecycle — threading subsystem scope.
//
// Covers the return-code contract mandated by docs/spec/threading.md:
//   - scePthreadSelf is stable and non-null on the main thread
//   - joining/detaching the main thread is rejected with EINVAL
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

static void* APS5_VABI Worker(void* arg) {
    auto& context = *static_cast<WorkerContext*>(arg);
    context.selfFromWorker = scePthreadSelf();
    context.unlockResult = scePthreadMutexUnlock(context.mutex);
    scePthreadExit(reinterpret_cast<void*>(WorkerRetval));
    return nullptr;
}

// The host main thread is not a guest thread: scePthreadSelf() returns null
// (Thread.cpp:440 returns thread-local currentThread, never registered for
// the host main). Joining or detaching a null handle fails with EINVAL
// (Thread.cpp:340-341,381-382), which is what this test pins.
TEST(PthreadSelf, MainThreadHasNoGuestHandleAndNullJoinRejected) {
    EXPECT_EQ(scePthreadSelf(), nullptr);
    EXPECT_EQ(scePthreadJoin(nullptr, nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadDetach(nullptr), SCE_KERNEL_ERROR_EINVAL);
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

}  // namespace
