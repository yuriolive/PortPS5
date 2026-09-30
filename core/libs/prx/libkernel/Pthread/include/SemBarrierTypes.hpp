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
    explicit PthreadSemPrivate(std::uint32_t value) noexcept : count(value), waiters(0) {}
};
static_assert(sizeof(std::atomic<std::uint32_t>) == 4, "WaitOnAddress needs a 4-byte word");

// Host object behind a pthread_barrier_t slot.
struct PthreadBarrierPrivate {
    std::atomic<std::uint64_t> state{0};   // generation << 32 | arrived.
    std::atomic<std::uint32_t> active{0};  // threads currently inside wait.
    const std::uint32_t count;             // arrivals that release a round.
    explicit PthreadBarrierPrivate(std::uint32_t n) noexcept : count(n) {}
};

#endif
