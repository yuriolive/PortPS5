#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" {
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr,
                                 const char* name) noexcept;
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex) noexcept;
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr,
                                const char* name) noexcept;
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond) noexcept;
int APS5_VABI scePthreadCondSignal(PthreadCond* cond) noexcept;
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex,
                                      KernelUseconds usec) noexcept;
}

int main() {
    alignas(8) std::uint64_t ms = 0, cs = 0;
    auto* m = reinterpret_cast<PthreadMutex*>(&ms);
    auto* c = reinterpret_cast<PthreadCond*>(&cs);
    printf("init m=%d c=%d\n", scePthreadMutexInit(m, nullptr, nullptr),
           scePthreadCondInit(c, nullptr, nullptr));
    printf("mword after init: 0x%llx cword: 0x%llx\n", (unsigned long long)ms,
           (unsigned long long)cs);
    std::atomic<int> turn{0};
    std::thread w([&] {
        printf("worker: lock %d\n", scePthreadMutexLock(m));
        printf("worker: locked, mword=0x%llx turn=%d\n", (unsigned long long)ms, turn.load());
        int spins = 0;
        while (turn.load() != 1 && spins < 50) {
            printf("worker: wait begin cword=0x%llx\n", (unsigned long long)cs);
            const int rc = scePthreadCondTimedwait(c, m, 1000000);
            printf("worker: wait rc=%d cword=0x%llx mword=0x%llx turn=%d\n", rc,
                   (unsigned long long)cs, (unsigned long long)ms, turn.load());
            if (rc != 0)
                break;
            ++spins;
        }
        printf("worker: unlock %d\n", scePthreadMutexUnlock(m));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("main: lock %d\n", scePthreadMutexLock(m));
    turn.store(1);
    printf("main: signal %d cword=0x%llx\n", scePthreadCondSignal(c),
           (unsigned long long)cs);
    printf("main: unlock %d\n", scePthreadMutexUnlock(m));
    w.join();
    printf("done mword=0x%llx cword=0x%llx\n", (unsigned long long)ms, (unsigned long long)cs);
    return 0;
}
