// tests/kernel/PthreadMutexTests.cpp
// Verification test suite for POSIX pthread mutex synchronization in PortPS5 libkernel.
// Verifies mutual exclusion invariants, recursive locking attributes, error-checking semantics, and try-lock.

#include "common/TestHarness.hpp"
#include "prx/libkernel/Pthread/include/Mutex.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

using namespace PortPS5::Testing;

// Verifies default mutex initialization, basic mutual exclusion lock/unlock, and destruction.
TEST(PthreadMutex, DefaultInitAndLockUnlock) {
    PthreadMutex mutex = nullptr;
    EXPECT_EQ(scePthreadMutexInit(&mutex, nullptr, "test_default"), 0);
    ASSERT_NE(mutex, nullptr);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies trylock semantics: returns EBUSY when locked by another thread, succeeds when unlocked.
TEST(PthreadMutex, TrylockSemantics) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "test_trylock"), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);

    std::atomic<int> trylock_result{-1};
    // From a separate thread, trylock must detect that the mutex is already locked
    std::thread other([&] {
        trylock_result.store(scePthreadMutexTrylock(&mutex), std::memory_order_release);
    });
    other.join();

    // Must return SCE_KERNEL_ERROR_EBUSY (0x80020010)
    EXPECT_EQ(trylock_result.load(std::memory_order_acquire), SCE_KERNEL_ERROR_EBUSY);

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    // After unlocking, trylock from another thread must succeed
    std::thread other2([&] {
        int r = scePthreadMutexTrylock(&mutex);
        if (r == 0) {
            scePthreadMutexUnlock(&mutex);
        }
        trylock_result.store(r, std::memory_order_release);
    });
    other2.join();

    EXPECT_EQ(trylock_result.load(std::memory_order_acquire), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies recursive mutex attribute allows nested locks by the same owner, requiring matching unlocks.
TEST(PthreadMutex, RecursiveLocking) {
    PthreadMutexattr attr = nullptr;
    ASSERT_EQ(scePthreadMutexattrInit(&attr), 0);
    ASSERT_EQ(scePthreadMutexattrSettype(&attr, 2), 0); // Type 2 = Recursive

    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, &attr, "test_recursive"), 0);
    ASSERT_EQ(scePthreadMutexattrDestroy(&attr), 0);

    // Acquire lock 5 times recursively
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    }

    // While held recursively, another thread cannot trylock
    std::atomic<int> other_trylock{-1};
    std::thread other([&] {
        other_trylock.store(scePthreadMutexTrylock(&mutex), std::memory_order_release);
    });
    other.join();
    EXPECT_EQ(other_trylock.load(std::memory_order_acquire), SCE_KERNEL_ERROR_EBUSY);

    // Release lock 5 times
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    }

    // Now that all 5 recursive acquisitions are unlocked, another thread can acquire the mutex
    std::thread other2([&] {
        int r = scePthreadMutexTrylock(&mutex);
        if (r == 0) {
            scePthreadMutexUnlock(&mutex);
        }
        other_trylock.store(r, std::memory_order_release);
    });
    other2.join();
    EXPECT_EQ(other_trylock.load(std::memory_order_acquire), 0);

    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies non-recursive mutex trylock returns EBUSY when self-locked to prevent deadlock.
TEST(PthreadMutex, ErrorCheckDeadlockDetection) {
    PthreadMutexattr attr = nullptr;
    ASSERT_EQ(scePthreadMutexattrInit(&attr), 0);
    ASSERT_EQ(scePthreadMutexattrSettype(&attr, 1), 0); // Type 1 = ErrorCheck

    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, &attr, "test_errorcheck"), 0);
    ASSERT_EQ(scePthreadMutexattrDestroy(&attr), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);

    // Calling trylock while already owning the mutex returns SCE_KERNEL_ERROR_EBUSY
    EXPECT_EQ(scePthreadMutexTrylock(&mutex), SCE_KERNEL_ERROR_EBUSY);

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that timedlock expires with SCE_TIMEDOUT when the mutex remains held.
TEST(PthreadMutex, TimedlockTimeout) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "test_timedlock"), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);

    std::atomic<int> timedlock_result{-1};
    auto start = std::chrono::steady_clock::now();

    std::thread other([&] {
        // Attempt timedlock with 50,000 µs (50 ms) timeout
        timedlock_result.store(scePthreadMutexTimedlock(&mutex, 50000), std::memory_order_release);
    });
    other.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(30));
    // SCE timeout error is 0x8002003C
    EXPECT_EQ(timedlock_result.load(std::memory_order_acquire), static_cast<int>(0x8002003Cu));

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Stress test: 16 concurrent threads contending for a single mutex, ensuring zero data races.
TEST(PthreadMutex, ConcurrentContention) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "test_concurrent"), 0);

    constexpr int NUM_THREADS = 16;
    constexpr int ITERATIONS = 500;
    int shared_counter = 0;

    std::array<std::thread, NUM_THREADS> workers;
    for (auto& worker : workers) {
        worker = std::thread([&] {
            for (int i = 0; i < ITERATIONS; ++i) {
                EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
                ++shared_counter;
                EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    // Verify counter matches exactly total expected increments across all threads
    EXPECT_EQ(shared_counter, NUM_THREADS * ITERATIONS);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that after a mutex is unlocked, subsequent acquisitions from different threads succeed cleanly.
TEST(PthreadMutex, HandOffToSecondThread) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "handoff_test"), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    std::atomic<bool> acquired{false};
    std::thread worker([&] {
        if (scePthreadMutexLock(&mutex) == 0) {
            acquired.store(true, std::memory_order_release);
            EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
        }
    });
    worker.join();

    EXPECT_TRUE(acquired.load(std::memory_order_acquire));
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

} // namespace
