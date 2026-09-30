// libc runtime support: process-exit handler registry (__cxa_atexit / __cxa_finalize), C11 atomics helpers
// and FileStream globals. Exit handlers are guest code, so they are stored and invoked with the System V
// calling convention (APS5_VABI); calling them through a plain function pointer would pass the argument in
// the wrong register on Windows. The registry is guarded by g_exitMutex; handlers run with the lock released
// so a handler may itself register further handlers or call finalize.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <stdexcept>
#include <functional>
#include <regex>
#include <mutex>
#include <vector>
#include <utility>

#include "prx/libc/include/General.hpp"
#include "prx/libc/include/specifics/gcc/AtomicOps.hpp"
#include "prx/libc/include/FileStream.hpp"
#include "SceTypes.hpp"

// Defined in LocaleSupport.cpp: initializes the application heap from the process parameters.
extern "C" void APS5_VABI _init_env_nid_postfix();

namespace {

std::recursive_mutex g_sysLock;

struct ExitDestructor {
    void (APS5_VABI *func)(void*);
    void* arg;
    void* dsoHandle;
};

std::mutex g_exitMutex;
std::vector<ExitDestructor> g_exitDestructors;
bool g_exitRunnerRegistered = false;

}

extern "C" {

FileStream _Stderr_nid_postfix{stderr};
FileStream _Stdout_nid_postfix{stdout};

/// Registers `func(arg)` to run at process exit or when `dsoHandle` is finalized. Returns 0 (registration
/// can not fail). The first registration installs the std::atexit hook that finalizes everything.
int APS5_VABI __cxa_atexit_nid_postfix(void (APS5_VABI *func)(void*), void* arg, void* dsoHandle) {
    std::lock_guard lock(g_exitMutex);
    g_exitDestructors.push_back({func, arg, dsoHandle});
    if (!g_exitRunnerRegistered) {
        g_exitRunnerRegistered = true;
        std::atexit([] { CxaFinalize_nid_no_patch(nullptr); });
    }
    return 0;
}

/// Runs and removes matching handlers newest-first. The lock is dropped around each call: a handler may
/// register more handlers (they run too, since the search restarts) without deadlocking.
void CxaFinalize_nid_no_patch(void* dsoHandle) {
    for (;;) {
        ExitDestructor destructor{};
        {
            std::lock_guard lock(g_exitMutex);
            const auto found = std::find_if(g_exitDestructors.rbegin(), g_exitDestructors.rend(), [&](const ExitDestructor& entry) {
                return dsoHandle == nullptr || entry.dsoHandle == dsoHandle;
            });
            if (found == g_exitDestructors.rend()) return;
            destructor = *found;
            // reverse_iterator::base() points one past the element, hence the std::next(...).base() idiom.
            g_exitDestructors.erase(std::next(found).base());
        }
        destructor.func(destructor.arg);
    }
}

/// PS5 libc exports the same registry under the un-underscored name `cxa_atexit`.
int APS5_VABI cxa_atexit_nid_postfix(void (APS5_VABI *func)(void*), void* arg, void* dsoHandle) {
    return __cxa_atexit_nid_postfix(func, arg, dsoHandle);
}

/// PS5 libc entry `init_env(params)`. The argument block is ignored: process parameters reach the host
/// through ApplicationProcessParameters, so this shares `_init_env`'s initialization (defined in
/// LocaleSupport.cpp). Kept here, not in LocaleSupport.cpp, to stay clear of the locale-export work.
void APS5_VABI init_env_nid_postfix(const InitEnvParams* params) {
    (void)params;
    _init_env_nid_postfix();
}

/// PS5 libc export `cxa_finalize`; see CxaFinalize_nid_no_patch.
void APS5_VABI cxa_finalize_nid_postfix(void* dsoHandle) {
    CxaFinalize_nid_no_patch(dsoHandle);
}

/// 4-byte atomic fetch-add (sequentially consistent regardless of `memoryOrder`); returns the previous value.
unsigned int APS5_VABI _Atomic_fetch_add_4_nid_postfix(volatile unsigned int* target, unsigned int value, int memoryOrder) {
    (void)memoryOrder;
    return GccAtomicFetchAdd(target, value);
}

/// 4-byte atomic fetch-sub (sequentially consistent regardless of `memoryOrder`); returns the previous value.
unsigned int APS5_VABI _Atomic_fetch_sub_4_nid_postfix(volatile unsigned int* target, unsigned int value, int memoryOrder) {
    (void)memoryOrder;
    return GccAtomicFetchSub(target, value);
}

/// MSVC-STL-style 4-byte weak compare-exchange used by the guest's <atomic>. Returns 1 on success, 0 on
/// failure (then `*expected` holds the current value). Memory orders are ignored: the implementation is
/// always sequentially consistent, which is at least as strong as any order the guest requests.
int APS5_VABI _Atomic_compare_exchange_weak_4_nid_postfix(volatile unsigned int* target, unsigned int* expected, unsigned int desired, int successOrder, int failureOrder) {
    (void)successOrder;
    (void)failureOrder;
    return GccAtomicCompareExchangeWeak(target, expected, desired) ? 1 : 0;
}

/// 4-byte atomic load (sequentially consistent regardless of the requested order).
unsigned int APS5_VABI _Atomic_load_4_nid_postfix(volatile unsigned int* target, int memoryOrder) {
    (void)memoryOrder;
    return GccAtomicLoad(target);
}

/// MSVC-STL `_Stoul`: forwards to std::strtoul (host width and errno behaviour).
unsigned long APS5_VABI _Stoul_nid_postfix(const char* str, char** endptr, int base) {
    return std::strtoul(str, endptr, base);
}

/// Acquires the process-wide recursive "system lock" the guest's C++ runtime uses (paired with
/// _Unlocksyslock; recursion depth is the caller's responsibility).
void APS5_VABI _Locksyslock_nid_postfix() {
    g_sysLock.lock();
}

/// Releases one level of the system lock taken by _Locksyslock.
void APS5_VABI _Unlocksyslock_nid_postfix() {
    g_sysLock.unlock();
}

}
