// Heap-allocating string exports: strndup and asprintf.
//
// Subsystem: libc.prx strings/formatting. Results are allocated through the application heap
// (ApplicationHeapAllocate_nid_no_patch), because the guest releases them with its own free(), which routes
// to the same heap (docs: the libc allocator replacement table). Allocating from the host CRT heap here would
// hand the guest a pointer its free() can not release.
//
// Ported from AnyPS5 96b622c6 with these adaptations: failures set the guest errno (__error) rather than the
// host errno, and formatting errors no longer rethrow host exceptions into guest frames (the shared
// unwinder lets guest catch(...) swallow them); they map to -1/EINVAL like a real libc rejecting a format.
//
// Threading: stateless.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/General.hpp"

#ifdef _WIN32
#include "prx/libc/include/WindowsFormatting.hpp"
#endif

// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
extern "C" int* APS5_VABI __error_nid_postfix();

namespace {

constexpr int GuestEinval = 22;
constexpr int GuestEnomem = 12;

void SetGuestErrno(int value) { *__error_nid_postfix() = value; }

// Allocates from the application heap, mapping an exhausted heap to nullptr (caller sets ENOMEM). Only
// std::bad_alloc is absorbed: any other exception means the allocator itself is broken or unregistered,
// which is an unsupported state and must stay loud.
void* TryAllocate(std::size_t bytes) {
    try {
        return ApplicationHeapAllocate_nid_no_patch(bytes);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

}

extern "C" {

/// Copies at most `limit` bytes of `source` into a new application-heap string that is always NUL-terminated.
/// Returns nullptr with EINVAL for a null source and ENOMEM when the heap is exhausted.
char* APS5_VABI strndup_nid_postfix(const char* source, std::size_t limit) {
    if (source == nullptr) { SetGuestErrno(GuestEinval); return nullptr; }
    std::size_t length = 0;
    while (length < limit && source[length] != '\0') ++length;
    auto* result = static_cast<char*>(TryAllocate(length + 1));
    if (!result) { SetGuestErrno(GuestEnomem); return nullptr; }
    std::memcpy(result, source, length);
    result[length] = '\0';
    return result;
}

/// Formats into a newly allocated application-heap string stored in `*destination`. Returns the character
/// count, or -1 with `*destination` set to null: EINVAL for a null destination/format or a format the
/// formatter rejects, ENOMEM when the heap is exhausted.
int APS5_VABI asprintf_nid_postfix(char** destination, const char* format, ...) {
    if (!destination) { SetGuestErrno(GuestEinval); return -1; }
    *destination = nullptr;
    if (!format) { SetGuestErrno(GuestEinval); return -1; }
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
#else
    std::va_list args;
    va_start(args, format);
#endif
    int result = -1;
    try {
#ifdef _WIN32
        std::string text;
        const int count = LibcDetail::FormatWindows(nullptr, 0, format, args, &text);
        const char* source = text.c_str();
#else
        char* text = nullptr;
        const int count = ::vasprintf(&text, format, args);
        std::unique_ptr<char, decltype(&std::free)> owner(text, std::free);
        const char* source = text;
#endif
        if (count >= 0) {
            auto* output = static_cast<char*>(TryAllocate(static_cast<std::size_t>(count) + 1));
            if (output) {
                std::memcpy(output, source, static_cast<std::size_t>(count) + 1);
                *destination = output;
                result = count;
            } else SetGuestErrno(GuestEnomem);
        } else SetGuestErrno(GuestEinval);
    } catch (const std::bad_alloc&) {
        SetGuestErrno(GuestEnomem);
    } catch (const std::exception&) {
        // FormatWindows reports malformed/unsupported conversions by throwing.
        SetGuestErrno(GuestEinval);
    }
#ifdef _WIN32
    __builtin_sysv_va_end(args);
#else
    va_end(args);
#endif
    return result;
}

}
