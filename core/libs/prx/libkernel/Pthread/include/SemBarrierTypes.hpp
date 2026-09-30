// PortPS5 libkernel: host objects behind pthread semaphore and barrier slots.
//
// Subsystem: libkernel threading (docs/spec/threading.md, "Handle objects").
// Kept in a header only so unit tests can observe the waiter counters and wait
// deterministically for a thread to be parked (instead of sleeping and hoping);
// production code touches the words solely from Pthread/src/Sem.cpp and
// Pthread/Posix/Barrier.cpp, which keeps the ordering argument in those files
// local. All members are lock-free atomics accessed only through std::atomic.

#ifndef CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_SEMBARRIERTYPES_HPP
#define CORE_LIBS_PRX_LIBKERNEL_PTHREAD_INCLUDE_SEMBARRIERTYPES_HPP

#include <atomic>
#include <cstdint>

// Host object behind a PthreadSem slot (declared in SceTypes.hpp).
struct PthreadSemPrivate {
    std::atomic<std::uint32_t> count;    // available tokens; the futex word.
    std::atomic<std::uint32_t> waiters;  // threads inside a blocking wait.
    // Posts in flight. A post touches the object after publishing the token
    // (it reads `waiters` and wakes), and a consumer can take that token and
    // destroy the semaphore in the meantime (the completion-semaphore pattern:
    // worker posts, owner waits then destroys). Destroy therefore waits for
    // this to drain before freeing.
    std::atomic<std::uint32_t> posting;
    explicit PthreadSemPrivate(std::uint32_t value) noexcept
        : count(value), waiters(0), posting(0) {}
};

// Test seam (defined in Sem.cpp, exported from libkernel so a test executable
// and the DLL share ONE hook variable; an inline variable here would be
// duplicated per module): when a non-null hook is installed, scePthreadSemPost
// calls it after publishing the token and before it reads `waiters`, i.e.
// inside the window where the object must stay alive. Production never sets it.
extern "C" void PthreadSemSetPostWindowHook(void (*hook)());

// Host object behind a pthread_barrier_t slot.
struct PthreadBarrierPrivate {
    std::atomic<std::uint64_t> state{0};   // generation << 32 | arrived.
    std::atomic<std::uint32_t> active{0};  // threads currently inside wait.
    const std::uint32_t count;             // arrivals that release a round.
    explicit PthreadBarrierPrivate(std::uint32_t n) noexcept : count(n) {}
};

#endif
