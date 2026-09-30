// Wide-character (UTF-16 guest wchar_t) formatting and stream output: vswprintf, wprintf, fputwc, fputws.
//
// Subsystem: libc.prx formatting. Guest wchar_t is 16 bits (char16_t), which the host wchar_t functions can
// not serve on Linux and do not match on Windows semantics, so the conversions are done here:
//   * `%s` is a narrow UTF-8 string and `%ls`/`%S` a UTF-16 one (POSIX/FreeBSD semantics, not MSVC's);
//   * numeric conversions reuse the host formatter through snprintf, reading arguments from the guest's
//     System V va_list (LibcDetail::FormatArguments), exactly like the narrow path in WindowsFormatting.hpp;
//   * byte-oriented streams receive UTF-8 (the C-locale-free choice the rest of libc uses).
//
// Ported from AnyPS5 (940f28d1 vswprintf/wprintf, 77194e45 fputwc/fputws) with these adaptations: errors
// are reported through the guest errno (__error) instead of the host errno, nothing throws across the export
// boundary, lone surrogates and out-of-range UTF-8 become U+FFFD instead of invalid output, and
// field widths and precisions above 65536 are rejected so a hostile format can not overflow int or the stack.
//
// Threading: stateless apart from the stream's own FILE lock.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "SceTypes.hpp"
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/VarArgsAbi.hpp"
#include "prx/libc/include/WindowsFormatting.hpp"

// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
extern "C" int* APS5_VABI __error_nid_postfix();

#ifdef _WIN32
#define APS5_VA_BEGIN(last) __builtin_sysv_va_list args; __builtin_sysv_va_start(args, last)
#define APS5_VA_END() __builtin_sysv_va_end(args)
#else
#define APS5_VA_BEGIN(last) std::va_list args; va_start(args, last)
#define APS5_VA_END() va_end(args)
#endif

namespace {

constexpr int GuestEio = 5;
constexpr int GuestEinval = 22;
constexpr char16_t ReplacementCharacter = 0xfffd;

void SetGuestErrno(int value) { *__error_nid_postfix() = value; }

bool In(char16_t character, const char* set) {
    return character != 0 && character < 128 && std::strchr(set, static_cast<char>(character)) != nullptr;
}

// Largest field width or precision accepted. The host snprintf (MinGW's in particular) sizes scratch space
// from the width on the stack, so a guest-controlled 2-billion-wide field overflows the stack or exhausts
// memory. 65536 is far beyond any real formatted field; larger requests are rejected as invalid (-1/EINVAL).
constexpr int MaxFieldWidth = 1 << 16;

// Accumulates one decimal digit of a width/precision, rejecting values above MaxFieldWidth before the
// multiplication can overflow int.
int AppendDigit(int value, char16_t digit) {
    if (value > MaxFieldWidth) throw std::invalid_argument("Format width or precision too large");
    const int next = value * 10 + (digit - u'0');
    if (next > MaxFieldWidth) throw std::invalid_argument("Format width or precision too large");
    return next;
}

// Validates a width/precision taken from a `*` argument (widened to avoid negating INT_MIN).
int CheckStarField(long long magnitude) {
    if (magnitude > MaxFieldWidth) throw std::invalid_argument("Format width or precision too large");
    return static_cast<int>(magnitude);
}

// Appends the UTF-16 form of up to `limit` bytes of UTF-8 `text` (stops at NUL). Malformed sequences fall
// back to the raw byte as a Latin-1 code unit; code points above U+10FFFF become U+FFFD.
void AppendUtf16(std::u16string& out, const char* text, std::size_t limit) {
    for (std::size_t index = 0; index < limit && text[index] != '\0';) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t extra = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
        std::uint32_t code = extra == 3 ? lead & 0x07 : extra == 2 ? lead & 0x0f : extra == 1 ? lead & 0x1f : lead;
        std::size_t used = 1;
        for (; used <= extra; ++used) {
            // text[] is NUL-terminated, and NUL is not a continuation byte, so this never reads past the end.
            const unsigned char next = static_cast<unsigned char>(text[index + used]);
            if ((next & 0xc0) != 0x80) { extra = 0; code = lead; used = 1; break; }
            code = (code << 6) | (next & 0x3f);
        }
        if (extra != 0) used = extra + 1;
        index += used;
        if (code > 0x10ffff) out.push_back(ReplacementCharacter);
        else if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 + (code & 0x3ff)));
        } else out.push_back(static_cast<char16_t>(code));
    }
}

// UTF-16 -> UTF-8. A lone surrogate is encoded as U+FFFD: encoding it verbatim would emit CESU-style bytes
// that are not valid UTF-8.
std::string ToUtf8(const std::u16string& text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        std::uint32_t code = text[index];
        if (code >= 0xd800 && code < 0xdc00 && index + 1 < text.size() && text[index + 1] >= 0xdc00 && text[index + 1] < 0xe000)
            code = 0x10000 + ((code - 0xd800) << 10) + (text[++index] - 0xdc00);
        else if (code >= 0xd800 && code < 0xe000) code = ReplacementCharacter;
        if (code < 0x80) out.push_back(static_cast<char>(code));
        else if (code < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }
    return out;
}

template<class T> void AppendNumber(std::u16string& out, const std::string& spec, T value) {
    const int size = std::snprintf(nullptr, 0, spec.c_str(), value);
    if (size < 0) throw std::runtime_error("Formatting conversion failed");
    std::string text(static_cast<std::size_t>(size) + 1, '\0');
    std::snprintf(&text[0], text.size(), spec.c_str(), value);
    out.append(text.begin(), text.begin() + size);
}

void AppendPadded(std::u16string& out, const std::u16string& text, bool left, int width) {
    const std::size_t pad = width > 0 && static_cast<std::size_t>(width) > text.size() ? static_cast<std::size_t>(width) - text.size() : 0;
    if (!left) out.append(pad, u' ');
    out += text;
    if (left) out.append(pad, u' ');
}

// Wide printf engine. Throws std::invalid_argument/std::runtime_error/std::bad_alloc on malformed input;
// every exported wrapper catches and converts to a -1 return with EINVAL.
std::u16string FormatWide(const char16_t* format, VaList* source) {
    if (format == nullptr || source == nullptr) throw std::invalid_argument("Null formatting argument");
    LibcDetail::FormatArguments args(source);
    std::u16string out;
    while (*format) {
        if (*format != u'%') { out.push_back(*format++); continue; }
        ++format;
        if (*format == u'%') { out.push_back(*format++); continue; }
        std::string spec = "%";
        bool left = false;
        int width = 0;
        int precision = -1;
        while (In(*format, "-+ #0")) {
            if (*format == u'-') left = true;
            spec += static_cast<char>(*format++);
        }
        if (*format == u'*') {
            ++format;
            const int requested = args.Next<int>();
            // Widen before negating: -INT_MIN overflows int.
            const long long magnitude = requested < 0 ? -static_cast<long long>(requested) : requested;
            if (requested < 0) { left = true; spec += '-'; }
            width = CheckStarField(magnitude);
            spec += std::to_string(width);
        } else {
            // Digits are not copied verbatim: a width with hundreds of digits would reach snprintf
            // unbounded. Re-emit the validated value instead.
            bool hasWidth = false;
            while (*format >= u'0' && *format <= u'9') {
                width = AppendDigit(width, *format++);
                hasWidth = true;
            }
            if (hasWidth) spec += std::to_string(width);
        }
        if (*format == u'.') {
            ++format;
            precision = 0;
            if (*format == u'*') {
                ++format;
                precision = args.Next<int>();
                // A negative `*` precision means "no precision" (C99 7.19.6.1); large positive is bounded.
                if (precision >= 0) spec += "." + std::to_string(CheckStarField(precision));
            } else {
                while (*format >= u'0' && *format <= u'9') precision = AppendDigit(precision, *format++);
                spec += "." + std::to_string(precision);  // validated, see the width comment above
            }
        }
        std::string length;
        if (In(*format, "hljztL")) {
            length += static_cast<char>(*format++);
            if ((length == "h" && *format == u'h') || (length == "l" && *format == u'l')) length += static_cast<char>(*format++);
        }
        const char16_t conversion = *format;
        if (!conversion) throw std::invalid_argument("Incomplete format conversion");
        ++format;
        const bool integerLength = length.empty() || length == "h" || length == "hh" || length == "l" || length == "ll" ||
            length == "j" || length == "z" || length == "t";
        if (conversion == u'd' || conversion == u'i') {
            if (!integerLength) throw std::invalid_argument("Invalid integer length");
            long long value;
            if (length.empty() || length == "h" || length == "hh") {
                value = args.Next<int>();
                if (length == "h") value = static_cast<short>(value);
                if (length == "hh") value = static_cast<signed char>(value);
            } else value = args.Next<long long>();
            AppendNumber(out, spec + "ll" + static_cast<char>(conversion), value);
        } else if (In(conversion, "ouxX")) {
            if (!integerLength) throw std::invalid_argument("Invalid integer length");
            unsigned long long value;
            if (length.empty() || length == "h" || length == "hh") {
                value = args.Next<unsigned int>();
                if (length == "h") value = static_cast<unsigned short>(value);
                if (length == "hh") value = static_cast<unsigned char>(value);
            } else value = args.Next<unsigned long long>();
            AppendNumber(out, spec + "ll" + static_cast<char>(conversion), value);
        } else if (In(conversion, "aAeEfFgG")) {
            if (length == "L") AppendNumber(out, spec + "L" + static_cast<char>(conversion), args.Next<long double>());
            else {
                if (!length.empty() && length != "l") throw std::invalid_argument("Invalid floating length");
                AppendNumber(out, spec + static_cast<char>(conversion), args.Next<double>());
            }
        } else if (conversion == u'p' && length.empty()) {
            AppendNumber(out, spec + 'p', args.Next<void*>());
        } else if (conversion == u'c' || conversion == u'C') {
            const int value = args.Next<int>();
            std::u16string text;
            if (conversion == u'C' || length == "l") text.push_back(static_cast<char16_t>(value));
            else text.push_back(static_cast<char16_t>(static_cast<unsigned char>(value)));
            AppendPadded(out, text, left, width);
        } else if (conversion == u's' || conversion == u'S') {
            std::u16string text;
            const std::size_t limit = precision < 0 ? SIZE_MAX : static_cast<std::size_t>(precision);
            if (conversion == u'S' || length == "l") {
                const char16_t* value = args.Next<const char16_t*>();
                if (value == nullptr) value = u"(null)";
                for (std::size_t index = 0; index < limit && value[index] != 0; ++index) text.push_back(value[index]);
            } else {
                const char* value = args.Next<const char*>();
                AppendUtf16(text, value != nullptr ? value : "(null)", limit);
            }
            AppendPadded(out, text, left, width);
        } else {
            // Includes %n, which would write through a guest pointer: deliberately unsupported here.
            throw std::invalid_argument("Unsupported format conversion");
        }
    }
    return out;
}

// Writes `bytes` to the stream and mirrors the FILE state into the guest-visible prefix. Returns false on a
// short write or a closed/null stream.
bool WriteBytes(FileStream* stream, const std::string& bytes) {
    try {
        auto* native = GetNativeStream(stream);
        const bool complete = std::fwrite(bytes.data(), 1, bytes.size(), native) == bytes.size();
        stream->SyncStatus();
        return complete;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

extern "C" {

/// Wide vsnprintf-style formatter: writes at most `size - 1` UTF-16 units plus a terminator into `buffer`.
/// Returns the character count, or -1 when the output was truncated (the buffer is still terminated), the
/// buffer is null/empty (EINVAL), or the format is malformed/unsupported (EINVAL, buffer emptied).
int APS5_VABI vswprintf_nid_postfix(char16_t* buffer, std::size_t size, const char16_t* format, VaList* args) {
    if (buffer == nullptr || size == 0) { SetGuestErrno(GuestEinval); return -1; }
    try {
        const std::u16string text = FormatWide(format, args);
        const std::size_t copied = text.size() < size - 1 ? text.size() : size - 1;
        std::memcpy(buffer, text.data(), copied * sizeof(char16_t));
        buffer[copied] = 0;
        return copied == text.size() ? static_cast<int>(copied) : -1;
    } catch (const std::exception&) {
        buffer[0] = 0;
        SetGuestErrno(GuestEinval);
        return -1;
    }
}

/// Wide printf to stdout (UTF-8 on the byte stream). Returns the number of UTF-16 units formatted, or -1
/// with EINVAL (malformed format) / EIO (write failure).
int APS5_VABI wprintf_nid_postfix(const char16_t* format, ...) {
    APS5_VA_BEGIN(format);
    int result = -1;
    try {
        const std::u16string text = FormatWide(format, reinterpret_cast<VaList*>(args));
        const std::string bytes = ToUtf8(text);
        if (std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size()) result = static_cast<int>(text.size());
        else SetGuestErrno(GuestEio);
    } catch (const std::exception&) {
        SetGuestErrno(GuestEinval);
    }
    APS5_VA_END();
    return result;
}

/// Writes one UTF-16 unit as UTF-8 to `stream`. Returns the unit written, or -1 with EINVAL for a null or
/// closed stream and EIO for a write failure. A lone surrogate is written as U+FFFD (the unit is still
/// returned, as the standard requires).
int APS5_VABI fputwc_nid_postfix(char16_t value, FileStream* stream) {
    if (stream == nullptr) { SetGuestErrno(GuestEinval); return -1; }
    if (!WriteBytes(stream, ToUtf8(std::u16string(1, value)))) { SetGuestErrno(GuestEio); return -1; }
    return value;
}

/// Writes a NUL-terminated UTF-16 string as UTF-8 to `stream`. Returns the number of UTF-16 units written,
/// or -1 with EINVAL for a null string/stream and EIO for a write failure.
int APS5_VABI fputws_nid_postfix(const char16_t* str, FileStream* stream) {
    if (str == nullptr || stream == nullptr) { SetGuestErrno(GuestEinval); return -1; }
    const std::u16string text(str);
    if (!WriteBytes(stream, ToUtf8(text))) { SetGuestErrno(GuestEio); return -1; }
    return static_cast<int>(text.size());
}

}
