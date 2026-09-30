// Guest scanf-family argument marshalling for hosts whose own va_list is not the System V one (Windows).
//
// Guest code passes scanf destinations in a System V va_list. On Linux that is the host's own va_list and
// vfscanf can consume it directly. On Windows the host va_list is a flat pointer array, so handing it the
// System V structure (as AnyPS5 85a0ec2e did with a reinterpret_cast) makes vfscanf read the struct's
// gp_offset/fp_offset as if they were a pointer. Instead this header walks the format once, pulls exactly one
// pointer per argument-consuming conversion out of the System V va_list, rewrites guest length modifiers
// to their host equivalents, and lets the caller invoke the host scanf with the pointers as ordinary varargs.
//
// Guest data model is LP64 (long == 64 bit) whereas Windows is LLP64 (long == 32 bit), so `l`, `z`, `j` and `t`
// on an integer conversion are rewritten to `ll`; otherwise `%ld` would store only 4 of the guest's 8 bytes.
// Wide conversions (%lc, %ls, %l[) are left alone: guest wchar_t is 16-bit, like the Windows one.
//
// Limits: at most ScanfArguments::MaxPointers argument-consuming conversions per call. Exceeding that is
// reported as an invalid format (Ok() == false) instead of reading past the real arguments.
#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_SCANFARGUMENTS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_SCANFARGUMENTS_HPP

#include "prx/libc/include/WindowsFormatting.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace LibcDetail {

class ScanfArguments {
public:
    static constexpr std::size_t MaxPointers = 16;

    /// Parses `format` and extracts its destination pointers from the System V `source` va_list.
    /// Ok() is false for a null format/source, an unterminated conversion or too many conversions.
    ScanfArguments(const char* format, const void* source) {
        if (format == nullptr || source == nullptr) return;
        FormatArguments args(source);
        std::string out;
        for (const char* cursor = format; *cursor;) {
            if (*cursor != '%') { out.push_back(*cursor++); continue; }
            out.push_back(*cursor++);
            if (*cursor == '%') { out.push_back(*cursor++); continue; }
            bool suppressed = false;
            if (*cursor == '*') { suppressed = true; out.push_back(*cursor++); }
            while (*cursor >= '0' && *cursor <= '9') out.push_back(*cursor++);
            std::string length;
            while (*cursor && std::strchr("hlLjzt", *cursor)) length.push_back(*cursor++);
            const char conversion = *cursor;
            if (conversion == '\0') return;  // unterminated conversion
            const bool integer = std::strchr("diouxXn", conversion) != nullptr;
            // Host `long` is 32-bit; the guest's is 64-bit. Map every 64-bit-capable length to "ll".
            if (integer && (length == "l" || length == "z" || length == "j" || length == "t")) length = "ll";
            out += length;
            out.push_back(conversion);
            ++cursor;
            if (conversion == '[') {
                // A scanset ends at the first ']' after an optional '^' and an optional leading ']'.
                if (*cursor == '^') out.push_back(*cursor++);
                if (*cursor == ']') out.push_back(*cursor++);
                while (*cursor && *cursor != ']') out.push_back(*cursor++);
                if (*cursor == '\0') return;
                out.push_back(*cursor++);
            }
            if (!suppressed) {
                if (count_ == MaxPointers) return;
                pointers_[count_++] = args.Next<void*>();
            }
        }
        format_ = std::move(out);
        ok_ = true;
    }

    bool Ok() const { return ok_; }
    /// Host-format string equivalent to the guest one (valid only when Ok()).
    const char* Format() const { return format_.c_str(); }
    /// MaxPointers destination slots; slots beyond the real conversion count are null and ignored by scanf.
    void* const* Pointers() const { return pointers_; }

private:
    bool ok_ = false;
    std::string format_;
    void* pointers_[MaxPointers] = {};
    std::size_t count_ = 0;
};

// Expands to the 16 marshalled pointer arguments, for use as `std::fscanf(stream, scan.Format(), APS5_SCANF_SPREAD(p))`
// where `p = scan.Pointers()`. Passing unused trailing null varargs is harmless for scanf (it only consumes
// as many as the format names).
#define APS5_SCANF_SPREAD(p) p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]

}

#endif
