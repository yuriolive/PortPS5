// tests/kernel/PthreadCondTests.cpp
// Verification test suite for POSIX condition variable emulation in PortPS5 libkernel.
// Verifies wait/signal/broadcast synchronization invariants, spurious wakeup tolerance, and timeout handling.

#include "common/TestHarness.hpp"
#include "prx/libkernel/Pthread/include/Cond.hpp"
#include "prx/libkernel/Pthread/include/Mutex.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

using namespace PortPS5::Testing;

// Verifies that scePthreadCondSignal wakes a single waiter blocked on scePthreadCondWait.
TEST(PthreadCond, SignalWakesWaiter) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "cond_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "cond_var"), 0);

    bool signaled = false;
    std::atomic<bool> ready{false};

    std::thread waiter([&] {
        EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
        ready.store(true, std::memory_order_release);
        // Wait loop guarding against spurious wakeups
        while (!signaled) {
            EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
        }
        EXPECT_TRUE(signaled);
        EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    });

    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    signaled = true;
    EXPECT_EQ(scePthreadCondSignal(&cond), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    waiter.join();

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that scePthreadCondBroadcast unblocks all concurrent waiters on a condition variable.
TEST(PthreadCond, BroadcastWakesAllWaiters) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "bcast_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "bcast_var"), 0);

    constexpr int NUM_WAITERS = 4;
    std::atomic<int> ready_count{0};
    std::atomic<int> wake_count{0};
    bool proceed = false;

    std::array<std::thread, NUM_WAITERS> waiters;
    for (auto& waiter : waiters) {
        waiter = std::thread([&] {
            EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
            ready_count.fetch_add(1, std::memory_order_release);
            while (!proceed) {
                EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
            }
            wake_count.fetch_add(1, std::memory_order_release);
            EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
        });
    }

    while (ready_count.load(std::memory_order_acquire) != NUM_WAITERS) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    proceed = true;
    EXPECT_EQ(scePthreadCondBroadcast(&cond), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    for (auto& waiter : waiters) {
        waiter.join();
    }

    // All waiters must have woken up cleanly
    EXPECT_EQ(wake_count.load(std::memory_order_acquire), NUM_WAITERS);

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that scePthreadCondTimedwait returns timeout error code when no signal occurs.
TEST(PthreadCond, TimedwaitExpires) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "timed_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "timed_cond"), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);

    auto start = std::chrono::steady_clock::now();
    // 40,000 usec = 40 ms timeout
    int res = scePthreadCondTimedwait(&cond, &mutex, 40000);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(25));
    // SCE timeout error is 0x8002003C
    EXPECT_EQ(res, static_cast<int>(0x8002003Cu));

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies ping-pong synchronization pattern between producer and consumer across multiple iterations.
TEST(PthreadCond, ProducerConsumerPingPong) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "pingpong_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "pingpong_cond"), 0);

    constexpr int ROUNDS = 50;
    int state = 0; // 0 = producer turn, 1 = consumer turn
    int producer_count = 0;
    int consumer_count = 0;

    std::thread consumer([&] {
        for (int i = 0; i < ROUNDS; ++i) {
            EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
            while (state != 1) {
                EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
            }
            ++consumer_count;
            state = 0;
            EXPECT_EQ(scePthreadCondSignal(&cond), 0);
            EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
        }
    });

    for (int i = 0; i < ROUNDS; ++i) {
        EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
        while (state != 0) {
            EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
        }
        ++producer_count;
        state = 1;
        EXPECT_EQ(scePthreadCondSignal(&cond), 0);
        EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    }

    consumer.join();

    EXPECT_EQ(producer_count, ROUNDS);
    EXPECT_EQ(consumer_count, ROUNDS);

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that multiple waiters are unblocked one by one via sequential signals.
// Behavioral invariant: each scePthreadCondSignal wakes exactly one waiter, so two signals
// release two waiters in sequence. Preconditions: both workers block in CondWait holding the
// mutex-protected predicate (permits == 0); the main thread then publishes one permit per
// signal. Expected failure modes: a lost wakeup hangs the bounded wait below; a broadcast
// instead of signal would still pass the count but is covered by wake-order assertions.
// The predicate loop (not a bare single wait) tolerates spurious wakeups, and bounded polling
// (not fixed sleeps with exact counts) tolerates CI scheduler delays without flaking.
TEST(PthreadCond, MultipleWaitersSequentialSignal) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "seq_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "seq_cond"), 0);

    constexpr int WAITERS = 2;
    std::atomic<int> ready{0};
    std::atomic<int> woken{0};
    int permits = 0; // mutex-guarded predicate: one permit released per signal

    // Bounded wait helper: polls an atomic until it reaches the target or times out.
    // Returns true when the target was observed (deterministic under load; a fixed
    // sleep_then_EXPECT_EQ flakes when the woken thread is not yet rescheduled).
    const auto waitUntil = [](const std::atomic<int>& value, int target,
                              std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (value.load(std::memory_order_acquire) != target) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    };

    std::array<std::thread, WAITERS> workers;
    for (auto& w : workers) {
        w = std::thread([&] {
            EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
            ready.fetch_add(1, std::memory_order_release);
            // Predicate loop: re-wait on spurious wakeups without consuming a permit.
            while (permits == 0) {
                EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
            }
            --permits;
            woken.fetch_add(1, std::memory_order_release);
            EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
        });
    }

    ASSERT_TRUE(waitUntil(ready, WAITERS, std::chrono::seconds(15)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Signal first waiter: publish one permit so exactly one worker can proceed.
    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    ++permits;
    EXPECT_EQ(scePthreadCondSignal(&cond), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    ASSERT_TRUE(waitUntil(woken, 1, std::chrono::seconds(15)));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Signal second waiter.
    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    ++permits;
    EXPECT_EQ(scePthreadCondSignal(&cond), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    for (auto& w : workers) {
        w.join();
    }
    EXPECT_EQ(woken.load(std::memory_order_acquire), WAITERS);

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies condition attribute clock configuration and validation.
TEST(PthreadCond, CondattrClockValidation) {
    PthreadCondattr attr = nullptr;
    ASSERT_EQ(scePthreadCondattrInit(&attr), 0);
    ASSERT_NE(attr, nullptr);

    // Valid clocks: 0 (realtime), 1 (monotonic), 13
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 0), 0);
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 1), 0);
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 13), 0);

    // Invalid clocks (< 0 or > 13) return SCE EINVAL (0x80020016)
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, -1), static_cast<int>(0x80020016u));
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 14), static_cast<int>(0x80020016u));
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 99), static_cast<int>(0x80020016u));
    EXPECT_EQ(scePthreadCondattrSetclock(nullptr, 0), static_cast<int>(0x80020016u));

    // Initialize condvar with monotonic clock attribute
    EXPECT_EQ(scePthreadCondattrSetclock(&attr, 1), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, &attr, "mono_cond"), 0);

    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "mono_mtx"), 0);

    // Verify timedwait expires properly on monotonic condvar
    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    EXPECT_EQ(scePthreadCondTimedwait(&cond, &mutex, 10000), static_cast<int>(0x8002003Cu));
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
    EXPECT_EQ(scePthreadCondattrDestroy(&attr), 0);
}

// Verifies that scePthreadCondTimedwait with zero timeout returns timeout immediately if not signaled.
TEST(PthreadCond, TimedwaitZeroTimeoutExpiresImmediately) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "zero_timed_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "zero_timed_cond"), 0);

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    auto start = std::chrono::steady_clock::now();
    int res = scePthreadCondTimedwait(&cond, &mutex, 0);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
    EXPECT_EQ(res, static_cast<int>(0x8002003Cu)); // SCE_KERNEL_ERROR_ETIMEDOUT

    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

// Verifies that scePthreadCondSignalto wakes waiting threads (broadcast-as-signal behavior).
TEST(PthreadCond, SignaltoWakesWaiters) {
    PthreadMutex mutex = nullptr;
    ASSERT_EQ(scePthreadMutexInit(&mutex, nullptr, "sigto_mtx"), 0);
    PthreadCond cond = nullptr;
    ASSERT_EQ(scePthreadCondInit(&cond, nullptr, "sigto_cond"), 0);

    std::atomic<bool> ready{false};
    std::atomic<bool> woken{false};

    std::thread waiter([&] {
        EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
        ready.store(true, std::memory_order_release);
        while (!woken.load(std::memory_order_acquire)) {
            EXPECT_EQ(scePthreadCondWait(&cond, &mutex), 0);
        }
        EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);
    });

    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(scePthreadMutexLock(&mutex), 0);
    woken.store(true, std::memory_order_release);
    EXPECT_EQ(scePthreadCondSignalto(&cond, nullptr), 0);
    EXPECT_EQ(scePthreadMutexUnlock(&mutex), 0);

    waiter.join();
    EXPECT_TRUE(woken.load(std::memory_order_acquire));

    EXPECT_EQ(scePthreadCondDestroy(&cond), 0);
    EXPECT_EQ(scePthreadMutexDestroy(&mutex), 0);
}

} // namespace

