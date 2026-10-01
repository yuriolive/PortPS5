// PortPS5 libkernel guest signal registration and mask emulation.
//
// Subsystem: libkernel System. Guest handlers are dispatched from host CRT
// signals (only the six signals in NativeSignal are mapped); the blocked mask
// is process-global state guarded by `registration` for read-modify-write
// sequences. Every export is APS5_VABI (System V ABI); errors are reported as
// -1 / SIG_ERR with errno 22 (EINVAL) in the guest errno slot, never thrown.
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <atomic>
#include <csignal>
#include <cstdint>
#include <mutex>

/** Return the address of the calling thread guest errno slot. */
extern "C" int* APS5_VABI __error_nid_postfix();
namespace {
using GuestHandler = void (APS5_VABI *)(int);
std::atomic<GuestHandler> handlers[32]{};
static_assert(std::atomic<GuestHandler>::is_always_lock_free);
std::atomic<std::uint32_t> blockedMask{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
std::mutex registration;
int NativeSignal(int guest) {
    switch (guest) {
        case 2: return SIGINT;
        case 4: return SIGILL;
        case 6: return SIGABRT;
        case 8: return SIGFPE;
        case 11: return SIGSEGV;
        case 15: return SIGTERM;
        default: return 0;
    }
}
void Dispatch(int native) {
    int guest = 0;
    for (int candidate : {2, 4, 6, 8, 11, 15})
        if (NativeSignal(candidate) == native) { guest = candidate; break; }
    if (!guest) return;
#ifdef _WIN32
    // Preserve the guest's persistent registration across CRT delivery.
    std::signal(native, Dispatch);
#endif
    if ((blockedMask.load() & (1u << guest)) != 0) return;
    const auto callback = handlers[guest].load();
    if (reinterpret_cast<std::uintptr_t>(callback) > 1) callback(guest);
}
}

struct GuestSignalSet {
    std::uint32_t bits[4];
};

extern "C" {
/** Register a guest handler. @return the previous handler, or SIG_ERR (-1) with errno EINVAL for an unmapped signal or a SIG_ERR handler. */
GuestHandler APS5_VABI signal_nid_postfix(int guest, GuestHandler handler) {
    const auto invalid = reinterpret_cast<GuestHandler>(static_cast<std::uintptr_t>(-1));
    const int native = NativeSignal(guest);
    if (!native || handler == invalid) { *__error_nid_postfix() = 22; return invalid; }
    std::lock_guard lock(registration);
    const auto previous = handlers[guest].exchange(handler);
    const auto address = reinterpret_cast<std::uintptr_t>(handler);
    auto hostHandler = address == 0 ? SIG_DFL : address == 1 ? SIG_IGN : Dispatch;
    if (std::signal(native, hostHandler) == SIG_ERR) {
        handlers[guest].store(previous);
        *__error_nid_postfix() = 22;
        return invalid;
    }
    return previous;
}
/** Raise a mapped signal on the host. @return 0, or -1 with errno EINVAL. */
int APS5_VABI raise_nid_postfix(int guest) {
    const int native = NativeSignal(guest);
    if (!native) { *__error_nid_postfix() = 22; return -1; }
    const int result = std::raise(native);
    if (result) *__error_nid_postfix() = 22;
    return result ? -1 : 0;
}
// SIG_BLOCK=1, SIG_UNBLOCK=2, SIG_SETMASK=3 (FreeBSD). An unknown `how` is a
// guest error a real kernel reports as EINVAL with -1, not a host exception:
// a throw here would cross the APS5_VABI boundary. Validation happens before
// any write so a rejected call leaves both the mask and *previousSet untouched.
int APS5_VABI _sigprocmask_nid_postfix(int how, const GuestSignalSet* set, GuestSignalSet* previousSet) {
    if (set != nullptr && (how < 1 || how > 3)) {
        *__error_nid_postfix() = 22;
        return -1;
    }
    std::lock_guard lock(registration);
    if (previousSet != nullptr) {
        previousSet->bits[0] = blockedMask.load();
        previousSet->bits[1] = 0;
        previousSet->bits[2] = 0;
        previousSet->bits[3] = 0;
    }
    if (set != nullptr) {
        switch (how) {
            case 1: blockedMask.fetch_or(set->bits[0]); break;
            case 2: blockedMask.fetch_and(~set->bits[0]); break;
            default: blockedMask.store(set->bits[0]); break;
        }
    }
    return 0;
}

// libc-level sigprocmask shares the syscall-level implementation (the two NIDs
// are the same operation; AnyPS5 c6d098d4 moved it here from the Socket stub
// that aborted on every call).
int APS5_VABI sigprocmask_nid_postfix(int how, const void* set, void* previousSet) {
    return _sigprocmask_nid_postfix(how, static_cast<const GuestSignalSet*>(set),
                                    static_cast<GuestSignalSet*>(previousSet));
}
}

extern "C" {

/** Not implemented; aborts via NotImplemented_nid_no_patch, so it never returns. */
int APS5_VABI _is_signal_return_nid_postfix(std::uint64_t programCounter) {
    (void)programCounter;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
