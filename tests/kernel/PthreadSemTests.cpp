#include "common/TestHarness.hpp"
#include "prx/libkernel/Semaphore/include/Semaphore.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using namespace PortPS5::Testing;

// Verifies semaphore token initialization, poll (trywait), signal (post), and wait.
TEST(PthreadSem, InitAndTokenAcquisition) {
    KernelSema sem = nullptr;
    ASSERT_EQ(sceKernelCreateSema(&sem, "test_sem", 0, 2, 5, nullptr), KERNEL_SEMA_OK);
    ASSERT_NE(sem, nullptr);

    // Initial tokens: 2. Poll 1 -> 1 left.
    EXPECT_EQ(sceKernelPollSema(sem, 1), KERNEL_SEMA_OK);
    // Poll 1 -> 0 left.
    EXPECT_EQ(sceKernelPollSema(sem, 1), KERNEL_SEMA_OK);
    // Poll 1 when empty returns KERNEL_SEMA_ERROR_EBUSY.
    EXPECT_EQ(sceKernelPollSema(sem, 1), KERNEL_SEMA_ERROR_EBUSY);

    // Signal 2 tokens
    EXPECT_EQ(sceKernelSignalSema(sem, 2), KERNEL_SEMA_OK);
    // Wait for 2 tokens succeeds immediately
    EXPECT_EQ(sceKernelWaitSema(sem, 2, nullptr), KERNEL_SEMA_OK);
    // Verified empty
    EXPECT_EQ(sceKernelPollSema(sem, 1), KERNEL_SEMA_ERROR_EBUSY);

    delete sem;
}

// Verifies that sceKernelWaitSema returns timeout error code when tokens are not signaled.
TEST(PthreadSem, TimedwaitExpires) {
    KernelSema sem = nullptr;
    ASSERT_EQ(sceKernelCreateSema(&sem, "timed_sem", 0, 0, 5, nullptr), KERNEL_SEMA_OK);

    KernelUseconds timeout = 40000; // 40 ms
    auto start = std::chrono::steady_clock::now();
    int res = sceKernelWaitSema(sem, 1, &timeout);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(25));
    EXPECT_EQ(res, KERNEL_SEMA_ERROR_ETIMEDOUT);

    delete sem;
}

// Verifies multi-threaded producer-consumer token passing over 50 iterations without token loss.
TEST(PthreadSem, ProducerConsumerSignaling) {
    KernelSema sem = nullptr;
    // Set maxCount to 100 to accommodate any scheduling jitter where producer outpaces consumer
    ASSERT_EQ(sceKernelCreateSema(&sem, "pc_sem", 0, 0, 100, nullptr), KERNEL_SEMA_OK);

    constexpr int ITERATIONS = 50;
    std::atomic<int> consumed{0};

    std::thread consumer([&] {
        for (int i = 0; i < ITERATIONS; ++i) {
            EXPECT_EQ(sceKernelWaitSema(sem, 1, nullptr), KERNEL_SEMA_OK);
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < ITERATIONS; ++i) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        EXPECT_EQ(sceKernelSignalSema(sem, 1), KERNEL_SEMA_OK);
    }

    consumer.join();
    EXPECT_EQ(consumed.load(std::memory_order_relaxed), ITERATIONS);

    delete sem;
}

// Verifies that signalling tokens beyond the configured maxCount returns KERNEL_SEMA_ERROR_EINVAL.
TEST(PthreadSem, ClampingMaxTokens) {
    KernelSema sem = nullptr;
    ASSERT_EQ(sceKernelCreateSema(&sem, "clamp_sem", 0, 3, 3, nullptr), KERNEL_SEMA_OK);

    // Signalling beyond maxCount must be rejected
    EXPECT_EQ(sceKernelSignalSema(sem, 1), KERNEL_SEMA_ERROR_EINVAL);

    delete sem;
}

// Verifies that a timed wait with zero timeout on an empty semaphore returns timeout without blocking.
TEST(PthreadSem, ZeroWaitReturnsImmediately) {
    KernelSema sem = nullptr;
    ASSERT_EQ(sceKernelCreateSema(&sem, "zero_wait_sem", 0, 0, 10, nullptr), KERNEL_SEMA_OK);

    KernelUseconds zero_timeout = 0;
    auto start = std::chrono::steady_clock::now();
    int res = sceKernelWaitSema(sem, 1, &zero_timeout);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
    EXPECT_EQ(res, KERNEL_SEMA_ERROR_ETIMEDOUT);

    delete sem;
}

} // namespace
