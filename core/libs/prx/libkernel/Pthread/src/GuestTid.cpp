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

thread_local std::uint32_t t_tid = 0;

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
    const std::uint32_t tid = g_next.fetch_add(1, std::memory_order_relaxed);
    if (tid > GuestTid::kMaxTid || tid < GuestTid::kMinTid)
        return 0;  // Exhausted (>2^24 live): caller returns EAGAIN.
    g_live.fetch_add(1, std::memory_order_relaxed);
    return tid;
}

}  // namespace

std::uint32_t Ensure() noexcept {
    if (t_tid != 0)
        return t_tid;
    const std::uint32_t tid = Allocate();
    if (tid != 0)
        t_tid = tid;
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
    if (t_tid == tid)
        return;
    if (t_tid != 0)
        Recycle(t_tid);
    t_tid = tid;
}

void Recycle(std::uint32_t tid) noexcept {
    if (tid < GuestTid::kMinTid || tid > GuestTid::kMaxTid)
        return;
    if (t_tid == tid)
        t_tid = 0;
    {
        std::lock_guard<std::mutex> lk(g_freeMutex);
        g_free.push_back(tid);
    }
    g_live.fetch_sub(1, std::memory_order_relaxed);
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
