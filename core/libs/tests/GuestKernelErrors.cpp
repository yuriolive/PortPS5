// Unit tests for libkernel error-code contracts — threading subsystem scope.
//
// Covers the SCE error returns mandated by docs/spec/threading.md:
//   - equeue lifecycle: wait timeout, bad args, double-delete, use-after-delete
//   - error-check mutex: recursive lock reports EDEADLK, unlock/destroy sequence
//
// Ported from AnyPS5 upstream/main core/libs/tests/GuestKernelErrors.cpp
// (raw port used Require/abort + manual main(); converted to GoogleTest via
// portps5_add_gtest so cases are discovered by ctest -L unit).
// No GPU and no game data needed.
//
// Ref: docs/spec/threading.md

// SDL renames main() to SDL_main() unless told otherwise; GTest owns main here.
#define SDL_MAIN_HANDLED

#include "SceTypes.hpp"
#include "common/TestHarness.hpp"

#include <gtest/gtest.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// Helpers — forward declarations of the APS5_VABI exports under test
// ---------------------------------------------------------------------------

extern "C" {
// Creates an event queue. Returns SCE_OK on success.
int APS5_VABI sceKernelCreateEqueue(KernelEqueue* eq, const char* name) noexcept;
// Deletes an event queue. Returns SCE_OK or SCE_KERNEL_ERROR_EBADF.
int APS5_VABI sceKernelDeleteEqueue(KernelEqueue eq) noexcept;
// Waits on an event queue with timeout. Returns SCE_OK, ETIMEDOUT, EFAULT or EINVAL.
int APS5_VABI sceKernelWaitEqueue(KernelEqueue eq, KernelEvent* ev, int num, int* out, const KernelUseconds* timo) noexcept;
// Deletes a user event. Returns SCE_OK or SCE_KERNEL_ERROR_ENOENT.
int APS5_VABI sceKernelDeleteUserEvent(KernelEqueue eq, int id) noexcept;
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
// Locks a mutex. Returns SCE_OK or SCE_KERNEL_ERROR_EDEADLK on recursive lock.
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
// Unlocks a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
}

namespace {

// SCE kernel errors are 0x80020000 | FreeBSD errno (KernelErrors.hpp). The
// equeue used to return an unrelated 0x8001xxxx range; the literal-value test
// below pins the real console values so the two families cannot drift again.
constexpr int SCE_OK = ::PortPS5::Testing::SCE_OK;
constexpr int SCE_KERNEL_ERROR_ENOENT = ::PortPS5::Testing::SCE_KERNEL_ERROR_ENOENT;
constexpr int SCE_KERNEL_ERROR_EBADF = ::PortPS5::Testing::SCE_KERNEL_ERROR_EBADF;
constexpr int SCE_KERNEL_ERROR_EDEADLK = ::PortPS5::Testing::SCE_KERNEL_ERROR_EDEADLK;
constexpr int SCE_KERNEL_ERROR_EFAULT = ::PortPS5::Testing::SCE_KERNEL_ERROR_EFAULT;
constexpr int SCE_KERNEL_ERROR_EINVAL = ::PortPS5::Testing::SCE_KERNEL_ERROR_EINVAL;
constexpr int SCE_KERNEL_ERROR_ETIMEDOUT = ::PortPS5::Testing::SCE_KERNEL_ERROR_ETIMEDOUT;
constexpr int MUTEX_TYPE_ERRORCHECK = 1;

// Verifies the equeue wait/delete error contract: an empty queue times out
// instead of blocking forever; null event storage faults; zero capacity and
// unknown user-event ids are rejected; a deleted queue handle is dead for
// every entry point; null queue storage is rejected at create.
TEST(KernelErrors, EqueueWaitAndDeleteContract) {
    KernelEqueue eq = 0;
    ASSERT_EQ(sceKernelCreateEqueue(&eq, "errors"), SCE_OK);
    KernelEvent event{};
    int count = -1;
    const KernelUseconds timeout = 1000;
    EXPECT_EQ(sceKernelWaitEqueue(eq, &event, 1, &count, &timeout), SCE_KERNEL_ERROR_ETIMEDOUT);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(sceKernelWaitEqueue(eq, nullptr, 1, &count, &timeout), SCE_KERNEL_ERROR_EFAULT);
    EXPECT_EQ(sceKernelWaitEqueue(eq, &event, 0, &count, &timeout), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(sceKernelDeleteUserEvent(eq, 7), SCE_KERNEL_ERROR_ENOENT);
    EXPECT_EQ(sceKernelDeleteEqueue(eq), SCE_OK);
    EXPECT_EQ(sceKernelDeleteEqueue(eq), SCE_KERNEL_ERROR_EBADF);
    EXPECT_EQ(sceKernelWaitEqueue(eq, &event, 1, &count, &timeout), SCE_KERNEL_ERROR_EBADF);
    EXPECT_EQ(sceKernelCreateEqueue(nullptr, "errors"), SCE_KERNEL_ERROR_EINVAL);
}

// Regression for AnyPS5 97cae145: equeue errors must be the literal SCE
// codes (0x80020000 | errno), not the old 0x8001xxxx range. A guest that
// compares sceKernelWaitEqueue against SCE_KERNEL_ERROR_ETIMEDOUT
// (0x8002003C) depends on the exact number, so this test uses literals, not
// the shared constants.
TEST(KernelErrors, EqueueReturnsLiteralSceCodes) {
    KernelEqueue eq = 0;
    ASSERT_EQ(sceKernelCreateEqueue(&eq, "literal"), SCE_OK);
    KernelEvent event{};
    int count = -1;
    const KernelUseconds timeout = 1000;
    EXPECT_EQ(static_cast<std::uint32_t>(sceKernelWaitEqueue(eq, &event, 1, &count, &timeout)), 0x8002003Cu);
    EXPECT_EQ(static_cast<std::uint32_t>(sceKernelWaitEqueue(eq, nullptr, 1, &count, &timeout)), 0x8002000Eu);
    EXPECT_EQ(static_cast<std::uint32_t>(sceKernelWaitEqueue(eq, &event, 0, &count, &timeout)), 0x80020016u);
    EXPECT_EQ(static_cast<std::uint32_t>(sceKernelDeleteUserEvent(eq, 7)), 0x80020002u);
    ASSERT_EQ(sceKernelDeleteEqueue(eq), SCE_OK);
    EXPECT_EQ(static_cast<std::uint32_t>(sceKernelDeleteEqueue(eq)), 0x80020009u);
}

// Verifies error-check mutex semantics: a second lock by the owner fails with
// EDEADLK instead of deadlocking; unlock restores the lockable state so the
// destroy path runs on a released mutex.
TEST(KernelErrors, ErrorCheckMutexRecursiveLockFails) {
    PthreadMutexattr attr = nullptr;
    ASSERT_EQ(scePthreadMutexattrInit(&attr), SCE_OK);
    ASSERT_EQ(scePthreadMutexattrSettype(&attr, MUTEX_TYPE_ERRORCHECK), SCE_OK);
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, &attr, nullptr), SCE_OK);
    EXPECT_EQ(scePthreadMutexattrDestroy(&attr), SCE_OK);
    EXPECT_EQ(scePthreadMutexLock(&mutex), SCE_OK);
    EXPECT_EQ(scePthreadMutexLock(&mutex), SCE_KERNEL_ERROR_EDEADLK);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), SCE_OK);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), SCE_OK);
}

}  // namespace
