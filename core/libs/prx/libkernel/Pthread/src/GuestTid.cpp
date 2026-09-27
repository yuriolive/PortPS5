#ifdef _WIN32
#include <windows.h>
#include <fibersapi.h>
#else
#include <pthread.h>
#endif

#include "prx/libkernel/Pthread/include/GuestTid.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace GuestTid {
namespace {

// Why a free list + bump allocator: tids must be compact (24 bits) and
// recycled after join/detach-exit. Allocation is cold (thread entry/first
// futex touch), so a single mutex here is fine; it is never held across a
// guest wait. Hot paths only read the thread_local below.
std::atomic<std::uint32_t> g_next{GuestTid::kMinTid};
std::mutex g_freeMutex;
std::vector<std::uint32_t> g_free;
std::atomic<std::uint32_t> g_live{0};

// Trivial POD types for TLS avoid compiler-generated __cxa_thread_atexit registration,
// which has concurrency and destruction-order bugs under MinGW-w64 on Windows.
thread_local std::uint32_t t_tid = 0;
thread_local bool t_ownedByHandle = false;

#ifdef _WIN32
static DWORD g_flsIndex = FLS_OUT_OF_INDEXES;
static std::once_flag g_flsOnce;

static VOID NTAPI FlsCleanup(PVOID lpFlsData) {
    const auto tid = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(lpFlsData));
    if (tid != 0) {
        GuestTid::Recycle(tid);
    }
}

static void RegisterCleanup(std::uint32_t tid) noexcept {
    std::call_once(g_flsOnce, [] {
        g_flsIndex = FlsAlloc(FlsCleanup);
    });
    if (g_flsIndex != FLS_OUT_OF_INDEXES) {
        FlsSetValue(g_flsIndex, reinterpret_cast<PVOID>(static_cast<std::uintptr_t>(tid)));
    }
}

static void UnregisterCleanup() noexcept {
    if (g_flsIndex != FLS_OUT_OF_INDEXES) {
        FlsSetValue(g_flsIndex, nullptr);
    }
}
#else
static pthread_key_t g_pthreadKey;
static std::once_flag g_pthreadOnce;

static void PosixCleanup(void* data) {
    const auto tid = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data));
    if (tid != 0) {
        GuestTid::Recycle(tid);
    }
}

static void RegisterCleanup(std::uint32_t tid) noexcept {
    std::call_once(g_pthreadOnce, [] {
        pthread_key_create(&g_pthreadKey, PosixCleanup);
    });
    pthread_setspecific(g_pthreadKey, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tid)));
}

static void UnregisterCleanup() noexcept {
    pthread_setspecific(g_pthreadKey, nullptr);
}
#endif

std::uint32_t Allocate() noexcept {
    {
        std::lock_guard<std::mutex> lk(g_freeMutex);
        if (!g_free.empty()) {
            const std::uint32_t tid = g_free.back();
            g_free.pop_back();
            g_live.fetch_add(1, std::memory_order_relaxed);
            return tid;
        }
    }
    std::uint32_t tid = g_next.load(std::memory_order_relaxed);
    for (;;) {
        if (tid > GuestTid::kMaxTid || tid < GuestTid::kMinTid)
            return 0;  // Exhausted (>2^24 live): caller returns EAGAIN.
        if (g_next.compare_exchange_weak(tid, tid + 1,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
            g_live.fetch_add(1, std::memory_order_relaxed);
            return tid;
        }
    }
}

}  // namespace

std::uint32_t Ensure() noexcept {
    if (t_tid != 0)
        return t_tid;
    const std::uint32_t tid = Allocate();
    if (tid != 0) {
        t_tid = tid;
        t_ownedByHandle = false;
        RegisterCleanup(tid);
    }
    return tid;
}

std::uint32_t AllocateForThread() noexcept {
    return Allocate();
}

void Adopt(std::uint32_t tid) noexcept {
    if (tid < GuestTid::kMinTid || tid > GuestTid::kMaxTid)
        return;
    // New thread entry: thread_local is 0, so adopt without extra counting
    // (AllocateForThread already counted). If this thread somehow already
    // owns a different tid (reuse of a host worker thread for a guest
    // entry), recycle the old one to keep the live count exact.
    if (t_tid == tid) {
        t_ownedByHandle = true;
        UnregisterCleanup();
        return;
    }
    if (t_tid != 0)
        Recycle(t_tid);
    t_tid = tid;
    t_ownedByHandle = true;
    UnregisterCleanup();
}

void Recycle(std::uint32_t tid) noexcept {
    if (tid < GuestTid::kMinTid || tid > GuestTid::kMaxTid)
        return;
    if (t_tid == tid) {
        t_tid = 0;
        t_ownedByHandle = false;
        UnregisterCleanup();
    }
    try {
        std::lock_guard<std::mutex> lk(g_freeMutex);
        for (std::uint32_t freeTid : g_free) {
            if (freeTid == tid)
                return;  // Already recycled; avoid duplicate free-list entries.
        }
        g_free.push_back(tid);
        g_live.fetch_sub(1, std::memory_order_relaxed);
    } catch (const std::bad_alloc&) {}
}

void RecycleSelf() noexcept {
    if (t_tid == 0)
        return;
    Recycle(t_tid);
}

std::uint32_t LiveCount() noexcept {
    return g_live.load(std::memory_order_relaxed);
}

}  // namespace GuestTid
