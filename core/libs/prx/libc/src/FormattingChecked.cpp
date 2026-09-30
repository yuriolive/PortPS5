// Annex-K-style formatted output exports: snprintf_s, sprintf_s, vsprintf_s, printf_s.
//
// Subsystem: libc.prx formatting. Semantics follow the host snprintf family (output truncated to `size - 1`
// characters plus a terminator, return value is the untruncated length), which is what AnyPS5 a599eca7
// chose for the PS5 `_s` printf variants; no separate constraint handler is modelled.
//
// Windows hosts cannot hand the guest's System V va_list to the CRT, so they go through
// LibcDetail::FormatWindows exactly like the unchecked family in Formatting.cpp; other hosts use the
// native va_list machinery.
//
// Threading: stateless.
#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include "SceTypes.hpp"
#include "prx/libc/include/VarArgsAbi.hpp"

#ifdef _WIN32
#include "prx/libc/include/WindowsFormatting.hpp"
#endif

extern "C" {

#ifdef _WIN32

/// vsprintf_s: formats `format` with the guest va_list into `buffer[size]`; returns the untruncated length.
int APS5_VABI vsprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, VaList* args) {
    return LibcDetail::FormatWindows(buffer, size, format, args);
}

/// sprintf_s: same contract as vsprintf_s with inline varargs.
int APS5_VABI sprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::FormatWindows(buffer, size, format, args);
    __builtin_sysv_va_end(args);
    return result;
}

/// snprintf_s: identical to sprintf_s (kept as a separate NID).
int APS5_VABI snprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::FormatWindows(buffer, size, format, args);
    __builtin_sysv_va_end(args);
    return result;
}

/// printf_s: formats to stdout; returns the number of characters written.
int APS5_VABI printf_s_nid_postfix(const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::PrintWindows(format, args);
    __builtin_sysv_va_end(args);
    return result;
}

#else

/// vsprintf_s: formats `format` with the guest va_list into `buffer[size]`; returns the untruncated length.
int APS5_VABI vsprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, VaList* args) {
    return std::vsnprintf(buffer, size, format, *reinterpret_cast<std::va_list*>(args));
}

/// sprintf_s: same contract as vsprintf_s with inline varargs.
int APS5_VABI sprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int result = std::vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

/// snprintf_s: identical to sprintf_s (kept as a separate NID).
int APS5_VABI snprintf_s_nid_postfix(char* buffer, std::size_t size, const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int result = std::vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

/// printf_s: formats to stdout; returns the number of characters written.
int APS5_VABI printf_s_nid_postfix(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int result = std::vprintf(format, args);
    va_end(args);
    return result;
}

#endif

}
