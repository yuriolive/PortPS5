// PortPS5 libkernel synchronization and threading subsystem.
// Implements guest threading and synchronization primitives with System V ABI invariants.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_GUESTTID_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_GUESTTID_HPP

// Compact guest tids in [1, 2^24) (docs/spec/threading.md Target design).
// Why compact: the tid is the owner field in every futex word (24 bits), and
// the value scePthreadGetthreadid returns. It covers guest threads and host
// threads that call into the guest (e.g. driver workers), so any thread that
// touches a sync word lazily owns one via EnsureGuestTid().
// Ordering: allocation is a cold path (thread entry); a small mutex guards
// the free list only. Hot paths (lock/unlock) only read the thread_local.

#include <cstdint>
#include <mutex>
#include <vector>
#include <atomic>

namespace GuestTid {

inline constexpr std::uint32_t kMinTid = 1u;
inline constexpr std::uint32_t kMaxTid = 0xFFFFFFu;  // 2^24 - 1.

// Allocator state. Lives in GuestTid.cpp; the free list is guarded by a
// cold-path-only mutex (never held across a guest wait).
std::uint32_t Ensure() noexcept;
void Recycle(std::uint32_t tid) noexcept;
// Pre-allocate a tid for a child thread handle without touching the
// creator's thread_local (creation is cold; entry adopts it). Returns 0 on
// exhaustion (caller maps to EAGAIN).
std::uint32_t AllocateForThread() noexcept;
// Adopt a pre-allocated handle tid into this thread's thread_local (entry).
// The tid is already counted, so this never bumps the allocator.
void Adopt(std::uint32_t tid) noexcept;
// Recycle the calling thread's tid when it was heap-allocated for a detached
// exit. Joinable threads recycle via their Pthread handle (see Thread.cpp),
// so the exiting thread must NOT recycle there (ownership transfers to join).
void RecycleSelf() noexcept;
// Test hook: how many tids are currently live (for uniqueness checks).
std::uint32_t LiveCount() noexcept;

}  // namespace GuestTid

#endif
