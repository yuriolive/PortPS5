// tests/kernel/PthreadBarrierOnceTests.cpp
// Verification for pthread_barrier_*, pthread_equal, sched_yield
// (AnyPS5 5e708c16) and scePthreadOnce / pthread_once (AnyPS5 e764385d),
// all futex-adapted in PortPS5. No GPU and no game data needed.

#include "common/TestHarness.hpp"
#include "SceTypes.hpp"
#include "prx/libkernel/Pthread/include/SemBarrierTypes.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI pthread_barrier_init_nid_postfix(PthreadBarrierPrivate** barrier, const void* attr, unsigned count) noexcept;
int APS5_VABI pthread_barrier_wait_nid_postfix(PthreadBarrierPrivate** barrier) noexcept;
int APS5_VABI pthread_barrier_destroy_nid_postfix(PthreadBarrierPrivate** barrier) noexcept;
int APS5_VABI pthread_equal_nid_postfix(Pthread first, Pthread second) noexcept;
int APS5_VABI sched_yield_nid_postfix(void) noexcept;
Pthread APS5_VABI scePthreadSelf() noexcept;
int APS5_VABI scePthreadOnce(std::int32_t* once, void(APS5_VABI* initialize)());
int APS5_VABI pthread_once_nid_postfix(void* control, void(APS5_VABI* initialize)());
}

namespace {

using namespace PortPS5::Testing;

constexpr int kPosixEinval = 22;
constexpr int kPosixEbusy = 16;
constexpr int kSerial = -1;  // PTHREAD_BARRIER_SERIAL_THREAD on FreeBSD.

// init rejects a null slot and a zero count with EINVAL; a non-null attr is
// accepted (upstream aborted on it); operations on a destroyed or
// never-initialised barrier are EINVAL, including a second destroy.
TEST(PthreadBarrier, ArgumentValidationAndLifecycle) {
    PthreadBarrierPrivate* barrier = nullptr;
    EXPECT_EQ(pthread_barrier_init_nid_postfix(nullptr, nullptr, 2), kPosixEinval);
    EXPECT_EQ(pthread_barrier_init_nid_postfix(&barrier, nullptr, 0), kPosixEinval);
    EXPECT_TRUE(barrier == nullptr);
    EXPECT_EQ(pthread_barrier_wait_nid_postfix(&barrier), kPosixEinval);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), kPosixEinval);
    EXPECT_EQ(pthread_barrier_wait_nid_postfix(nullptr), kPosixEinval);

    const std::uint64_t fakeAttr = 0;
    ASSERT_EQ(pthread_barrier_init_nid_postfix(&barrier, &fakeAttr, 3), 0);
    EXPECT_TRUE(barrier != nullptr);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), 0);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), kPosixEinval);
    EXPECT_EQ(pthread_barrier_wait_nid_postfix(&barrier), kPosixEinval);
}

// A barrier with count 1 releases its only arrival immediately as the serial
// thread, repeatedly (the round counter must advance cleanly).
TEST(PthreadBarrier, CountOneIsAlwaysSerial) {
    PthreadBarrierPrivate* barrier = nullptr;
    ASSERT_EQ(pthread_barrier_init_nid_postfix(&barrier, nullptr, 1), 0);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(pthread_barrier_wait_nid_postfix(&barrier), kSerial);
    }
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), 0);
}

// Multi-round rendezvous: with N threads and many rounds, (a) nobody leaves a
// round before all N arrived (phase counter check), and (b) exactly one thread
// per round gets PTHREAD_BARRIER_SERIAL_THREAD.
TEST(PthreadBarrier, RoundsReleaseTogetherWithOneSerialThread) {
    constexpr unsigned kThreads = 4;
    constexpr int kRounds = 200;
    PthreadBarrierPrivate* barrier = nullptr;
    ASSERT_EQ(pthread_barrier_init_nid_postfix(&barrier, nullptr, kThreads), 0);

    std::vector<std::atomic<unsigned>> arrivedInRound(kRounds);
    std::atomic<int> serialCount{0};
    std::atomic<int> earlyRelease{0};
    std::atomic<int> badReturn{0};

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int round = 0; round < kRounds; ++round) {
                arrivedInRound[round].fetch_add(1, std::memory_order_acq_rel);
                const int rc = pthread_barrier_wait_nid_postfix(&barrier);
                if (rc == kSerial) {
                    serialCount.fetch_add(1, std::memory_order_relaxed);
                } else if (rc != 0) {
                    badReturn.fetch_add(1, std::memory_order_relaxed);
                }
                // Released only once every thread of this round has arrived.
                if (arrivedInRound[round].load(std::memory_order_acquire) != kThreads) {
                    earlyRelease.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(serialCount.load(), kRounds);
    EXPECT_EQ(earlyRelease.load(), 0);
    EXPECT_EQ(badReturn.load(), 0);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), 0);
}

// Regression for the generation/arrival packing: when more threads than
// `count` call wait in the same window, the extra arrivals must be counted for
// the NEXT round (not dropped by a counter reset and not released early).
// count=2 with 6 callers must complete exactly 3 rounds, i.e. 3 serial returns,
// and every caller must return (a dropped arrival would hang the test).
TEST(PthreadBarrier, ExtraArrivalsLandInNextRound) {
    PthreadBarrierPrivate* barrier = nullptr;
    ASSERT_EQ(pthread_barrier_init_nid_postfix(&barrier, nullptr, 2), 0);
    std::atomic<int> serialCount{0};
    std::atomic<int> returned{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back([&] {
            if (pthread_barrier_wait_nid_postfix(&barrier) == kSerial) {
                serialCount.fetch_add(1);
            }
            returned.fetch_add(1);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(returned.load(), 6);
    EXPECT_EQ(serialCount.load(), 3);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), 0);
}

// Destroy while a thread is parked in wait is EBUSY and leaves the barrier
// usable; after the round completes destroy succeeds.
TEST(PthreadBarrier, DestroyWithWaiterIsEbusy) {
    PthreadBarrierPrivate* barrier = nullptr;
    ASSERT_EQ(pthread_barrier_init_nid_postfix(&barrier, nullptr, 2), 0);
    std::atomic<int> waiterResult{99};
    std::thread waiter([&] { waiterResult.store(pthread_barrier_wait_nid_postfix(&barrier)); });

    // Destroy only once the waiter is provably parked (active counter), so the
    // EBUSY expectation cannot race thread start-up.
    while (barrier->active.load(std::memory_order_acquire) == 0) {
        std::this_thread::yield();
    }
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), kPosixEbusy);

    const int mainResult = pthread_barrier_wait_nid_postfix(&barrier);
    waiter.join();
    // Exactly one of the two arrivals is the serial thread.
    EXPECT_EQ((mainResult == kSerial) + (waiterResult.load() == kSerial), 1);
    EXPECT_EQ(pthread_barrier_destroy_nid_postfix(&barrier), 0);
}

// pthread_equal compares identity, including across threads, and sched_yield
// always succeeds.
TEST(PthreadThreadHelpers, EqualAndYield) {
    const Pthread mainSelf = scePthreadSelf();
    EXPECT_NE(pthread_equal_nid_postfix(mainSelf, mainSelf), 0);
    Pthread other = nullptr;
    std::thread worker([&] { other = scePthreadSelf(); });
    worker.join();
    EXPECT_EQ(pthread_equal_nid_postfix(mainSelf, other), 0);
    EXPECT_NE(pthread_equal_nid_postfix(nullptr, nullptr), 0);
    EXPECT_EQ(sched_yield_nid_postfix(), 0);
}

// ---------------------------------------------------------------------------
// scePthreadOnce / pthread_once
// ---------------------------------------------------------------------------

std::atomic<int> g_onceCalls{0};
std::atomic<int> g_onceSideEffect{0};

// Delay helper kept out of the System V initializer below: the MinGW GCC 15.2
// toolchain hits an internal compiler error (choose_baseaddr during prologue
// generation) when std::this_thread::sleep_for is inlined into an APS5_VABI
// function of this TU.
[[gnu::noinline]] void SlowDown() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void APS5_VABI CountingInit() {
    g_onceCalls.fetch_add(1);
    // Slow enough that concurrent callers must wait for completion.
    SlowDown();
    g_onceSideEffect.store(42, std::memory_order_release);
}

// The once implementation exists (the tree previously shipped only an aborting
// NotImplemented stub under the postfixed name) and rejects bad arguments with
// SCE EINVAL codes, never a throw: null control, null routine, corrupt state.
TEST(PthreadOnce, ScePthreadOnceReturnCodes) {
    std::int32_t control = 0;
    EXPECT_EQ(scePthreadOnce(nullptr, CountingInit), SCE_KERNEL_ERROR_EINVAL);
    EXPECT_EQ(scePthreadOnce(&control, nullptr), SCE_KERNEL_ERROR_EINVAL);
    std::int32_t corrupt = 0x1234;
    EXPECT_EQ(scePthreadOnce(&corrupt, CountingInit), SCE_KERNEL_ERROR_EINVAL);

    g_onceCalls.store(0);
    EXPECT_EQ(scePthreadOnce(&control, CountingInit), SCE_OK);
    EXPECT_EQ(scePthreadOnce(&control, CountingInit), SCE_OK);
    EXPECT_EQ(g_onceCalls.load(), 1);
    EXPECT_EQ(control, 1);
}

// 8 threads race on one control block: the initializer runs exactly once and
// every caller, including those that lost the claim, returns only after the
// initializer's writes are visible.
TEST(PthreadOnce, RunsOnceAndPublishesToWaiters) {
    std::int32_t control = 0;
    g_onceCalls.store(0);
    g_onceSideEffect.store(0);
    std::atomic<int> sawStale{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            EXPECT_EQ(scePthreadOnce(&control, CountingInit), SCE_OK);
            if (g_onceSideEffect.load(std::memory_order_acquire) != 42) {
                sawStale.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(g_onceCalls.load(), 1);
    EXPECT_EQ(sawStale.load(), 0);
}

// pthread_once shares the futex implementation and keeps its POSIX codes:
// EINVAL (22) for bad arguments or a corrupt state word, 0 otherwise.
TEST(PthreadOnce, PosixOnceSharesImplementation) {
    struct Control {
        std::int32_t state;
        void* mutex;
    } control{0, nullptr};
    g_onceCalls.store(0);
    EXPECT_EQ(pthread_once_nid_postfix(&control, CountingInit), 0);
    EXPECT_EQ(pthread_once_nid_postfix(&control, CountingInit), 0);
    EXPECT_EQ(g_onceCalls.load(), 1);
    EXPECT_EQ(pthread_once_nid_postfix(nullptr, CountingInit), kPosixEinval);
    EXPECT_EQ(pthread_once_nid_postfix(&control, nullptr), kPosixEinval);
    Control corrupt{99, nullptr};
    EXPECT_EQ(pthread_once_nid_postfix(&corrupt, CountingInit), kPosixEinval);
}

// Note: the "initializer unwinds, state resets to 0, next caller retries"
// contract is covered by the legacy guest_once ctest (core/libs/tests/
// GuestOnce.cpp), which runs against this same RunOnce implementation. It is
// not repeated here because a C++ exception thrown from a System V guest frame
// only unwinds correctly in the standalone guest_* executables, whose unwinder
// registration matches libc.prx; under gtest_main the throw segfaults
// (verified while porting).

}  // namespace
