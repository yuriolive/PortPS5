// Unit tests for pthread cond timedwait — threading subsystem scope.
//
// Verifies a thread blocked in scePthreadCondTimedwait wakes with
// ETIMEDOUT slices (not a hang or success) until the mutex holder releases,
// then joins cleanly. Precondition: futex-backed cond/mutex (threading-futex).
//
// Ported from AnyPS5 upstream/main core/libs/tests/GuestCondTimedwait.cpp
// (Require/abort + manual main() converted to GoogleTest via
// portps5_add_gtest). ETIMEDOUT follows PortPS5::Testing (FreeBSD-style),
// not upstream's 0x8002003C. No GPU and no game data needed.
//
// Ref: docs/spec/threading.md

// SDL renames main() to SDL_main() unless told otherwise; GTest owns main here.
#define SDL_MAIN_HANDLED

#include "SceTypes.hpp"
#include "common/TestHarness.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>

extern "C" {
// Creates a guest thread. Returns SCE_OK on success.
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name) noexcept;
// Joins a guest thread. Returns SCE_OK on success.
int APS5_VABI scePthreadJoin(Pthread thread, void** retval) noexcept;
// Initialises a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr, const char* name) noexcept;
// Destroys a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
// Locks a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
// Unlocks a mutex. Returns SCE_OK on success.
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
// Initialises a condition variable. Returns SCE_OK on success.
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr, const char* name) noexcept;
// Destroys a condition variable. Returns SCE_OK on success.
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond) noexcept;
// Waits on a condition with a microsecond timeout. Returns SCE_OK or ETIMEDOUT.
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex, unsigned int usec) noexcept;
}

namespace {

// Cond timedwait reports kSceTimedOut (0x8002003C, SyncWords.hpp:27): the
// cond family uses SCE codes, unlike the equeue FreeBSD-style ETIMEDOUT.
// Upstream's 0x8002003C happens to match here; the TestHarness alias does not.
constexpr int SCE_KERNEL_ERROR_ETIMEDOUT = static_cast<int>(0x8002003Cu);

struct Context {
    PthreadMutex mutex = nullptr;
    std::atomic<bool> acquired{false};
};

// Contender thread entry: locks the shared mutex (blocking until the main
// test thread unlocks), flags acquisition, unlocks, and exits.
// Role: provides the waiter half of the timedwait-slice scenario.
// Parameters: arg — pointer to the test's Context (mutex + acquired flag).
// Returns: always nullptr; lock/unlock failures surface via EXPECT_*.
static void* APS5_VABI Contender(void* arg) {
    auto& context = *static_cast<Context*>(arg);
    EXPECT_EQ(scePthreadMutexLock(&context.mutex), ::PortPS5::Testing::SCE_OK);
    context.acquired.store(true);
    EXPECT_EQ(scePthreadMutexUnlock(&context.mutex), ::PortPS5::Testing::SCE_OK);
    return nullptr;
}

// A contender blocked on a held mutex is observable through timedwait slices:
// every slice reports ETIMEDOUT until the holder unlocks, then the contender
// acquires, releases, and joins. Failure mode: a hang (slice returns OK early
// without progress) or a lost wakeup (join blocks forever).
TEST(PthreadCond, TimedwaitSlicesUntilUnlock) {
    Context context;
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&context.mutex, nullptr, nullptr), ::PortPS5::Testing::SCE_OK);
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, nullptr), ::PortPS5::Testing::SCE_OK);

    ASSERT_EQ(scePthreadMutexLock(&context.mutex), ::PortPS5::Testing::SCE_OK);
    Pthread thread = nullptr;
    ASSERT_EQ(scePthreadCreate(&thread, nullptr, Contender, &context, nullptr), ::PortPS5::Testing::SCE_OK);
    while (!context.acquired.load())
        EXPECT_EQ(scePthreadCondTimedwait(&cond, &context.mutex, 1000), SCE_KERNEL_ERROR_ETIMEDOUT);
    EXPECT_EQ(scePthreadMutexUnlock(&context.mutex), ::PortPS5::Testing::SCE_OK);
    EXPECT_EQ(scePthreadJoin(thread, nullptr), ::PortPS5::Testing::SCE_OK);

    EXPECT_EQ(scePthreadCondDestroy(&cond), ::PortPS5::Testing::SCE_OK);
    EXPECT_EQ(scePthreadMutexDestroy(&context.mutex), ::PortPS5::Testing::SCE_OK);
}

}  // namespace
