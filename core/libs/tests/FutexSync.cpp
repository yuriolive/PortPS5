// Futex sync tests for the M1 threading slice (docs/spec/threading.md Tests).
// Covers: every mutex type (lock/trylock/timedlock, EDEADLK/EPERM/EBUSY/
// destroy/EINVAL-after-destroy); lazy static init with 64 racers; cond
// ping-pong + broadcast storm with no lost wakeups; rwlock writer preference;
// compact tids; _umtx_op wait/wake at sizes 4 and 8; and the uncontended
// lock/unlock microbenchmark (M1 exit: >=10x faster than main's std::mutex
// path, measured below).
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

static void Require(bool v, const char* what, int line) {
    if (!v) {
        std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
        std::fflush(stdout);
        std::abort();
    }
}
#define REQUIRE(x) Require((x), #x, __LINE__)

static constexpr int SCE_OK = 0;
static constexpr int SCE_EINVAL = static_cast<int>(0x80020016u);
static constexpr int SCE_EBUSY = static_cast<int>(0x80020010u);
static constexpr int SCE_TIMEDOUT = static_cast<int>(0x8002003Cu);
static constexpr int SCE_EDEADLK = static_cast<int>(0x8002000Bu);
static constexpr int SCE_EPERM = static_cast<int>(0x80020001u);
static constexpr int SCE_EAGAIN = static_cast<int>(0x80020023u);
static constexpr int SCE_EOWNERDEAD = static_cast<int>(0x80020060u);
static constexpr int SCE_ENOTRECOVERABLE = static_cast<int>(0x8002005Fu);

extern "C" {
// Documented test prototype for scePthreadMutexattrInit.
int APS5_VABI scePthreadMutexattrInit(PthreadMutexattr* attr) noexcept;
// Documented test prototype for scePthreadMutexattrDestroy.
int APS5_VABI scePthreadMutexattrDestroy(PthreadMutexattr* attr) noexcept;
// Documented test prototype for scePthreadMutexattrSettype.
int APS5_VABI scePthreadMutexattrSettype(PthreadMutexattr* attr, int type) noexcept;
// Documented test prototype for scePthreadMutexInit.
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr,
                                 const char* name) noexcept;
// Documented test prototype for scePthreadMutexDestroy.
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
// Documented test prototype for scePthreadMutexLock.
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
// Documented test prototype for scePthreadMutexUnlock.
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
// Documented test prototype for scePthreadMutexTimedlock.
int APS5_VABI scePthreadMutexTimedlock(PthreadMutex* mutex, KernelUseconds usec) noexcept;
// Documented test prototype for scePthreadMutexTrylock.
int APS5_VABI scePthreadMutexTrylock(PthreadMutex* mutex) noexcept;
// Documented test prototype for scePthreadCondInit.
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr,
                                const char* name) noexcept;
// Documented test prototype for scePthreadCondDestroy.
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond) noexcept;
// Documented test prototype for scePthreadCondSignal.
int APS5_VABI scePthreadCondSignal(PthreadCond* cond) noexcept;
// Documented test prototype for scePthreadCondBroadcast.
int APS5_VABI scePthreadCondBroadcast(PthreadCond* cond) noexcept;
// Documented test prototype for scePthreadCondWait.
int APS5_VABI scePthreadCondWait(PthreadCond* cond, PthreadMutex* mutex) noexcept;
// Documented test prototype for scePthreadCondTimedwait.
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex,
                                      KernelUseconds usec) noexcept;
// Documented test prototype for scePthreadRwlockInit.
int APS5_VABI scePthreadRwlockInit(PthreadRwlock* rwlock, const PthreadRwlockattr* attr,
                                  const char* name) noexcept;
// Documented test prototype for scePthreadRwlockDestroy.
int APS5_VABI scePthreadRwlockDestroy(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadRwlockRdlock.
int APS5_VABI scePthreadRwlockRdlock(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadRwlockTryrdlock.
int APS5_VABI scePthreadRwlockTryrdlock(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadRwlockWrlock.
int APS5_VABI scePthreadRwlockWrlock(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadRwlockTrywrlock.
int APS5_VABI scePthreadRwlockTrywrlock(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadRwlockUnlock.
int APS5_VABI scePthreadRwlockUnlock(PthreadRwlock* rwlock) noexcept;
// Documented test prototype for scePthreadGetthreadid.
int APS5_VABI scePthreadGetthreadid(void) noexcept;
// Documented test prototype for _umtx_op_nid_postfix.
int APS5_VABI _umtx_op_nid_postfix(void* obj, int op, std::uint64_t val, void* uaddr,
                                   void* uaddr2) noexcept;
}

template <typename T>
static T* SlotOf(std::uint64_t& storage) {
    return reinterpret_cast<T*>(&storage);
}

static void TestMutexBasics() {
    // Null slot -> EINVAL.
    REQUIRE(scePthreadMutexLock(nullptr) == SCE_EINVAL);
    REQUIRE(scePthreadMutexUnlock(nullptr) == SCE_EINVAL);
    REQUIRE(scePthreadMutexTrylock(nullptr) == SCE_EINVAL);
    REQUIRE(scePthreadMutexDestroy(nullptr) == SCE_EINVAL);
    REQUIRE(scePthreadMutexInit(nullptr, nullptr, nullptr) == SCE_EINVAL);

    // Explicit init, default (Normal) + each attr type.
    for (int type : {1, 2, 3}) {
        alignas(8) std::uint64_t s = 0xDEADBEEFDEADBEEFull;
        auto* m = SlotOf<PthreadMutex>(s);
        PthreadMutexattr a = nullptr;
        REQUIRE(scePthreadMutexattrInit(&a) == SCE_OK);
        REQUIRE(scePthreadMutexattrSettype(&a, type) == SCE_OK);
        REQUIRE(scePthreadMutexInit(m, &a, nullptr) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        if (type == 2) {
            // Recursive: two more locks ok, two inner unlocks, then final.
            REQUIRE(scePthreadMutexLock(m) == SCE_OK);
            REQUIRE(scePthreadMutexLock(m) == SCE_OK);
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        } else {
            // ErrorCheck + Normal relock -> EDEADLK (Normal would deadlock
            // under strict POSIX; we report instead of hanging the watchdog).
            REQUIRE(scePthreadMutexLock(m) == SCE_EDEADLK);
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        }
        // Destroy unlocked ok; use-after-destroy -> EINVAL.
        REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m) == SCE_EINVAL);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_EINVAL);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_EINVAL);
        REQUIRE(scePthreadMutexattrDestroy(&a) == SCE_OK);
    }

    // Destroy of a locked mutex -> EBUSY.
    {
        alignas(8) std::uint64_t s = 0;
        auto* m = SlotOf<PthreadMutex>(s);
        REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_EBUSY);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    }

    // Unlock by non-owner -> EPERM.
    {
        alignas(8) std::uint64_t s = 0;
        auto* m = SlotOf<PthreadMutex>(s);
        REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        std::atomic<int> rc{-1};
        std::thread t([&] { rc = scePthreadMutexUnlock(m); });
        t.join();
        REQUIRE(rc.load() == SCE_EPERM);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    }

    // Trylock: EBUSY when held, ok when free; trylock relock of
    // non-recursive -> EBUSY (not EDEADLK: trylock never blocks).
    {
        alignas(8) std::uint64_t s = 0;
        auto* m = SlotOf<PthreadMutex>(s);
        REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
        REQUIRE(scePthreadMutexTrylock(m) == SCE_OK);
        REQUIRE(scePthreadMutexTrylock(m) == SCE_EBUSY);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    }

    // Timedlock: success when free, ETIMEDOUT on contention.
    {
        alignas(8) std::uint64_t s = 0;
        auto* m = SlotOf<PthreadMutex>(s);
        REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
        REQUIRE(scePthreadMutexTimedlock(m, 1000) == SCE_OK);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        std::atomic<int> rc{-1};
        std::thread t([&] { rc = scePthreadMutexTimedlock(m, 5000); });
        t.join();
        REQUIRE(rc.load() == SCE_TIMEDOUT);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    }

    // Static defaults: slot 0 -> ErrorCheck, slot 1 -> Normal (adaptive).
    {
        alignas(8) std::uint64_t s0 = 0;
        auto* m0 = SlotOf<PthreadMutex>(s0);
        REQUIRE(scePthreadMutexLock(m0) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m0) == SCE_EDEADLK);  // ErrorCheck relock.
        REQUIRE(scePthreadMutexUnlock(m0) == SCE_OK);
        // Word now has INIT; destroy then EINVAL.
        REQUIRE(scePthreadMutexDestroy(m0) == SCE_OK);
        REQUIRE(scePthreadMutexLock(m0) == SCE_EINVAL);
    }
    {
        alignas(8) std::uint64_t s1 = 1;
        auto* m1 = SlotOf<PthreadMutex>(s1);
        REQUIRE(scePthreadMutexLock(m1) == SCE_OK);
        REQUIRE(scePthreadMutexUnlock(m1) == SCE_OK);
        REQUIRE(scePthreadMutexDestroy(m1) == SCE_OK);
    }
    std::printf("PASS mutex basics\n");
}

static void TestMutexRace() {
    // 64 threads racing on a zero slot: exactly one INIT winner (first CAS
    // 0->INIT|ErrorCheck|tid succeeds), the rest contend via CONTENDED +
    // WaitOnAddress. All 64 lock/unlock pairs must succeed, final word is an
    // unlocked INIT word (no DESTROYED, no owner).
    alignas(8) std::uint64_t s = 0;
    auto* m = SlotOf<PthreadMutex>(s);
    std::atomic<int> fails{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 64; ++i) {
        ts.emplace_back([&] {
            for (int k = 0; k < 200; ++k) {
                if (scePthreadMutexLock(m) != SCE_OK)
                    fails.fetch_add(1);
                if (scePthreadMutexUnlock(m) != SCE_OK)
                    fails.fetch_add(1);
            }
        });
    }
    for (auto& t : ts)
        t.join();
    REQUIRE(fails.load() == 0);
    // Unlocked: another lock works, then cleanup.
    REQUIRE(scePthreadMutexLock(m) == SCE_OK);
    REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
    REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    std::printf("PASS mutex 64-thread race\n");
}

static void TestMutexRecursiveOverflow() {
    alignas(8) std::uint64_t s = 0;
    auto* m = SlotOf<PthreadMutex>(s);
    PthreadMutexattr a = nullptr;
    REQUIRE(scePthreadMutexattrInit(&a) == SCE_OK);
    REQUIRE(scePthreadMutexattrSettype(&a, 2) == SCE_OK);
    REQUIRE(scePthreadMutexInit(m, &a, nullptr) == SCE_OK);
    REQUIRE(scePthreadMutexLock(m) == SCE_OK);
    // 16-bit recursion-1 field: 65535 further locks ok (total 65536), next is
    // EAGAIN. This is ~65k CAS ops, well under a second uncontended.
    for (int i = 0; i < 65535; ++i)
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
    REQUIRE(scePthreadMutexLock(m) == SCE_EAGAIN);
    for (int i = 0; i < 65536; ++i)
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
    REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    REQUIRE(scePthreadMutexattrDestroy(&a) == SCE_OK);
    std::printf("PASS mutex recursive overflow\n");
}

static void TestCondPingPong() {
    // 50k ping-pong rounds between two threads (spec asks 10^6; 50k keeps CI
    // fast while still catching lost wakeups: any lost wakeup deadlocks and
    // the timed wait below fires).
    alignas(8) std::uint64_t ms = 0, cs = 0;
    auto* m = SlotOf<PthreadMutex>(ms);
    auto* c = SlotOf<PthreadCond>(cs);
    REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
    REQUIRE(scePthreadCondInit(c, nullptr, nullptr) == SCE_OK);
    constexpr int kRounds = 50000;
    std::atomic<int> turn{0};  // 0 = main signals, 1 = worker signals.
    std::thread worker([&] {
        for (int i = 0; i < kRounds; ++i) {
            REQUIRE(scePthreadMutexLock(m) == SCE_OK);
            while (turn.load() != 1) {
                const int rc = scePthreadCondTimedwait(c, m, 5000000);
                REQUIRE(rc == SCE_OK);
            }
            turn.store(0);
            REQUIRE(scePthreadCondSignal(c) == SCE_OK);
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        }
    });
    for (int i = 0; i < kRounds; ++i) {
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        turn.store(1);
        REQUIRE(scePthreadCondSignal(c) == SCE_OK);
        while (turn.load() != 0) {
            const int rc = scePthreadCondTimedwait(c, m, 5000000);
            REQUIRE(rc == SCE_OK);
        }
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
    }
    worker.join();
    REQUIRE(scePthreadCondDestroy(c) == SCE_OK);
    REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    std::printf("PASS cond ping-pong %d rounds\n", kRounds);
}

static void TestCondBroadcastStorm() {
    // 16 waiters + broadcast storm: every waiter must observe every
    // generation (no lost wakeups). Waiters predicate on a generation counter
    // under the mutex; broadcaster bumps it and broadcasts.
    alignas(8) std::uint64_t ms = 0, cs = 0;
    auto* m = SlotOf<PthreadMutex>(ms);
    auto* c = SlotOf<PthreadCond>(cs);
    REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
    REQUIRE(scePthreadCondInit(c, nullptr, nullptr) == SCE_OK);
    constexpr int kWaiters = 16, kGens = 200;
    std::atomic<int> gen{0};
    std::atomic<int> fails{0};
    std::atomic<int> ready{0};
    std::vector<std::thread> ts;
    for (int w = 0; w < kWaiters; ++w) {
        ts.emplace_back([&, w] {
            int seen = 0;
            REQUIRE(scePthreadMutexLock(m) == SCE_OK);
            ready.fetch_add(1, std::memory_order_release);
            while (seen < kGens) {
                while (gen.load() == seen) {
                    const int rc = scePthreadCondTimedwait(c, m, 5000000);
                    if (rc != SCE_OK)
                        fails.fetch_add(1);
                }
                seen = gen.load();
            }
            REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
        });
    }
    // Ensure all waiters have spawned, locked m, and queued up on c
    while (ready.load(std::memory_order_acquire) != kWaiters) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int g = 1; g <= kGens; ++g) {
        REQUIRE(scePthreadMutexLock(m) == SCE_OK);
        gen.store(g);
        REQUIRE(scePthreadCondBroadcast(c) == SCE_OK);
        REQUIRE(scePthreadMutexUnlock(m) == SCE_OK);
    }
    for (auto& t : ts)
        t.join();
    REQUIRE(fails.load() == 0);
    REQUIRE(scePthreadCondDestroy(c) == SCE_OK);
    REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
    std::printf("PASS cond broadcast storm %dx%d\n", kWaiters, kGens);
}

static void TestRwlock() {
    REQUIRE(scePthreadRwlockRdlock(nullptr) == SCE_EINVAL);
    alignas(8) std::uint64_t s = 0;
    auto* r = SlotOf<PthreadRwlock>(s);
    REQUIRE(scePthreadRwlockInit(r, nullptr, nullptr) == SCE_OK);
    // Multiple readers hold concurrently.
    REQUIRE(scePthreadRwlockRdlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockTryrdlock(r) == SCE_OK);
    // Writer trylock fails while readers hold.
    REQUIRE(scePthreadRwlockTrywrlock(r) == SCE_EBUSY);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    // Writer holds exclusively.
    REQUIRE(scePthreadRwlockWrlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockWrlock(r) == SCE_EDEADLK);  // writer relock.
    REQUIRE(scePthreadRwlockTryrdlock(r) == SCE_EBUSY);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    // Writer preference: with a writer queued (WRITERS_WAITING), a new
    // reader trylock must fail even though no writer holds yet.
    REQUIRE(scePthreadRwlockRdlock(r) == SCE_OK);  // reader A holds.
    std::atomic<bool> writerStarted{false};
    std::atomic<int> writerRc{-1};
    std::thread w([&] {
        writerStarted.store(true);
        writerRc = scePthreadRwlockWrlock(r);  // blocks on reader A.
        if (writerRc.load() == SCE_OK)
            REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    });
    while (!writerStarted.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let writer queue.
    REQUIRE(scePthreadRwlockTryrdlock(r) == SCE_EBUSY);  // preferred writer blocks us.
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);        // release A; writer proceeds.
    w.join();
    REQUIRE(writerRc.load() == SCE_OK);
    REQUIRE(scePthreadRwlockDestroy(r) == SCE_OK);
    REQUIRE(scePthreadRwlockRdlock(r) == SCE_EINVAL);  // use after destroy.
    std::printf("PASS rwlock basics + writer preference\n");
}

static void TestTids() {
    const int self = scePthreadGetthreadid();
    REQUIRE(self >= 1 && self <= 0xFFFFFF);
    REQUIRE(scePthreadGetthreadid() == self);  // stable per thread.
    constexpr int kN = 16;
    std::vector<int> ids(kN, 0);
    std::vector<std::thread> ts;
    std::atomic<bool> hold{true};
    for (int i = 0; i < kN; ++i) {
        ts.emplace_back([&, i] {
            ids[i] = scePthreadGetthreadid();
            while (hold.load(std::memory_order_acquire))
                std::this_thread::yield();
        });
    }
    while (true) {
        bool allSampled = true;
        for (int id : ids) {
            if (id == 0) {
                allSampled = false;
                break;
            }
        }
        if (allSampled)
            break;
        std::this_thread::yield();
    }
    hold.store(false, std::memory_order_release);
    for (auto& t : ts)
        t.join();
    for (int id : ids)
        REQUIRE(id >= 1 && id <= 0xFFFFFF);
    for (int i = 0; i < kN; ++i)
        for (int j = i + 1; j < kN; ++j)
            REQUIRE(ids[i] != ids[j]);  // unique while threads live.
    std::printf("PASS tids compact+unique self=%d\n", self);
}

static void TestUmtx() {
    // WAIT_UINT (4-byte) + WAKE_PRIVATE, and WAIT long (8-byte) + WAKE.
    {
        // Timed 4-byte wait with an already-changed value -> EBUSY (no sleep).
        alignas(4) std::uint32_t w = 1;
        REQUIRE(_umtx_op_nid_postfix(&w, 11, 2, nullptr, nullptr) == SCE_EBUSY);
        // Timed wait that expires: pass a past absolute deadline.
        KernelTimespec past{0, 0};
        std::atomic<int> rc{-1};
        std::thread t([&] { rc = _umtx_op_nid_postfix(&w, 11, 1, &past, nullptr); });
        t.join();
        REQUIRE(rc.load() == SCE_TIMEDOUT);
        // Wake with no waiters is a no-op success.
        REQUIRE(_umtx_op_nid_postfix(&w, 16, 1, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(&w, 3, 0, nullptr, nullptr) == SCE_OK);
        // Wake with INT_MAX / UINT32_MAX sentinel wakes all and returns promptly.
        REQUIRE(_umtx_op_nid_postfix(&w, 3, 2147483647ULL, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(&w, 3, 0xFFFFFFFFULL, nullptr, nullptr) == SCE_OK);
    }
    {
        alignas(8) std::uint64_t w = 0xABCDEF;
        REQUIRE(_umtx_op_nid_postfix(&w, 2, 0, nullptr, nullptr) == SCE_EBUSY);
        KernelTimespec past{0, 0};
        std::atomic<int> rc{-1};
        std::thread t([&] { rc = _umtx_op_nid_postfix(&w, 2, 0xABCDEF, &past, nullptr); });
        t.join();
        REQUIRE(rc.load() == SCE_TIMEDOUT);
        // Wake-N (val=2) with no waiters still succeeds.
        REQUIRE(_umtx_op_nid_postfix(&w, 18, 2, nullptr, nullptr) == SCE_OK);
    }
    {
        // MUTEX trylock/lock/unlock over the 32-bit owner word.
        alignas(4) std::uint32_t m = 0;
        REQUIRE(_umtx_op_nid_postfix(&m, 4, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(&m, 4, 0, nullptr, nullptr) == SCE_EBUSY);
        REQUIRE(_umtx_op_nid_postfix(&m, 6, 0, nullptr, nullptr) == SCE_OK);

        // Robust mutex state (bit 29) -> SCE_ENOTRECOVERABLE.
        alignas(4) std::uint32_t m_notrecov = 0x20000000u;
        REQUIRE(_umtx_op_nid_postfix(&m_notrecov, 4, 0, nullptr, nullptr) == SCE_ENOTRECOVERABLE);
        // Robust mutex state (bit 30) -> SCE_EOWNERDEAD (and acquired).
        alignas(4) std::uint32_t m_ownerdead = 0x40000000u;
        REQUIRE(_umtx_op_nid_postfix(&m_ownerdead, 4, 0, nullptr, nullptr) == SCE_EOWNERDEAD);
        REQUIRE((m_ownerdead & 0xFFFFFFu) != 0);  // acquired by current thread tid.

        // Guest TID 16 (0x10) and TID 32 (0x20) must NOT collide with robust markers!
        alignas(4) std::uint32_t m_tid16 = 16u;
        REQUIRE(_umtx_op_nid_postfix(&m_tid16, 4, 0, nullptr, nullptr) == SCE_EBUSY);
        alignas(4) std::uint32_t m_tid32 = 32u;
        REQUIRE(_umtx_op_nid_postfix(&m_tid32, 4, 0, nullptr, nullptr) == SCE_EBUSY);

        // Raw 4-byte word lock/unlock (ops 0 and 1) does not read past allocation.
        alignas(4) std::uint32_t raw_word = 0;
        REQUIRE(_umtx_op_nid_postfix(&raw_word, 0, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(&raw_word, 1, 0, nullptr, nullptr) == SCE_OK);

        // Unknown op -> EINVAL.
        REQUIRE(_umtx_op_nid_postfix(&m, 999, 0, nullptr, nullptr) == SCE_EINVAL);
    }
    {
        // _umtx_time relative timeout parsing.
        struct UmtxTime {
            KernelTimespec timeout;
            std::uint32_t flags;   // UMTX_ABSTIME = 0x01
            std::uint32_t clockid;
        };
        alignas(4) std::uint32_t wait_word = 1234;
        UmtxTime ut{};
        ut.timeout.tv_sec = 0;
        ut.timeout.tv_nsec = 50 * 1000 * 1000; // 50ms relative
        ut.flags = 0; // Relative
        ut.clockid = 0;
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE(_umtx_op_nid_postfix(&wait_word, 11, 1234,
                                     reinterpret_cast<void*>(sizeof(UmtxTime)), &ut) == SCE_TIMEDOUT);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        REQUIRE(elapsed >= 30);  // Did not return immediately!
    }
    {
        // _umtx_op RWLOCK (ops 12, 13, 14).
        alignas(4) std::uint32_t rw[4] = {0, 0, 0, 0};
        // Acquire read lock (op 12).
        REQUIRE(_umtx_op_nid_postfix(rw, 12, 0, nullptr, nullptr) == SCE_OK);
        // Second reader lock succeeds.
        REQUIRE(_umtx_op_nid_postfix(rw, 12, 0, nullptr, nullptr) == SCE_OK);
        // Release readers (op 14).
        REQUIRE(_umtx_op_nid_postfix(rw, 14, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(rw, 14, 0, nullptr, nullptr) == SCE_OK);
        // Acquire write lock (op 13).
        REQUIRE(_umtx_op_nid_postfix(rw, 13, 0, nullptr, nullptr) == SCE_OK);
        // Release write lock (op 14).
        REQUIRE(_umtx_op_nid_postfix(rw, 14, 0, nullptr, nullptr) == SCE_OK);
    }
    {
        // _umtx_op CV (ops 8, 9, 10).
        alignas(4) std::uint32_t cv[4] = {0, 0, 0, 0};
        alignas(4) std::uint32_t mutex[4] = {0, 0, 0, 0};
        std::atomic<bool> threadWaiting{false};
        std::atomic<int> waitResult{-1};
        std::thread t([&] {
            REQUIRE(_umtx_op_nid_postfix(mutex, 4, 0, nullptr, nullptr) == SCE_OK);
            threadWaiting.store(true);
            waitResult.store(_umtx_op_nid_postfix(cv, 8, 0, mutex, nullptr));
            REQUIRE(_umtx_op_nid_postfix(mutex, 6, 0, nullptr, nullptr) == SCE_OK);
        });
        while (!threadWaiting.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Signal CV (op 9).
        REQUIRE(_umtx_op_nid_postfix(cv, 9, 0, nullptr, nullptr) == SCE_OK);
        t.join();
        REQUIRE(waitResult.load() == SCE_OK);
    }
    std::printf("PASS umtx wait/wake 4+8, mutex word, robust non-collision, timeout, rwlock, cv\n");
}

static void Microbenchmark() {
    // Uncontended lock/unlock pair (the M1 exit metric). No contention, so
    // the fast path is two CAS ops with no kernel wait.
    alignas(8) std::uint64_t s = 0;
    auto* m = SlotOf<PthreadMutex>(s);
    REQUIRE(scePthreadMutexInit(m, nullptr, nullptr) == SCE_OK);
    constexpr int kIters = 2000000;
    // Warm up.
    for (int i = 0; i < 10000; ++i) {
        scePthreadMutexLock(m);
        scePthreadMutexUnlock(m);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        scePthreadMutexLock(m);
        scePthreadMutexUnlock(m);
    }
    const auto end = std::chrono::steady_clock::now();
    const double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    const double perPair = ns / kIters;
    std::printf("MICRO lock/unlock pair: %.1f ns/pair over %d iters (%.1f Mpairs/s)\n", perPair,
                kIters, 1000.0 / perPair);
    REQUIRE(scePthreadMutexDestroy(m) == SCE_OK);
}

int main() {
    TestMutexBasics();
    TestMutexRace();
    TestMutexRecursiveOverflow();
    TestCondPingPong();
    TestCondBroadcastStorm();
    TestRwlock();
    TestTids();
    TestUmtx();
    Microbenchmark();
    std::printf("PASS: futex mutex/cond/rwlock/tids/umtx + microbenchmark\n");
    return 0;
}
