// C++ ABI support exports (demangler, thread-exit destructors, iostream category).
//
// Thread-exit destructors are guest functions (System V ABI); see RunThreadExitEntry for why they are
// invoked through a trampoline rather than handed to the host C++ runtime directly.
#include "prx/libc/include/exceptions/Runtime.hpp"
#include <cstddef>
#ifndef _UNWIND_H
#define _UNWIND_H
#endif

#include <cxxabi.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <typeinfo>
#include <new>
#include <ios>
#include <locale>
#include <regex>
#include <functional>

#include "prx/libc/include/General.hpp"

namespace {

/// Guest destructor registered for thread exit (System V ABI, like every guest function).
using GuestThreadDestructor = void (APS5_VABI *)(void*);

struct ThreadExitEntry {
    GuestThreadDestructor destructor;
    void* object;
};

/// Host-ABI trampoline handed to the host runtime. The runtime would call a guest destructor with the
/// Windows calling convention (argument in rcx) while the guest reads its argument from rdi, so the guest
/// function is invoked from here through its real System V type instead. Owns and frees its entry.
void RunThreadExitEntry(void* raw) {
    auto* entry = static_cast<ThreadExitEntry*>(raw);
    const auto destructor = entry->destructor;
    void* const object = entry->object;
    delete entry;
    destructor(object);
}

/// Registers `destructor(object)` to run when the calling thread exits. Returns 0 on success, 12 (ENOMEM)
/// if the bookkeeping entry can not be allocated, or 22 (EINVAL) for a null destructor (which would
/// otherwise crash later at thread exit). `dso` is forwarded only as the host runtime's grouping key.
int RegisterThreadExit(GuestThreadDestructor destructor, void* object, void* dso) {
    if (destructor == nullptr) return 22;
    auto* entry = new (std::nothrow) ThreadExitEntry{destructor, object};
    if (entry == nullptr) return 12;
    const int result = __cxxabiv1::__cxa_thread_atexit(RunThreadExitEntry, entry, dso);
    if (result != 0) delete entry;
    return result;
}

}

extern "C" {

/// Demangles an Itanium C++ symbol; same contract as abi::__cxa_demangle.
void* APS5_VABI __cxa_demangle_nid_postfix(const char* mangled, char* buf, std::size_t* len, int* status) {
    return abi::__cxa_demangle(mangled, buf, len, status);
}

/// glibc-style `__cxa_thread_atexit_impl`: the guest destructor runs at thread exit. See RegisterThreadExit.
int APS5_VABI __cxa_thread_atexit_impl_nid_postfix(GuestThreadDestructor func, void* arg, void* dso) {
    return RegisterThreadExit(func, arg, dso);
}

/// Registers a thread-exit destructor on behalf of the guest C++ runtime. Returns 0 on success.
/// On Windows the guest's module id is an opaque value from the guest's own layout, so null is passed as
/// the grouping key instead of forwarding a value the host runtime did not create (AnyPS5 9f81bddf).
int APS5_VABI LibcInternalExtCxaThreadAtexit_nid_postfix(GuestThreadDestructor destructor, void* object, void* module_id) {
#ifdef _WIN32
    (void)module_id;
    return RegisterThreadExit(destructor, object, nullptr);
#else
    return RegisterThreadExit(destructor, object, module_id);
#endif
}

const std::error_category* _ZSt17iostream_categoryv_nid_postfix() { return &std::iostream_category(); }

}
