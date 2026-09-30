// Bounds-checked (C11 Annex K style) string and memory exports: strncpy_s, strcpy_s, strncat_s, strcat_s,
// memcpy_s, memmove_s, memset_s, plus strnstr.
//
// Subsystem: libc.prx strings. These are the `_s` functions the PS5 libc provides. Guest errno numbering
// (FreeBSD) is used for the returned codes: EINVAL = 22, ERANGE = 34. On a constraint violation the
// destination is neutralized (first byte NUL for strings, zero-filled for memcpy_s/memmove_s) as Annex K
// requires, so a caller that ignores the return code never sees stale or partially copied data.
//
// Ported from AnyPS5 a599eca7 (strncpy_s is the base it built on and was missing here). Known gap, shared
// with upstream: overlapping source/destination ranges for the string functions are not diagnosed.
//
// Threading: stateless.
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstring>

namespace {
constexpr int GuestEinval = 22;
constexpr int GuestErange = 34;
}

extern "C" {

/// strncpy_s: copies at most `count` bytes of `src` (stopping at NUL) into `dest[destsz]` and always
/// terminates. Returns 0; EINVAL for null dest/zero destsz (nothing written) or null src (dest emptied);
/// ERANGE when the result plus terminator does not fit (dest emptied).
int APS5_VABI strncpy_s_nid_postfix(char* dest, std::size_t destsz, const char* src, std::size_t count) {
    if (!dest || destsz == 0) return GuestEinval;
    if (!src) {
        dest[0] = '\0';
        return GuestEinval;
    }
    std::size_t length = 0;
    while (length < count && src[length] != '\0') ++length;
    if (length >= destsz) {
        dest[0] = '\0';
        return GuestErange;
    }
    std::memcpy(dest, src, length);
    dest[length] = '\0';
    return 0;
}

/// strcpy_s: strncpy_s with an unbounded count. Same return codes.
int APS5_VABI strcpy_s_nid_postfix(char* dest, std::size_t destsz, const char* src) {
    return strncpy_s_nid_postfix(dest, destsz, src, static_cast<std::size_t>(-1));
}

/// strncat_s: appends at most `count` bytes of `src` to the NUL-terminated string in `dest[destsz]`.
/// Returns 0; EINVAL for null dest/zero destsz or null src; ERANGE when `dest` is not terminated within
/// destsz or the result does not fit. On any error after dest is validated, dest is emptied.
int APS5_VABI strncat_s_nid_postfix(char* dest, std::size_t destsz, const char* src, std::size_t count) {
    if (!dest || destsz == 0) return GuestEinval;
    std::size_t used = 0;
    while (used < destsz && dest[used] != '\0') ++used;
    if (used == destsz || !src) {
        dest[0] = '\0';
        return used == destsz ? GuestErange : GuestEinval;
    }
    std::size_t length = 0;
    while (length < count && src[length] != '\0') ++length;
    if (length >= destsz - used) {
        dest[0] = '\0';
        return GuestErange;
    }
    std::memcpy(dest + used, src, length);
    dest[used + length] = '\0';
    return 0;
}

/// strcat_s: strncat_s with an unbounded count. Same return codes.
int APS5_VABI strcat_s_nid_postfix(char* dest, std::size_t destsz, const char* src) {
    return strncat_s_nid_postfix(dest, destsz, src, static_cast<std::size_t>(-1));
}

/// memcpy_s: copies `count` bytes into `dest[destsz]`. Returns 0; EINVAL for null dest, or null src with the
/// destination zero-filled; ERANGE (destination zero-filled) when count > destsz.
int APS5_VABI memcpy_s_nid_postfix(void* dest, std::size_t destsz, const void* src, std::size_t count) {
    if (!dest) return GuestEinval;
    if (!src || count > destsz) {
        std::memset(dest, 0, destsz);
        return src ? GuestErange : GuestEinval;
    }
    std::memcpy(dest, src, count);
    return 0;
}

/// memmove_s: memcpy_s with overlap-safe copying. Same return codes.
int APS5_VABI memmove_s_nid_postfix(void* dest, std::size_t destsz, const void* src, std::size_t count) {
    if (!dest) return GuestEinval;
    if (!src || count > destsz) {
        std::memset(dest, 0, destsz);
        return src ? GuestErange : GuestEinval;
    }
    std::memmove(dest, src, count);
    return 0;
}

/// memset_s: fills min(count, destsz) bytes through a volatile pointer so the store can not be optimized
/// away (its purpose is scrubbing secrets). Returns 0, EINVAL for null dest, or ERANGE (after filling
/// destsz bytes) when count > destsz.
int APS5_VABI memset_s_nid_postfix(void* dest, std::size_t destsz, int value, std::size_t count) {
    if (!dest) return GuestEinval;
    const auto length = count > destsz ? destsz : count;
    auto* bytes = static_cast<volatile unsigned char*>(dest);
    for (std::size_t index = 0; index < length; ++index) bytes[index] = static_cast<unsigned char>(value);
    return count > destsz ? GuestErange : 0;
}

/// strnstr: finds `needle` within the first `length` bytes of `haystack` (stopping at NUL). An empty needle
/// matches at the start. Returns the match or nullptr.
char* APS5_VABI strnstr_nid_postfix(const char* haystack, const char* needle, std::size_t length) {
    const std::size_t needleLength = std::strlen(needle);
    if (needleLength == 0) return const_cast<char*>(haystack);
    for (std::size_t index = 0; index < length && haystack[index] != '\0'; ++index) {
        if (needleLength > length - index) break;
        if (std::strncmp(haystack + index, needle, needleLength) == 0) return const_cast<char*>(haystack + index);
    }
    return nullptr;
}

}
