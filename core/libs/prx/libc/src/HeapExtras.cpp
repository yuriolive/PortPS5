// C++ allocation functions (operator new/delete family), std::nothrow / new_handler support and the C11
// aligned_alloc / BSD reallocalign entry points, all backed by the application heap.
//
// Subsystem: libc.prx heap front-ends. Every allocation goes through ApplicationHeap*_nid_no_patch so that
// memory from `operator new`, `malloc` and `memalign` is interchangeable, exactly as in the PS5 libc where
// they share one heap (and where a title's allocator replacement table redirects all of them).
//
// Failure policy (docs/spec rule: host exceptions must not reach guest frames):
//   * nothrow variants return nullptr when the heap is exhausted or the alignment is invalid;
//   * throwing variants run the registered new_handler in a loop (standard behaviour) and, when there is
//     none or it returns without freeing memory forever, end in the logging abort `Unsupported`. They do not
//     throw std::bad_alloc: a host exception object thrown from libc.prx has host typeinfo, which a guest
//     `catch (std::bad_alloc&)` would not match, and a guest catch(...) would silently swallow it.
//
// Ported/adapted from AnyPS5 a599eca7 (operator new/delete, aligned_alloc, reallocalign). Differences from
// upstream: nothrow variants really are nothrow, the new_handler is honoured, aligned new/delete use the
// application heap (upstream released aligned blocks with the guest heap, a different allocator), and all
// nothrow/aligned delete overloads are provided.
//
// Threading: the allocator callbacks enforce their own locking; the new_handler slot is an atomic.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <stdexcept>

#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/General.hpp"

// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
extern "C" int* APS5_VABI __error_nid_postfix();

namespace {

constexpr int GuestEinval = 22;
constexpr int GuestEnomem = 12;

using GuestNewHandler = void (APS5_VABI *)();

std::atomic<GuestNewHandler> g_newHandler{nullptr};

// One allocation attempt. Returns nullptr on exhaustion or invalid alignment. Only the heap's own
// failure types are absorbed; a broken/unregistered allocator (runtime_error) is an unsupported state.
void* TryAllocate(std::size_t size, std::size_t alignment) {
    try {
        // operator new(0) must return a unique non-null pointer.
        const std::size_t bytes = size == 0 ? 1 : size;
        return alignment == 0 ? ApplicationHeapAllocate_nid_no_patch(bytes) : ApplicationHeapAlign_nid_no_patch(alignment, bytes);
    } catch (const std::bad_alloc&) {
        return nullptr;
    } catch (const std::invalid_argument&) {
        return nullptr;  // non-power-of-two alignment
    } catch (const std::exception& error) {
        Unsupported(error.what());
    }
}

// Throwing-new core: retry through the new_handler until it is absent, then abort (see file header).
void* AllocateOrAbort(std::size_t size, std::size_t alignment) {
    // An invalid alignment can never be fixed by a new_handler; fail fast instead of retrying forever.
    if (alignment != 0 && (alignment & (alignment - 1)) != 0) Unsupported("operator new: alignment is not a power of two");
    for (;;) {
        if (void* pointer = TryAllocate(size, alignment)) return pointer;
        const GuestNewHandler handler = g_newHandler.load();
        if (handler == nullptr) Unsupported("operator new: out of memory and no new_handler installed");
        handler();
    }
}

// aligned_alloc/reallocalign core: a POSIX-style front end that reports failure by nullptr + guest errno
// (EINVAL for a non-power-of-two alignment, ENOMEM for exhaustion) instead of throwing into the guest.
void* AlignedOrNull(std::size_t size, std::size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        *__error_nid_postfix() = GuestEinval;
        return nullptr;
    }
    void* pointer = TryAllocate(size, alignment);
    if (pointer == nullptr) *__error_nid_postfix() = GuestEnomem;
    return pointer;
}

void Release(void* pointer) {
    if (pointer != nullptr) ApplicationHeapFree_nid_no_patch(pointer);
}

}

extern "C" {

/// `std::nothrow` tag object (only its address is used by callers).
unsigned char _ZSt7nothrow_nid_postfix = 0;

/// std::set_new_handler: installs `handler` and returns the previous one (null if none).
GuestNewHandler APS5_VABI _ZSt15set_new_handlerPFvvE_nid_postfix(GuestNewHandler handler) {
    return g_newHandler.exchange(handler);
}

/// std::get_new_handler.
GuestNewHandler APS5_VABI _ZSt15get_new_handlerv_nid_postfix() {
    return g_newHandler.load();
}

/// operator new(size_t): never returns null; aborts (logged) on exhaustion without a new_handler.
void* APS5_VABI _Znwm_nid_postfix(std::size_t size) { return AllocateOrAbort(size, 0); }
/// operator new[](size_t).
void* APS5_VABI _Znam_nid_postfix(std::size_t size) { return AllocateOrAbort(size, 0); }
/// operator new(size_t, const std::nothrow_t&): returns nullptr on exhaustion.
void* APS5_VABI _ZnwmRKSt9nothrow_t_nid_postfix(std::size_t size, const void*) { return TryAllocate(size, 0); }
/// operator new[](size_t, const std::nothrow_t&).
void* APS5_VABI _ZnamRKSt9nothrow_t_nid_postfix(std::size_t size, const void*) { return TryAllocate(size, 0); }
/// operator new(size_t, std::align_val_t): alignment must be a power of two or the process aborts (logged).
void* APS5_VABI _ZnwmSt11align_val_t_nid_postfix(std::size_t size, std::size_t alignment) { return AllocateOrAbort(size, alignment == 0 ? 1 : alignment); }
/// operator new[](size_t, std::align_val_t).
void* APS5_VABI _ZnamSt11align_val_t_nid_postfix(std::size_t size, std::size_t alignment) { return AllocateOrAbort(size, alignment == 0 ? 1 : alignment); }
/// operator new(size_t, std::align_val_t, const std::nothrow_t&).
void* APS5_VABI _ZnwmSt11align_val_tRKSt9nothrow_t_nid_postfix(std::size_t size, std::size_t alignment, const void*) { return TryAllocate(size, alignment == 0 ? 1 : alignment); }
/// operator new[](size_t, std::align_val_t, const std::nothrow_t&).
void* APS5_VABI _ZnamSt11align_val_tRKSt9nothrow_t_nid_postfix(std::size_t size, std::size_t alignment, const void*) { return TryAllocate(size, alignment == 0 ? 1 : alignment); }

/// operator delete(void*): null is a no-op.
void APS5_VABI _ZdlPv_nid_postfix(void* pointer) { Release(pointer); }
/// operator delete[](void*).
void APS5_VABI _ZdaPv_nid_postfix(void* pointer) { Release(pointer); }
/// operator delete(void*, size_t) (sized deallocation; the size is advisory).
void APS5_VABI _ZdlPvm_nid_postfix(void* pointer, std::size_t) { Release(pointer); }
/// operator delete[](void*, size_t).
void APS5_VABI _ZdaPvm_nid_postfix(void* pointer, std::size_t) { Release(pointer); }
/// operator delete(void*, const std::nothrow_t&).
void APS5_VABI _ZdlPvRKSt9nothrow_t_nid_postfix(void* pointer, const void*) { Release(pointer); }
/// operator delete[](void*, const std::nothrow_t&).
void APS5_VABI _ZdaPvRKSt9nothrow_t_nid_postfix(void* pointer, const void*) { Release(pointer); }
/// operator delete(void*, std::align_val_t).
void APS5_VABI _ZdlPvSt11align_val_t_nid_postfix(void* pointer, std::size_t) { Release(pointer); }
/// operator delete[](void*, std::align_val_t).
void APS5_VABI _ZdaPvSt11align_val_t_nid_postfix(void* pointer, std::size_t) { Release(pointer); }
/// operator delete(void*, size_t, std::align_val_t).
void APS5_VABI _ZdlPvmSt11align_val_t_nid_postfix(void* pointer, std::size_t, std::size_t) { Release(pointer); }
/// operator delete[](void*, size_t, std::align_val_t).
void APS5_VABI _ZdaPvmSt11align_val_t_nid_postfix(void* pointer, std::size_t, std::size_t) { Release(pointer); }

/// C11 aligned_alloc: memalign semantics (the FreeBSD libc does not enforce size % alignment == 0).
/// Returns nullptr with EINVAL for an invalid alignment and ENOMEM when the heap is exhausted.
void* APS5_VABI aligned_alloc_nid_postfix(std::size_t alignment, std::size_t size) {
    return AlignedOrNull(size, alignment);
}

/// reallocalign(ptr, size, alignment): only the `ptr == nullptr` form (plain aligned allocation) is
/// supported. Resizing an existing block needs its old size, which the application-heap callback table
/// (malloc/free/calloc/realloc/memalign/posix_memalign) does not expose; that state is an unsupported
/// abort (logged) rather than a silent wrong answer. The null-pointer form reports errors like aligned_alloc.
void* APS5_VABI reallocalign_nid_postfix(void* ptr, std::size_t size, std::size_t alignment) {
    if (ptr != nullptr) Unsupported("reallocalign of an existing block");
    return AlignedOrNull(size, alignment);
}

}
