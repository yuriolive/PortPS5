// Narrow printf/scanf family exports for libc.prx.
//
// Subsystem: libc.prx formatting. Guest code calls these with the System V calling convention and passes
// variadic arguments in a System V va_list (struct of gp_offset/fp_offset/overflow/reg_save). Linux hosts
// use the same layout, so the native v*printf/v*scanf can consume it directly. Windows hosts have a
// different va_list, so they go through LibcDetail::FormatWindows / LibcDetail::ScanfArguments, which read
// the System V structure themselves. Wide-character variants live in FormattingWide.cpp and the Annex-K
// `_s` variants in FormattingChecked.cpp.
//
// Threading: stateless; stream operations rely on the stdio FILE lock.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include "SceTypes.hpp"
#include "prx/libc/include/VarArgsAbi.hpp"
#include "prx/libc/include/FileStream.hpp"

#ifdef _WIN32
#include "prx/libc/include/WindowsFormatting.hpp"
#include "prx/libc/include/ScanfArguments.hpp"
#endif

// Guest errno accessor (FreeBSD numbering), defined in General.cpp / Errors.cpp.
extern "C" int* APS5_VABI __error_nid_postfix();

extern "C" {

/// vfprintf: formats `format` with the guest va_list into `stream`. Returns the character count, or -1 on a
/// short write. Throws (host exception) for a null/closed stream or a malformed format on Windows.
int APS5_VABI vfprintf_nid_postfix(FileStream* stream, const char* format, VaList* args) {
    auto* native = GetNativeStream(stream);
#ifdef _WIN32
    std::string buffer;
    const int count = LibcDetail::FormatWindows(nullptr, 0, format, args, &buffer);
    const int result = std::fwrite(buffer.data(), 1, static_cast<size_t>(count), native) ==
        static_cast<size_t>(count) ? count : -1;
#else
    const int result = std::vfprintf(native, format, *reinterpret_cast<std::va_list*>(args));
#endif
    stream->SyncStatus();
    return result;
}

/// fprintf: inline-varargs form of vfprintf_nid_postfix with the same return value and failure modes.
int APS5_VABI fprintf_nid_postfix(FileStream* stream, const char* format, ...) {
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
#else
    std::va_list args;
    va_start(args, format);
#endif
    const int result = vfprintf_nid_postfix(stream, format, reinterpret_cast<VaList*>(args));
#ifdef _WIN32
    __builtin_sysv_va_end(args);
#else
    va_end(args);
#endif
    return result;
}

/// fscanf: reads formatted input from `stream` into the guest pointers named by the conversions. Returns the
/// number of assigned items, or EOF (-1) on input failure before the first conversion, on a null/closed
/// stream (errno EINVAL) or on a malformed format (errno EINVAL).
/// On Windows the System V va_list is marshalled by LibcDetail::ScanfArguments (see that header for why the
/// host vfscanf can not consume it directly, and for the LP64 -> LLP64 length rewriting).
int APS5_VABI fscanf_nid_postfix(FileStream* stream, const char* format, ...) {
    std::FILE* native = nullptr;
    try {
        native = GetNativeStream(stream);
    } catch (const std::exception&) {
        *__error_nid_postfix() = 22;
        return EOF;
    }
    if (format == nullptr) { *__error_nid_postfix() = 22; return EOF; }
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    int result = EOF;
    const LibcDetail::ScanfArguments scan(format, args);
    if (scan.Ok()) {
        const auto* p = scan.Pointers();
        result = std::fscanf(native, scan.Format(), APS5_SCANF_SPREAD(p));
    } else {
        *__error_nid_postfix() = 22;
    }
    __builtin_sysv_va_end(args);
#else
    std::va_list args;
    va_start(args, format);
    const int result = std::vfscanf(native, format, args);
    va_end(args);
#endif
    stream->SyncStatus();
    return result;
}

#ifdef _WIN32

/// printf (Windows host): formats to stdout through the System V-aware formatter; returns the character count.
int APS5_VABI printf_nid_postfix(const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::PrintWindows(format, args);
    __builtin_sysv_va_end(args);
    return result;
}

/// libc-internal alias of printf under its own NID (same behaviour).
int APS5_VABI libc_printf_nid_postfix(const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::PrintWindows(format, args);
    __builtin_sysv_va_end(args);
    return result;
}

/// snprintf (Windows host): writes at most `size - 1` characters plus NUL; returns the untruncated length.
int APS5_VABI snprintf_nid_postfix(char* buffer, size_t size, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::FormatWindows(buffer, size, format, args);
    __builtin_sysv_va_end(args);
    return result;
}

/// sprintf (Windows host): unbounded write into `buffer` (caller guarantees space); returns the length.
int APS5_VABI sprintf_nid_postfix(char* buffer, const char* format, ...) {
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
    const int result = LibcDetail::FormatWindows(buffer, SIZE_MAX, format, args);
    __builtin_sysv_va_end(args);
    return result;
}

#else

/// printf (non-Windows host): the guest va_list is the host's own, so the native vprintf consumes it.
int APS5_VABI printf_nid_postfix(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int result = std::vprintf(format, args);
    va_end(args);
    return result;
}

/// libc-internal printf alias. The named register parameters (VA_ARGS) rebuild the System V register save
/// area by hand because this entry point is reached without a compiler-visible va_start; returns the length.
int APS5_VABI libc_printf_nid_postfix(VA_ARGS) {
    (void)rcx; (void)r8; (void)r9;
    LibcDetail::RegSaveArea regs;
    LibcDetail::FillRegSaveArea(regs, rsi, rdx, rcx, r8, r9, 0,
        xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7);
    LibcDetail::VaListLayout layout;
    std::va_list* va = LibcDetail::BuildVaList(layout, regs, 0u,
        reinterpret_cast<void*>(overflow_arg_area));
    return std::vprintf(reinterpret_cast<const char*>(rdi), *va);
}

/// snprintf (non-Windows host): register-rebuilt va_list (see libc_printf); returns the untruncated length.
int APS5_VABI snprintf_nid_postfix(VA_ARGS) {
    LibcDetail::RegSaveArea regs;
    LibcDetail::FillRegSaveArea(regs, rcx, r8, r9, 0, 0, 0,
        xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7);
    LibcDetail::VaListLayout layout;
    std::va_list* va = LibcDetail::BuildVaList(layout, regs, 0u,
        reinterpret_cast<void*>(overflow_arg_area));
    return std::vsnprintf(
        reinterpret_cast<char*>(rdi),
        static_cast<size_t>(rsi),
        reinterpret_cast<const char*>(rdx),
        *va
    );
}

/// sprintf (non-Windows host): register-rebuilt va_list (see libc_printf); unbounded write, returns the length.
int APS5_VABI sprintf_nid_postfix(VA_ARGS) {
    LibcDetail::RegSaveArea regs;
    LibcDetail::FillRegSaveArea(regs, rdx, rcx, r8, r9, 0, 0,
        xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7);
    LibcDetail::VaListLayout layout;
    std::va_list* va = LibcDetail::BuildVaList(layout, regs, 0u,
        reinterpret_cast<void*>(overflow_arg_area));
    return std::vsprintf(
        reinterpret_cast<char*>(rdi),
        reinterpret_cast<const char*>(rsi),
        *va
    );
}

#endif

/// sscanf: parses `rdi` (the input string) according to the format in `rsi`, storing through the guest
/// pointers that follow. Returns the number of assigned items or EOF on early input failure; on Windows a
/// malformed format or more than ScanfArguments::MaxPointers conversions gives EOF with errno EINVAL.
/// The VA_ARGS register parameters rebuild the System V register save area by hand (see libc_printf).
/// Windows note: the rebuilt structure is marshalled by LibcDetail::ScanfArguments because the host vsscanf
/// would otherwise interpret the System V struct as a host va_list and crash (regression test Sscanf in
/// core/libs/tests/GuestWideIo.cpp).
int APS5_VABI sscanf_nid_postfix(VA_ARGS) {
    LibcDetail::RegSaveArea regs;
    LibcDetail::FillRegSaveArea(regs, rdx, rcx, r8, r9, 0, 0,
        xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7);
    LibcDetail::VaListLayout layout;
    std::va_list* va = LibcDetail::BuildVaList(layout, regs, 0u,
        reinterpret_cast<void*>(overflow_arg_area));
#ifdef _WIN32
    const LibcDetail::ScanfArguments scan(reinterpret_cast<const char*>(rsi), reinterpret_cast<const void*>(va));
    if (!scan.Ok()) { *__error_nid_postfix() = 22; return EOF; }
    const auto* p = scan.Pointers();
    return std::sscanf(reinterpret_cast<const char*>(rdi), scan.Format(), APS5_SCANF_SPREAD(p));
#else
    return std::vsscanf(
        reinterpret_cast<const char*>(rdi),
        reinterpret_cast<const char*>(rsi),
        *va
    );
#endif
}

/// vprintf: formats with the guest va_list to stdout; returns the character count.
int APS5_VABI vprintf_nid_postfix(const char* str, VaList* c) {
#ifdef _WIN32
    return LibcDetail::PrintWindows(str, c);
#else
    std::va_list* va = reinterpret_cast<std::va_list*>(c);
    return std::vprintf(str, *va);
#endif
}

/// vsprintf: unbounded formatting of the guest va_list into `str`; returns the character count.
int APS5_VABI vsprintf_nid_postfix(char* str, const char* format, VaList* args) {
#ifdef _WIN32
    return LibcDetail::FormatWindows(str, SIZE_MAX, format, args);
#else
    return std::vsprintf(str, format, *reinterpret_cast<std::va_list*>(args));
#endif
}

/// vsnprintf: bounded formatting of the guest va_list into `str[size]`; returns the untruncated length.
int APS5_VABI vsnprintf_nid_postfix(char* str, size_t size, const char* format, VaList* c) {
#ifdef _WIN32
    return LibcDetail::FormatWindows(str, size, format, c);
#else
    std::va_list* va = reinterpret_cast<std::va_list*>(c);
    return std::vsnprintf(str, size, format, *va);
#endif
}

/// puts: writes `s` and a newline to stdout; returns a non-negative value or EOF.
int APS5_VABI puts_nid_postfix(const char* s) {
    return std::puts(s);
}

}
