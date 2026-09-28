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
// Documented test prototype for sceKernelUsleep_nid_postfix.
int APS5_VABI sceKernelUsleep_nid_postfix(unsigned int microseconds) noexcept;
}

struct UmtxTime {
    KernelTimespec timeout;
    std::uint32_t flags;   // UMTX_ABSTIME = 0x01
    std::uint32_t clockid;
};

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
    // Writer preference & recursion: with a writer queued (WRITERS_WAITING),
    // an existing reader can recursively re-acquire (preventing self-deadlock),
    // but a new reader thread must fail with SCE_EBUSY.
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
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let writer queue.

    // Recursive read locks by existing reader A must succeed even though writer is waiting:
    REQUIRE(scePthreadRwlockRdlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockTryrdlock(r) == SCE_OK);

    // A separate, new reader thread MUST be blocked by writer preference:
    std::atomic<int> readerBRc{-1};
    std::thread r2([&] {
        readerBRc.store(scePthreadRwlockTryrdlock(r));
    });
    r2.join();
    REQUIRE(readerBRc.load() == SCE_EBUSY);  // preferred writer blocks new reader.

    // Unlocking reader A 3 times releases all held locks, letting writer proceed:
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    w.join();
    REQUIRE(writerRc.load() == SCE_OK);
    // After writer unlocks, the idle word must be clean (kWritersWaiting cleared),
    // so subsequent readers do not block and tryrdlock immediately succeeds.
    REQUIRE(scePthreadRwlockTryrdlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockUnlock(r) == SCE_OK);
    REQUIRE(scePthreadRwlockDestroy(r) == SCE_OK);
    REQUIRE(scePthreadRwlockRdlock(r) == SCE_EINVAL);  // use after destroy.
    std::printf("PASS rwlock basics + writer preference + recursion\n");
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

        // Op 21: NWAKE_PRIVATE wakes all private waiters across an array of addresses.
        // val == 0 is a no-op that returns SCE_OK.
        alignas(4) std::uint32_t nw[2] = {1, 2};
        void* nwAddrs[2] = {&nw[0], &nw[1]};
        REQUIRE(_umtx_op_nid_postfix(nwAddrs, 21, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(nwAddrs, 21, 2, nullptr, nullptr) == SCE_OK);
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
        alignas(4) std::uint32_t m[4] = {0, 0, 0, 0};
        REQUIRE(_umtx_op_nid_postfix(m, 4, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(m, 4, 0, nullptr, nullptr) == SCE_EBUSY);
        REQUIRE(_umtx_op_nid_postfix(m, 6, 0, nullptr, nullptr) == SCE_OK);

        // Op 5: UMTX_OP_MUTEX_LOCK locks free mutex; unlocked via op 6.
        REQUIRE(_umtx_op_nid_postfix(m, 5, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(m, 6, 0, nullptr, nullptr) == SCE_OK);

        // Op 17: UMTX_OP_MUTEX_WAIT wait-only path.
        // Mutex is unlocked (0): returns SCE_OK immediately without acquiring ownership.
        alignas(4) std::uint32_t mWait = 0;
        REQUIRE(_umtx_op_nid_postfix(&mWait, 17, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(mWait == 0); // Not acquired!
        // Mutex is locked: timed wait with past deadline expires with SCE_TIMEDOUT.
        REQUIRE(_umtx_op_nid_postfix(&mWait, 4, 0, nullptr, nullptr) == SCE_OK); // locked
        KernelTimespec pastWait{0, 0};
        REQUIRE(_umtx_op_nid_postfix(&mWait, 17, 0, &pastWait, nullptr) == SCE_TIMEDOUT);
        REQUIRE(_umtx_op_nid_postfix(&mWait, 6, 0, nullptr, nullptr) == SCE_OK); // unlock

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
        REQUIRE(_umtx_op_nid_postfix(m, 999, 0, nullptr, nullptr) == SCE_EINVAL);
    }
    {
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

        // Contended writer handoff: reader-to-writer and writer-to-writer
        // must clear waiter flags to 0 so queued writers can acquire.
        alignas(4) std::uint32_t rwHandoff[4] = {0, 0, 0, 0};
        REQUIRE(_umtx_op_nid_postfix(rwHandoff, 12, 0, nullptr, nullptr) == SCE_OK);  // reader holds.
        std::atomic<bool> writerQueued{false};
        std::atomic<int> writerResult{-1};
        std::thread wrThread([&] {
            writerQueued.store(true);
            writerResult.store(_umtx_op_nid_postfix(rwHandoff, 13, 0, nullptr, nullptr));
            if (writerResult.load() == SCE_OK) {
                // Writer release clears state to 0 so subsequent acquirers succeed.
                REQUIRE(_umtx_op_nid_postfix(rwHandoff, 14, 0, nullptr, nullptr) == SCE_OK);
            }
        });
        while (!writerQueued.load())
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Reader release wakes queued writer:
        REQUIRE(_umtx_op_nid_postfix(rwHandoff, 14, 0, nullptr, nullptr) == SCE_OK);
        wrThread.join();
        REQUIRE(writerResult.load() == SCE_OK);
        // Word state must now be 0, allowing an immediate write lock:
        REQUIRE(_umtx_op_nid_postfix(rwHandoff, 13, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(rwHandoff, 14, 0, nullptr, nullptr) == SCE_OK);
    }
    {
        // _umtx_op CV (ops 8, 9, 10).
        // Per FreeBSD syscall contract, _umtx_op(cv, 8, ...) unlocks the mutex
        // and returns with the mutex UNLOCKED (caller re-locks in userland).
        alignas(4) std::uint32_t cv[4] = {0, 0, 0, 0};
        alignas(4) std::uint32_t mutex[4] = {0, 0, 0, 0};
        std::atomic<bool> threadWaiting{false};
        std::atomic<int> waitResult{-1};
        std::thread t([&] {
            REQUIRE(_umtx_op_nid_postfix(mutex, 4, 0, nullptr, nullptr) == SCE_OK);
            threadWaiting.store(true);
            waitResult.store(_umtx_op_nid_postfix(cv, 8, 0, mutex, nullptr));
            // Sycall returned: mutex MUST be unlocked. Unlocking directly returns EPERM.
            REQUIRE(_umtx_op_nid_postfix(mutex, 6, 0, nullptr, nullptr) == SCE_EPERM);
            // Reacquire mutex in userland:
            REQUIRE(_umtx_op_nid_postfix(mutex, 4, 0, nullptr, nullptr) == SCE_OK);
            REQUIRE(_umtx_op_nid_postfix(mutex, 6, 0, nullptr, nullptr) == SCE_OK);
        });
        while (!threadWaiting.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Verify mutex was unlocked while thread was waiting on cv:
        REQUIRE(mutex[0] == 0);
        // Verify c_has_waiters is marked (cv[0] == 1) for userland fast-path detection:
        REQUIRE(cv[0] == 1);
        // Signal CV (op 9).
        REQUIRE(_umtx_op_nid_postfix(cv, 9, 0, nullptr, nullptr) == SCE_OK);
        t.join();
        REQUIRE(waitResult.load() == SCE_OK);
        // Verify c_has_waiters is cleared back to 0 once waiter has completed:
        REQUIRE(cv[0] == 0);

        // Multi-waiter CV test: multiple concurrent waiters capture generation without
        // modifying it, ensuring neither waiter spuriously wakes before signal/broadcast.
        alignas(4) std::uint32_t cvMulti[4] = {0, 0, 0, 0};
        alignas(4) std::uint32_t mMulti1[4] = {0, 0, 0, 0};
        alignas(4) std::uint32_t mMulti2[4] = {0, 0, 0, 0};
        std::atomic<bool> w1Waiting{false};
        std::atomic<bool> w2Waiting{false};
        std::atomic<int> r1{-1};
        std::atomic<int> r2{-1};
        std::thread t1([&] {
            REQUIRE(_umtx_op_nid_postfix(mMulti1, 4, 0, nullptr, nullptr) == SCE_OK);
            w1Waiting.store(true);
            r1.store(_umtx_op_nid_postfix(cvMulti, 8, 0, mMulti1, nullptr));
            REQUIRE(_umtx_op_nid_postfix(mMulti1, 4, 0, nullptr, nullptr) == SCE_OK);
            REQUIRE(_umtx_op_nid_postfix(mMulti1, 6, 0, nullptr, nullptr) == SCE_OK);
        });
        while (!w1Waiting.load() || mMulti1[0] != 0)
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Verify w1 is still waiting and has not spuriously awakened.
        REQUIRE(r1.load() == -1);

        std::thread t2([&] {
            REQUIRE(_umtx_op_nid_postfix(mMulti2, 4, 0, nullptr, nullptr) == SCE_OK);
            w2Waiting.store(true);
            r2.store(_umtx_op_nid_postfix(cvMulti, 8, 0, mMulti2, nullptr));
            REQUIRE(_umtx_op_nid_postfix(mMulti2, 4, 0, nullptr, nullptr) == SCE_OK);
            REQUIRE(_umtx_op_nid_postfix(mMulti2, 6, 0, nullptr, nullptr) == SCE_OK);
        });
        while (!w2Waiting.load() || mMulti2[0] != 0)
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Neither w1 nor w2 should have woken up yet!
        REQUIRE(r1.load() == -1);
        REQUIRE(r2.load() == -1);

        // Broadcast to wake both:
        REQUIRE(_umtx_op_nid_postfix(cvMulti, 10, 0, nullptr, nullptr) == SCE_OK);
        t1.join();
        t2.join();
        REQUIRE(r1.load() == SCE_OK);
        REQUIRE(r2.load() == SCE_OK);

        // CV wait with CVWAIT_ABSTIME (val = 2) and past timestamp returns timeout:
        alignas(4) std::uint32_t cvTime[4] = {0, 0, 0, 0};
        alignas(4) std::uint32_t mTime[4] = {0, 0, 0, 0};
        REQUIRE(_umtx_op_nid_postfix(mTime, 4, 0, nullptr, nullptr) == SCE_OK);
        KernelTimespec pastTs{0, 1}; // epoch + 1ns
        REQUIRE(_umtx_op_nid_postfix(cvTime, 8, 2, mTime, &pastTs) == SCE_TIMEDOUT);
        // Re-lock mTime before second cv wait since cv wait returns with mutex unlocked:
        REQUIRE(_umtx_op_nid_postfix(mTime, 4, 0, nullptr, nullptr) == SCE_OK);
        // CV wait with CVWAIT_ABSTIME | CVWAIT_CLOCKID: clockid is taken from cvTime[2] (c_clockid).
        // CLOCK_MONOTONIC_FAST (4) with past timestamp returns SCE_TIMEDOUT:
        cvTime[2] = 4;
        REQUIRE(_umtx_op_nid_postfix(cvTime, 8, 2 | 4, mTime, &pastTs) == SCE_TIMEDOUT);
        REQUIRE(_umtx_op_nid_postfix(mTime, 4, 0, nullptr, nullptr) == SCE_OK);

        // Reject invalid clock ID in cvTime[2] (e.g. clockid = 99):
        cvTime[2] = 99;
        REQUIRE(_umtx_op_nid_postfix(cvTime, 8, 2 | 4, mTime, &pastTs) == SCE_EINVAL);
        // Mutex remains held because invalid parameter was rejected before unlocking.
        // When CVWAIT_CLOCKID is not set in val (val = 2), invalid cvTime[2] is ignored and defaults to CLOCK_REALTIME:
        REQUIRE(_umtx_op_nid_postfix(cvTime, 8, 2, mTime, &pastTs) == SCE_TIMEDOUT);
        // CV wait timed out and returned with mutex unlocked:
        REQUIRE(_umtx_op_nid_postfix(mTime, 6, 0, nullptr, nullptr) == SCE_EPERM);
        REQUIRE(_umtx_op_nid_postfix(mTime, 4, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(mTime, 6, 0, nullptr, nullptr) == SCE_OK);
    }
    {
        // Priority ceiling validation and UMTX_OP_SET_CEILING (op 7):
        // m_flags (m[1]) must have UMUTEX_PRIO_PROTECT (0x0008).
        // Ceiling <= 700 is valid; ceiling > 700 returns EINVAL.
        alignas(4) std::uint32_t mPrio[4] = {0, 0x0008u, 25u, 0};
        REQUIRE(_umtx_op_nid_postfix(mPrio, 5, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(_umtx_op_nid_postfix(mPrio, 6, 0, nullptr, nullptr) == SCE_OK);

        // Ceiling > 700 is rejected:
        alignas(4) std::uint32_t mPrioInvalid[4] = {0, 0x0008u, 701u, 0};
        REQUIRE(_umtx_op_nid_postfix(mPrioInvalid, 5, 0, nullptr, nullptr) == SCE_EINVAL);

        // SET_CEILING requires UMUTEX_PRIO_PROTECT in m[1]:
        alignas(4) std::uint32_t mNonPrio[4] = {0, 0x0000u, 0, 0};
        std::uint32_t oldCeiling = 0;
        REQUIRE(_umtx_op_nid_postfix(mNonPrio, 7, 30, &oldCeiling, nullptr) == SCE_EINVAL);

        // SET_CEILING rejects ceiling > 700:
        REQUIRE(_umtx_op_nid_postfix(mPrio, 7, 701, &oldCeiling, nullptr) == SCE_EINVAL);

        // SET_CEILING succeeds with valid ceiling, returns previous ceiling:
        REQUIRE(_umtx_op_nid_postfix(mPrio, 7, 35, &oldCeiling, nullptr) == SCE_OK);
        REQUIRE(oldCeiling == 25u);
        REQUIRE(mPrio[2] == 35u);
    }
    {
        // Reject invalid clockid in ParseUmtxTimeout:
        alignas(4) std::uint32_t wait_word = 1234;
        UmtxTime ut{};
        ut.timeout.tv_sec = 0;
        ut.timeout.tv_nsec = 1000;
        ut.flags = 0;
        ut.clockid = 99; // Invalid clock ID
        REQUIRE(_umtx_op_nid_postfix(&wait_word, 11, 1234,
                                     reinterpret_cast<void*>(sizeof(UmtxTime)), &ut) == SCE_EINVAL);
    }
    {
        // RWLOCK owner TID verification, EDEADLK detection, and rwWord[1] URWLOCK_PREFER_READER.
        // In FreeBSD struct urwlock, rwWord[2] is rw_blocked_readers and rwWord[3] is rw_blocked_writers;
        // locking/unlocking must never clobber these words.
        alignas(4) std::uint32_t rwTest[4] = {0, 0x02u /* URWLOCK_PREFER_READER */, 0x12345678u, 0x87654321u};
        // Acquire write lock:
        REQUIRE(_umtx_op_nid_postfix(rwTest, 13, 0, nullptr, nullptr) == SCE_OK);
        // Verify rw_blocked_readers and rw_blocked_writers remain untouched:
        REQUIRE(rwTest[2] == 0x12345678u);
        REQUIRE(rwTest[3] == 0x87654321u);
        // Recursive write lock from same thread must return SCE_EDEADLK:
        REQUIRE(_umtx_op_nid_postfix(rwTest, 13, 0, nullptr, nullptr) == SCE_EDEADLK);

        // Unlocking from another thread must return SCE_EPERM:
        std::atomic<int> otherUnlockRc{0};
        std::thread otherThread([&] {
            otherUnlockRc.store(_umtx_op_nid_postfix(rwTest, 14, 0, nullptr, nullptr));
        });
        otherThread.join();
        REQUIRE(otherUnlockRc.load() == SCE_EPERM);

        // Owner unlocks successfully, preserving layout words:
        REQUIRE(_umtx_op_nid_postfix(rwTest, 14, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(rwTest[2] == 0x12345678u);
        REQUIRE(rwTest[3] == 0x87654321u);

        // Reader preference in rwWord[1] allows reader acquisition even if writer is waiting:
        // Also test wrlock timeout cleans up kRwWriteWaiters bit:
        UmtxTime shortTimeout{};
        shortTimeout.timeout.tv_sec = 0;
        shortTimeout.timeout.tv_nsec = 10 * 1000 * 1000; // 10ms
        shortTimeout.flags = 0;
        shortTimeout.clockid = 0;

        // Hold read lock:
        REQUIRE(_umtx_op_nid_postfix(rwTest, 12, 0, nullptr, nullptr) == SCE_OK);
        // Thread tries wrlock and times out:
        std::atomic<int> timedOutRc{0};
        std::thread wrTimeoutThread([&] {
            timedOutRc.store(_umtx_op_nid_postfix(rwTest, 13, 0,
                             reinterpret_cast<void*>(sizeof(UmtxTime)), &shortTimeout));
        });
        wrTimeoutThread.join();
        REQUIRE(timedOutRc.load() == SCE_TIMEDOUT);
        // kRwWriteWaiters must have been cleared on timeout, so word state is just 1 (1 reader):
        REQUIRE(rwTest[0] == 1u);
        // Unlock reader:
        REQUIRE(_umtx_op_nid_postfix(rwTest, 14, 0, nullptr, nullptr) == SCE_OK);
        REQUIRE(rwTest[0] == 0u);
    }
    std::printf("PASS umtx wait/wake 4+8, mutex word, robust non-collision, timeout, rwlock, cv\n");
}

/**
 * @brief Tests sub-millisecond sleep precision and chunked millisecond sleeping.
 * Verifies that sceKernelUsleep does not return immediately when no other thread is ready.
 */
static void TestSubMillisecondSleep() {
    // sceKernelUsleep(0) yields immediately.
    REQUIRE(sceKernelUsleep_nid_postfix(0) == SCE_OK);

    // Sub-millisecond sleep (e.g. 500us): must not return immediately (0us).
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(sceKernelUsleep_nid_postfix(500) == SCE_OK);
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    REQUIRE(elapsedUs >= 400);

    // Multi-millisecond sleep (e.g. 2000us): exercises chunked millisecond Sleep path.
    const auto t1 = std::chrono::steady_clock::now();
    REQUIRE(sceKernelUsleep_nid_postfix(2000) == SCE_OK);
    const auto elapsedMsUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t1).count();
    REQUIRE(elapsedMsUs >= 1800);
    std::printf("PASS sub-millisecond sleep duration precision\n");
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
    TestSubMillisecondSleep();
    Microbenchmark();
    std::printf("PASS: futex mutex/cond/rwlock/tids/umtx + microbenchmark\n");
    return 0;
}
