// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#include "../include/Pthread.hpp"
#include "../include/ThreadLifecycle.hpp"
#include "prx/libkernel/Pthread/include/GuestTid.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HostThreadLocal.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <new>
#include <thread>

#ifndef _WIN32
#include <pthread.h>
#endif

static constexpr int SCE_OK = 0;
static constexpr int SCE_KERNEL_ERROR_EINVAL = static_cast<int>(0x80020016u);
static constexpr int SCE_KERNEL_ERROR_EAGAIN = static_cast<int>(0x80020023u);
static constexpr int SCE_KERNEL_ERROR_ENOMEM = static_cast<int>(0x8002000Cu);
static constexpr int SCE_KERNEL_ERROR_ESRCH = static_cast<int>(0x80020003u);

static constexpr std::size_t DEFAULT_STACK_SIZE = 1u << 20;
static constexpr int DETACH_DETACHED = 1;

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <limits>
#endif

struct ThreadArgs {
    PthreadEntry entry;
    void* arg;
    PthreadPrivate* self;
};

static std::atomic<thread_dtors_func_t> threadDtors{nullptr};
static std::atomic<get_thread_atexit_count_func_t> threadAtexitCount{nullptr};
static std::atomic<thread_atexit_report_func_t> threadAtexitReport{nullptr};
static thread_local PthreadPrivate* currentThread = nullptr;
static thread_local bool threadFinishing = false;

void ThreadLifecycle::SetThreadDtors(thread_dtors_func_t callback) {
    if (!callback)
        return;
    thread_dtors_func_t expected = nullptr;
    threadDtors.compare_exchange_strong(expected, callback);
}

void ThreadLifecycle::SetThreadAtexitCount(get_thread_atexit_count_func_t callback) {
    if (!callback)
        return;
    get_thread_atexit_count_func_t expected = nullptr;
    threadAtexitCount.compare_exchange_strong(expected, callback);
}

void ThreadLifecycle::SetThreadAtexitReport(thread_atexit_report_func_t callback) {
    if (!callback)
        return;
    thread_atexit_report_func_t expected = nullptr;
    threadAtexitReport.compare_exchange_strong(expected, callback);
}

#ifdef _WIN32
// Map a PS5 priority (default 700) onto five Win32 bands, never realtime.
// Why bands: the guest range names console scheduling classes, not host
// priorities; mapping to LOWEST..HIGHEST preserves relative order without
// starving presenter/driver threads (no REALTIME class, no TIME_CRITICAL).
// Thresholds center the default 700 on NORMAL.
int MapPriorityToWin32(int prio) noexcept {
    if (prio < 256)
        return THREAD_PRIORITY_LOWEST;
    if (prio < 512)
        return THREAD_PRIORITY_BELOW_NORMAL;
    if (prio < 768)
        return THREAD_PRIORITY_NORMAL;
    if (prio < 1024)
        return THREAD_PRIORITY_ABOVE_NORMAL;
    return THREAD_PRIORITY_HIGHEST;
}

void ApplyPriority(void* handle, int prio) noexcept {
    if (!handle)
        return;
    SetThreadPriority(handle, MapPriorityToWin32(prio));
}
#endif

static void finishThread(PthreadPrivate* self, void* retval) noexcept {
    if (!self || self != currentThread)
        return;
    if (threadFinishing)
        return;
    threadFinishing = true;
    if (const auto callback = threadDtors.load(std::memory_order_acquire)) {
        // Host exceptions never cross APS5_VABI; dtors run here on the host
        // side before we publish retval. A throwing dtor would terminate,
        // which is preferable to corrupting guest state.
        callback();
    }
    {
        std::unique_lock<std::mutex> lk(self->_join_mtx);
        self->_retval = retval;
        self->_finished.store(true, std::memory_order_release);
    }
    self->_join_cv.notify_all();
}

static void RunThread(std::unique_ptr<ThreadArgs> args) noexcept {
    const auto entry = args->entry;
    void* arg = args->arg;
    PthreadPrivate* self = args->self;
    currentThread = self;
    threadFinishing = false;
    // First entry claims the compact tid (cold path). On Windows the tid was
    // pre-allocated at creation and adopted in StartNativeThread on this same
    // thread, so this is a no-op there; on POSIX adopt here.
    if (self->guestTid == 0) {
        self->guestTid = GuestTid::Ensure();
    } else {
        GuestTid::Adopt(self->guestTid);
    }
    void* ret = nullptr;
    if (entry)
        ret = entry(arg);
    finishThread(self, ret);
    // Detached threads recycle on exit; joinable threads transfer ownership
    // to join (which recycles after WaitForSingleObject).
    if (self->_detached.load(std::memory_order_acquire) && self->guestTid != 0) {
        GuestTid::Recycle(self->guestTid);
        self->guestTid = 0;
    }
    currentThread = nullptr;
}

#ifdef _WIN32
static void ReleaseThread(PthreadPrivate* thread) noexcept {
    // An adopted handle is never reference-counted: it has no native handle to
    // close and must not be freed here (e.g. when an adopted host thread calls
    // scePthreadExit).
    if (thread->adopted)
        return;
    if (thread->references.fetch_sub(1, std::memory_order_acq_rel) != 1)
        return;
    CloseHandle(thread->nativeHandle);
    // Joinable handle destroyed without join (should not happen; join
    // recycles the tid). If guestTid is still set, recycle now to avoid leak.
    if (!thread->_detached && thread->guestTid != 0) {
        GuestTid::Recycle(thread->guestTid);
        thread->guestTid = 0;
    }
    delete thread;
}

struct NativeThreadArgs {
    std::unique_ptr<ThreadArgs> guest;
    std::future<bool> start;
    std::promise<void> initialized;
};

static unsigned __stdcall StartNativeThread(void* opaque) {
    std::unique_ptr<NativeThreadArgs> args(static_cast<NativeThreadArgs*>(opaque));
    auto* self = args->guest->self;
    try {
        ULONG_PTR low = 0;
        ULONG_PTR high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        if (high <= low || high - low < self->stackSize)
            throw 1;
        self->stackAddress = reinterpret_cast<void*>(high - self->stackSize);
        for (auto cursor = high - self->stackSize; cursor < high;) {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT || memory.Protect != PAGE_READWRITE || memory.RegionSize == 0)
                throw 2;
            cursor = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        }
        self->threadId = std::this_thread::get_id();
        currentThread = self;
        threadFinishing = false;
        // Adopt the pre-allocated compact tid (cold path, entry).
        if (self->guestTid != 0)
            GuestTid::Adopt(self->guestTid);
        else
            self->guestTid = GuestTid::Ensure();
        if (self->guestTid == 0)
            throw 3;  // >2^24 live threads.
        ApplyPriority(GetCurrentThread(), self->schedPriority);
        args->initialized.set_value();
    } catch (...) {
        try {
            args->initialized.set_exception(std::current_exception());
        } catch (...) {
        }
        return 0;
    }
    if (!args->start.get())
        return 0;
    auto guest = std::move(args->guest);
    args.reset();
    RunThread(std::move(guest));
    currentThread = nullptr;
    threadFinishing = false;
    ReleaseThread(self);
    return 0;
}
#endif

extern "C" {

/**
 * @brief scePthreadCreate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry,
                               void* arg, const char* name) noexcept {
    if (!thread || !entry)
        return SCE_KERNEL_ERROR_EINVAL;
    if (attr && !*attr)
        return SCE_KERNEL_ERROR_EINVAL;
    std::unique_ptr<PthreadPrivate> p;
    try {
        p = std::make_unique<PthreadPrivate>();
    } catch (const std::bad_alloc&) {
        return SCE_KERNEL_ERROR_ENOMEM;
    }
    if (name) {
        std::lock_guard<std::mutex> lk(p->_name_mtx);
        std::strncpy(p->threadName, name, sizeof(p->threadName) - 1);
    }
    bool detached = false;
    if (attr && *attr)
        detached = ((*attr)->_detachstate == DETACH_DETACHED);
    p->_detached.store(detached, std::memory_order_release);
    p->stackSize = (attr && *attr) ? (*attr)->_stacksize : DEFAULT_STACK_SIZE;
    p->guardSize = (attr && *attr) ? (*attr)->_guardsize : 4096;
    p->schedPriority.store((attr && *attr) ? (*attr)->_schedpriority : 700, std::memory_order_release);
    p->affinityMask.store((attr && *attr) ? (*attr)->_affinity : 0, std::memory_order_release);
    // Pre-allocate the compact tid for the child (cold path, without
    // touching the creator's thread_local). EAGAIN past 2^24 live threads.
    // Entry adopts it; join recycles it (joinable) or exit recycles it
    // (detached). This keeps tids compact and unique across guest + host
    // worker threads.
    p->guestTid = GuestTid::AllocateForThread();
    if (p->guestTid == 0)
        return SCE_KERNEL_ERROR_EAGAIN;
    std::promise<bool> start;
    std::unique_ptr<ThreadArgs> targs;
    try {
        targs = std::make_unique<ThreadArgs>(ThreadArgs{entry, arg, p.get()});
    } catch (const std::bad_alloc&) {
        GuestTid::Recycle(p->guestTid);
        return SCE_KERNEL_ERROR_ENOMEM;
    }
#ifdef _WIN32
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (p->stackSize < 16384 || p->stackSize % system.dwPageSize != 0 ||
        p->stackSize > std::numeric_limits<unsigned>::max()) {
        GuestTid::Recycle(p->guestTid);
        return SCE_KERNEL_ERROR_EINVAL;
    }
    auto native = std::make_unique<NativeThreadArgs>(NativeThreadArgs{std::move(targs), start.get_future(), {}});
    if (!native) {
        GuestTid::Recycle(p->guestTid);
        return SCE_KERNEL_ERROR_ENOMEM;
    }
    auto initialized = native->initialized.get_future();
    const auto handle = _beginthreadex(nullptr, static_cast<unsigned>(p->stackSize), StartNativeThread,
                                       native.get(), 0, nullptr);
    if (handle == 0) {
        GuestTid::Recycle(p->guestTid);
        p->guestTid = 0;
        return SCE_KERNEL_ERROR_EAGAIN;
    }
    p->nativeHandle = reinterpret_cast<void*>(handle);
    native.release();
    try {
        initialized.get();
    } catch (...) {
        start.set_value(false);
        WaitForSingleObject(p->nativeHandle, INFINITE);
        CloseHandle(p->nativeHandle);
        // Child never entered: its pre-allocated tid was never adopted.
        // StartNativeThread may have already adopted+recycled on stack-check
        // failure paths? No: adoption happens only on success; the exception
        // path returns before adoption, so recycle the pre-allocation here.
        // If adoption did happen then entry threw (tid==3 case), the entry
        // owns the tid and the exiting thread recycles it; avoid double
        // recycle by checking: entry adopts only on success, and this catch
        // means initialized.get() threw, i.e. entry never signaled success,
        // so the pre-allocation is still ours to recycle. Safe.
        GuestTid::Recycle(p->guestTid);
        p->guestTid = 0;
        return SCE_KERNEL_ERROR_EAGAIN;
    }
    auto* published = p.release();
    *thread = published;
    start.set_value(true);
    if (detached)
        ReleaseThread(published);
#else
    p->_thr = std::thread();
    try {
        p->_thr = std::thread([targs = std::move(targs), ready = start.get_future()]() mutable {
            if (ready.get())
                RunThread(std::move(targs));
        });
    } catch (...) {
        start.set_value(false);
        GuestTid::Recycle(p->guestTid);
        p->guestTid = 0;
        return SCE_KERNEL_ERROR_EAGAIN;
    }
    try {
        if (detached)
            p->_thr.detach();
    } catch (...) {
        start.set_value(false);
        try {
            if (p->_thr.joinable())
                p->_thr.join();
        } catch (...) {
        }
        GuestTid::Recycle(p->guestTid);
        p->guestTid = 0;
        return SCE_KERNEL_ERROR_EAGAIN;
    }
    *thread = p.release();
    start.set_value(true);
#endif
    return SCE_OK;
}

/**
 * @brief scePthreadJoin implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadJoin(Pthread thread, void** retval) noexcept {
    if (!thread)
        return SCE_KERNEL_ERROR_EINVAL;
    if (thread->_detached)
        return SCE_KERNEL_ERROR_EINVAL;
#ifdef _WIN32
    if (thread == currentThread)
        return SCE_KERNEL_ERROR_EINVAL;
    if (WaitForSingleObject(thread->nativeHandle, INFINITE) != WAIT_OBJECT_0)
        return SCE_KERNEL_ERROR_EINVAL;
    if (retval)
        *retval = thread->_retval;
    if (thread->guestTid != 0) {
        GuestTid::Recycle(thread->guestTid);
        thread->guestTid = 0;
    }
    ReleaseThread(thread);
#else
    if (thread->_thr.joinable()) {
        try {
            thread->_thr.join();
        } catch (...) {
            return SCE_KERNEL_ERROR_EINVAL;
        }
    }
    if (retval)
        *retval = thread->_retval;
    if (thread->guestTid != 0) {
        GuestTid::Recycle(thread->guestTid);
        thread->guestTid = 0;
    }
    delete thread;
#endif
    return SCE_OK;
}

/**
 * @brief scePthreadDetach implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadDetach(Pthread thread) noexcept {
    if (!thread)
        return SCE_KERNEL_ERROR_EINVAL;
    if (thread->_detached)
        return SCE_KERNEL_ERROR_EINVAL;
    thread->_detached = true;
#ifdef _WIN32
    // Tid recycles on thread exit (RunThread/StartNativeThread path), not
    // here: the thread may still run and own futex words under its tid.
    ReleaseThread(thread);
#else
    if (thread->_thr.joinable()) {
        try {
            thread->_thr.detach();
        } catch (...) {
            thread->_detached = false;
            return SCE_KERNEL_ERROR_EINVAL;
        }
    }
#endif
    return SCE_OK;
}

/**
 * @brief scePthreadExit implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
void APS5_VABI scePthreadExit(void* retval) noexcept {
    if (!currentThread) {
#ifdef _WIN32
        _endthreadex(0);
#else
        pthread_exit(retval);
#endif
        return;
    }
    auto* self = currentThread;
    finishThread(self, retval);
    currentThread = nullptr;
    threadFinishing = false;
    const bool detached = self->_detached;
    if (detached && self->guestTid != 0) {
        GuestTid::Recycle(self->guestTid);
        self->guestTid = 0;
    }
#ifdef _WIN32
    if (detached)
        ReleaseThread(self);
    _endthreadex(0);
#else
    pthread_exit(retval);
#endif
}

/**
 * @brief scePthreadSelf implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
Pthread APS5_VABI scePthreadSelf() noexcept {
    if (currentThread)
        return currentThread;
    // Host thread that never went through scePthreadCreate (the guest main
    // thread, driver workers): adopt a lazily created, detached handle so the
    // guest never sees a null "self" (scePthreadGetprio(scePthreadSelf(), ...)
    // and scePthreadRename(scePthreadSelf(), ...) are routine on the main
    // thread). AnyPS5 76b7f998. Allocation failure keeps the old null answer.
    //
    // Ownership: the handle lives in HostThreadLocal storage (an FLS slot whose
    // callback deletes it when the host thread exits). A C++ thread_local with
    // a non-trivial destructor (the first attempt used a unique_ptr) registers
    // through __cxa_thread_atexit, which libc.prx overrides
    // (CxxAbiSupport.cpp); that hook has the destruction-order/concurrency
    // hazard GuestTid.cpp already documents, and the adopted-handle test
    // segfaulted intermittently (~1 in 40 ctest repeats). FLS callbacks are
    // the project's established answer (HostThreadLocal, GuestTid). The
    // handle is only ever dereferenced by its own thread or via a guest that
    // kept it, and is invalid once the thread is gone, like any pthread_t.
    struct AdoptedTag {};
    PthreadPrivate* handle = nullptr;
    try {
        handle = &HostThreadLocal<PthreadPrivate, AdoptedTag>();
    } catch (...) {
        return nullptr;  // slot exhaustion / OOM keeps the old null answer.
    }
    handle->adopted = true;
    handle->_detached.store(true, std::memory_order_release);  // join/detach -> EINVAL.
#ifdef _WIN32
    // threadId exists only in the Windows layout of PthreadPrivate (Pthread.hpp);
    // the POSIX layout identifies the thread through std::thread instead.
    handle->threadId = std::this_thread::get_id();
#endif
    handle->guestTid = GuestTid::Ensure();
    // Ensure() returns 0 when the compact tid allocator is exhausted (>2^24
    // live threads), the same condition scePthreadCreate reports as EAGAIN. A
    // handle with tid 0 would own no futex word, so keep the old null answer
    // and do NOT publish it as currentThread: the next call retries once a tid
    // has been recycled.
    if (handle->guestTid == 0)
        return nullptr;
    currentThread = handle;
    return currentThread;
}

/**
 * @brief scePthreadYield implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
void APS5_VABI scePthreadYield() noexcept {
    // Both yield paths call SwitchToThread (spec): it yields to a ready
    // same-priority thread without the variable sleep of std::yield.
#ifdef _WIN32
    SwitchToThread();
#else
    std::this_thread::yield();
#endif
}

/**
 * @brief scePthreadCancel implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadCancel(Pthread thread) noexcept {
    (void)thread;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief scePthreadEqual implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadEqual(Pthread thread1, Pthread thread2) noexcept {
    return (thread1 == thread2) ? 1 : 0;
}

/**
 * @brief scePthreadGetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadGetaffinity(Pthread thread, KernelCpumask* mask) noexcept {
    if (!mask)
        return SCE_KERNEL_ERROR_EINVAL;
    if (!thread) {
        // Current thread's recorded mask.
        if (!currentThread)
            return SCE_KERNEL_ERROR_ESRCH;
        *mask = currentThread->affinityMask;
        return SCE_OK;
    }
    *mask = thread->affinityMask;
    return SCE_OK;
}

/**
 * @brief scePthreadGetname implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadGetname(Pthread thread, char* name) noexcept {
    if (!name)
        return SCE_KERNEL_ERROR_EINVAL;
    PthreadPrivate* self = thread ? thread : currentThread;
    if (!self)
        return SCE_KERNEL_ERROR_ESRCH;
    std::lock_guard<std::mutex> lk(self->_name_mtx);
    std::memcpy(name, self->threadName, sizeof(self->threadName));
    return SCE_OK;
}

/**
 * @brief scePthreadGetprio implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadGetprio(Pthread thread, int* prio) noexcept {
    if (!prio)
        return SCE_KERNEL_ERROR_EINVAL;
    PthreadPrivate* self = thread ? thread : currentThread;
    if (!self)
        return SCE_KERNEL_ERROR_ESRCH;
    *prio = self->schedPriority;
    return SCE_OK;
}

/**
 * @brief scePthreadGetthreadid implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadGetthreadid(void) noexcept {
    // Compact tid in [1, 2^24): owner field + Getthreadid value (spec).
    const std::uint32_t tid = GuestTid::Ensure();
    return static_cast<int>(tid);
}

/**
 * @brief scePthreadRename implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadRename(Pthread thread, const char* name) noexcept {
    PthreadPrivate* self = thread ? thread : currentThread;
    if (!self || !name)
        return SCE_KERNEL_ERROR_EINVAL;
    std::lock_guard<std::mutex> lk(self->_name_mtx);
    std::memset(self->threadName, 0, sizeof(self->threadName));
    std::strncpy(self->threadName, name, sizeof(self->threadName) - 1);
    return SCE_OK;
}

/**
 * @brief scePthreadSetaffinity implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadSetaffinity(Pthread thread, KernelCpumask mask) noexcept {
    // Recorded, not applied: guest masks name console cores, not host cores.
    PthreadPrivate* self = thread ? thread : currentThread;
    if (!self)
        return SCE_KERNEL_ERROR_ESRCH;
    self->affinityMask.store(mask, std::memory_order_release);
    return SCE_OK;
}

/**
 * @brief scePthreadSetcancelstate implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadSetcancelstate(int state, int* old_state) noexcept {
    (void)state;
    (void)old_state;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief scePthreadSetcanceltype implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadSetcanceltype(int type, int* old_type) noexcept {
    (void)type;
    (void)old_type;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

/**
 * @brief scePthreadSetprio implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
int APS5_VABI scePthreadSetprio(Pthread thread, int prio) noexcept {
    PthreadPrivate* self = thread ? thread : currentThread;
    if (!self)
        return SCE_KERNEL_ERROR_ESRCH;
    self->schedPriority.store(prio, std::memory_order_release);
#ifdef _WIN32
    void* handle = self->nativeHandle ? self->nativeHandle : GetCurrentThread();
    // Never a realtime class: bands only (see MapPriorityToWin32).
    ApplyPriority(handle, prio);
#endif
    return SCE_OK;
}

}

extern "C" {

/**
 * @brief __pthread_cxa_finalize_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return Status or error code.
 */
void APS5_VABI __pthread_cxa_finalize_nid_postfix(void* argument) noexcept {
    (void)argument;
    NotImplemented_nid_no_patch(__func__);
}

}
