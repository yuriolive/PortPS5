// PortPS5 libkernel: pthread_once / scePthreadOnce.
//
// Subsystem: libkernel threading (docs/spec/threading.md). Both entry points
// share one futex-word implementation over the guest's `once` control block, so
// there is no process-global std::mutex / condition variable (the previous
// pthread_once serialised every once-control in the process through one lock).
//
// Guest ABI: FreeBSD pthread_once_t is {int32 state; void* mutex}; state 0 =
// never run, 1 = done, 2 = running. scePthreadOnce receives the same control
// block (int32_t*), so both functions operate on the state word at offset 0.
//
// Ordering: the claim is a CAS 0 -> 2 (acq_rel); completion is a release store
// of 1 followed by WakeAll, so a waiter that observes 1 with an acquire load
// also observes every write the initializer made. Waiters re-check the word
// after every wake (spurious wakes are allowed).
//
// Initializers may run guest code that initialises other once controls, so the
// initializer is never called under any host lock. If the initializer unwinds
// (a guest C++ exception crossing this frame) the state is reset to 0 so a
// later caller retries, sleepers are woken, and the exception continues.
//
// History: AnyPS5 e764385d removed a NotImplemented scePthreadOnce stub that
// shadowed the working export because the NID patcher binds the NID to the
// postfixed symbol. Our tree had only that stub and no implementation, so this
// file replaces it with the real one under the plain (non-postfixed) name.

#include "prx/libc/include/General.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/Pthread/include/FutexCore.hpp"
#include "prx/libkernel/Pthread/include/SyncWords.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace {

// FreeBSD guest ABI: PTHREAD_ONCE_INIT is {0, nullptr}; completed state is 1.
struct GuestOnce {
    std::int32_t state;
    void* mutex;
};
static_assert(offsetof(GuestOnce, state) == 0, "scePthreadOnce passes &state");
static_assert(offsetof(GuestOnce, mutex) == 8 && sizeof(GuestOnce) == 16);

constexpr std::int32_t kNever = 0;
constexpr std::int32_t kDone = 1;
constexpr std::int32_t kRunning = 2;

// Runs `initialize` exactly once per control block.
// Returns true on success (initializer ran or had already completed) and false
// when the state word holds a value that is not 0/1/2 (corrupt or
// uninitialised-garbage control block: the caller reports EINVAL).
// Not noexcept on purpose: a guest exception from `initialize` must keep
// unwinding through this frame (see file header).
bool RunOnce(std::int32_t* stateWord, void(APS5_VABI* initialize)()) {
    std::atomic_ref<std::int32_t> state(*stateWord);
    for (;;) {
        std::int32_t observed = state.load(std::memory_order_acquire);
        if (observed == kDone)
            return true;
        if (observed == kNever) {
            std::int32_t expected = kNever;
            if (!state.compare_exchange_strong(expected, kRunning, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
                continue;  // Lost the claim race: re-read and wait or finish.
            try {
                initialize();
            } catch (...) {
                state.store(kNever, std::memory_order_release);
                FutexCore::WakeAll(stateWord);
                throw;
            }
            state.store(kDone, std::memory_order_release);
            FutexCore::WakeAll(stateWord);
            return true;
        }
        if (observed != kRunning)
            return false;
        // Another thread is running the initializer: sleep while it still is.
        FutexCore::WaitU32(reinterpret_cast<volatile std::uint32_t*>(stateWord),
                           static_cast<std::uint32_t>(kRunning), FutexCore::Deadline{});
    }
}

}  // namespace

/**
 * @brief pthread_once_nid_postfix implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return 0, or EINVAL (null control block, null routine, corrupt state).
 */
extern "C" int APS5_VABI pthread_once_nid_postfix(GuestOnce* control,
                                                  void(APS5_VABI* initialize)()) {
    if (!control || !initialize)
        return SyncWords::kPosixEinval;
    return RunOnce(&control->state, initialize) ? 0 : SyncWords::kPosixEinval;
}

/**
 * @brief scePthreadOnce implementation.
 * Invoked by guest code using System V ABI calling convention.
 * @return 0, or SCE_KERNEL_ERROR_EINVAL (null control block, null routine,
 *         corrupt state).
 */
extern "C" int APS5_VABI scePthreadOnce(std::int32_t* once, void(APS5_VABI* initialize)()) {
    if (!once || !initialize)
        return SyncWords::kSceEinval;
    return RunOnce(once, initialize) ? SyncWords::kSceOk : SyncWords::kSceEinval;
}
