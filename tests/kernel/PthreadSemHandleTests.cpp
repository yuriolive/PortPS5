// tests/kernel/PthreadSemHandleTests.cpp
// Verification for the scePthreadSem* exports (AnyPS5 064006b6, futex-adapted).
//
// Covers the return-code contract (EINVAL / EBUSY / ETIMEDOUT / EOVERFLOW),
// handle lifecycle (destroy, use-after-destroy, double destroy), blocking and
// timed waits, and a multi-thread token-conservation stress that would expose a
// lost wakeup or a double-taken token in the lock-free implementation.
// No GPU and no game data needed.

#include "common/TestHarness.hpp"
#include "SceTypes.hpp"
#include "prx/libkernel/Pthread/include/SemBarrierTypes.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI scePthreadSemInit(PthreadSem* sem, int flag, unsigned int value, const char* name) noexcept;
int APS5_VABI scePthreadSemDestroy(PthreadSem* sem) noexcept;
int APS5_VABI scePthreadSemPost(PthreadSem* sem) noexcept;
int APS5_VABI scePthreadSemWait(PthreadSem* sem) noexcept;
int APS5_VABI scePthreadSemTrywait(PthreadSem* sem) noexcept;
int APS5_VABI scePthreadSemTimedwait(PthreadSem* sem, KernelUseconds usec) noexcept;
int APS5_VABI scePthreadSemGetvalue(PthreadSem* sem, int* value) noexcept;
}

namespace {

using namespace PortPS5::Testing;

constexpr int kEoverflow = static_cast<int>(0x80020054u);

// Every entry point rejects a null slot with EINVAL instead of dereferencing
// or throwing (the upstream version threw std::invalid_argument here).
TEST(PthreadSemHandle, NullSlotIsEinvalEverywhere) {
    int value = 0;
    EXPECT_EQ(scePthreadSemInit(nullptr, 0, 1, "n"), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemDestroy(nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemPost(nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemWait(nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemTrywait(nullptr), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemTimedwait(nullptr, 1000), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemGetvalue(nullptr, &value), SCE_KERNEL_ERROR_EINVAL);
}

// A non-zero `flag` (process-shared request) is rejected with EINVAL and leaves
// the slot untouched, matching shadPS4 and SharpEmu; flag 0 is the only mode.
TEST(PthreadSemHandle, NonPrivateFlagIsEinval) {
    PthreadSem sem = nullptr;
    EXPECT_EQ(scePthreadSemInit(&sem, 1, 1, "shared"), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_TRUE(sem == nullptr);
}

// An all-zero slot (never initialised) is not a live semaphore.
TEST(PthreadSemHandle, UninitialisedSlotIsEinval) {
    PthreadSem sem = nullptr;
    int value = 0;
    EXPECT_EQ(scePthreadSemPost(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemTrywait(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_KERNEL_ERROR_EINVAL);
}

// Initial value boundary: SEM_VALUE_MAX (INT_MAX) is the largest legal count,
// one more is EINVAL; post at the maximum reports EOVERFLOW and leaves the
// count unchanged.
TEST(PthreadSemHandle, ValueBoundsAndPostOverflow) {
    PthreadSem tooBig = nullptr;
    EXPECT_EQ(scePthreadSemInit(&tooBig, 0, 0x80000000u, "big"), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(tooBig, nullptr);

    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 0x7FFFFFFFu, "max"), SCE_OK);
    EXPECT_EQ(scePthreadSemPost(&sem), kEoverflow);
    int value = 0;
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 0x7FFFFFFF);
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// getvalue reflects trywait/post; trywait on an empty semaphore is EAGAIN and
// must not consume anything; a null value pointer is EINVAL.
TEST(PthreadSemHandle, TrywaitPostAndGetvalue) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 2, "try"), SCE_OK);
    int value = -1;
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 2);
    EXPECT_EQ(scePthreadSemGetvalue(&sem, nullptr), SCE_KERNEL_ERROR_EINVAL);

    EXPECT_EQ(scePthreadSemTrywait(&sem), SCE_OK);
    EXPECT_EQ(scePthreadSemTrywait(&sem), SCE_OK);
    // POSIX sem_trywait (and FreeBSD) report EAGAIN, not EBUSY; shadPS4 and
    // SharpEmu agree. EBUSY here was an upstream AnyPS5 mistake.
    EXPECT_EQ(scePthreadSemTrywait(&sem), SCE_KERNEL_ERROR_EAGAIN);
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 0);

    EXPECT_EQ(scePthreadSemPost(&sem), SCE_OK);
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(scePthreadSemWait(&sem), SCE_OK);  // token available: no block.
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// Timed wait: times out with ETIMEDOUT after roughly the requested time, takes
// an available token immediately (also with a zero timeout), and a post that
// lands during the wait satisfies it.
TEST(PthreadSemHandle, TimedwaitTimeoutAndSuccess) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 0, "timed"), SCE_OK);

    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(scePthreadSemTimedwait(&sem, 30000), SCE_KERNEL_ERROR_ETIMEDOUT);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(20));

    EXPECT_EQ(scePthreadSemTimedwait(&sem, 0), SCE_KERNEL_ERROR_ETIMEDOUT);

    ASSERT_EQ(scePthreadSemPost(&sem), SCE_OK);
    EXPECT_EQ(scePthreadSemTimedwait(&sem, 0), SCE_OK);

    std::thread poster([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        scePthreadSemPost(&sem);
    });
    EXPECT_EQ(scePthreadSemTimedwait(&sem, 5u * 1000u * 1000u), SCE_OK);
    poster.join();
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// A blocking wait really blocks until a post arrives.
TEST(PthreadSemHandle, WaitBlocksUntilPost) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 0, "block"), SCE_OK);
    std::atomic<bool> done{false};
    std::thread waiter([&] {
        EXPECT_EQ(scePthreadSemWait(&sem), SCE_OK);
        done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(done.load(std::memory_order_acquire));
    ASSERT_EQ(scePthreadSemPost(&sem), SCE_OK);
    waiter.join();
    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// Lifecycle: destroy frees once; every later use, including a second destroy,
// is EINVAL (the slot holds the destroyed marker, never a dangling pointer);
// a slot can be re-initialised after destroy.
TEST(PthreadSemHandle, DestroyThenUseIsEinval) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 1, "life"), SCE_OK);
    ASSERT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
    int value = 0;
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemPost(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemWait(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemTrywait(&sem), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemTimedwait(&sem, 1000), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_KERNEL_ERROR_EINVAL);

    ASSERT_EQ(scePthreadSemInit(&sem, 0, 3, "again"), SCE_OK);
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 3);
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// Destroy with a sleeping waiter is the detectable misuse: EBUSY, and the
// waiter is unaffected (it still completes once a token is posted). The test
// polls the waiter counter (SemBarrierTypes.hpp) so destroy is only attempted
// once the thread is provably parked, which keeps the test deterministic.
TEST(PthreadSemHandle, DestroyWithWaiterIsEbusy) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 0, "busy"), SCE_OK);
    std::atomic<int> result{-1};
    std::thread waiter([&] { result.store(scePthreadSemWait(&sem)); });

    while (sem->waiters.load(std::memory_order_acquire) == 0) {
        std::this_thread::yield();
    }
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_KERNEL_ERROR_EBUSY);
    ASSERT_EQ(scePthreadSemPost(&sem), SCE_OK);
    waiter.join();
    EXPECT_EQ(result.load(), SCE_OK);
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

// Stress / conservation: several consumers and producers exchange a fixed
// number of tokens. Every posted token must be consumed exactly once (no lost
// wakeup leaves a consumer asleep, no double take lets one through twice).
TEST(PthreadSemHandle, TokenConservationUnderContention) {
    PthreadSem sem = nullptr;
    ASSERT_EQ(scePthreadSemInit(&sem, 0, 0, "stress"), SCE_OK);
    constexpr int kProducers = 3;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 2000;
    constexpr int kTotal = kProducers * kPerProducer;
    std::atomic<int> consumed{0};

    std::vector<std::thread> threads;
    for (int c = 0; c < kConsumers; ++c) {
        threads.emplace_back([&] {
            while (consumed.load(std::memory_order_acquire) < kTotal) {
                // Timed so consumers exit once the total is reached.
                if (scePthreadSemTimedwait(&sem, 20000) == SCE_OK) {
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }
    for (int p = 0; p < kProducers; ++p) {
        threads.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                EXPECT_EQ(scePthreadSemPost(&sem), SCE_OK);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(consumed.load(), kTotal);
    int value = -1;
    ASSERT_EQ(scePthreadSemGetvalue(&sem, &value), SCE_OK);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(scePthreadSemDestroy(&sem), SCE_OK);
}

}  // namespace
