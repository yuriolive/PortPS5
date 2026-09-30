// Wide-to-multibyte conversion for the "C" locale (guest wchar_t is 16 bits).
//
// Subsystem: libc.prx strings. Only the C locale exists (see LocaleSupport.cpp), and in the C locale a
// wide character maps to exactly one byte when it is <= 0xFF; anything larger has no multibyte form.
// Ported from AnyPS5 96b622c6 with the guest errno (__error) used instead of the host errno and a null
// source rejected instead of dereferenced.
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>

// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
extern "C" int* APS5_VABI __error_nid_postfix();

extern "C" {

/// wcstombs for the C locale. Converts the NUL-terminated UTF-16 string `source` into at most `capacity`
/// bytes of `destination`; with a null `destination` only the required length is computed. Returns the
/// number of bytes produced (excluding the terminating NUL), or (size_t)-1 with EILSEQ (86) when a unit
/// is above 0xFF, or EINVAL (22) for a null source. If the output fills `capacity` without reaching the
/// terminator, no NUL is written (standard behaviour).
std::size_t APS5_VABI wcstombs_nid_postfix(char* destination, const std::uint16_t* source, std::size_t capacity) {
    if (source == nullptr) {
        *__error_nid_postfix() = 22;
        return static_cast<std::size_t>(-1);
    }
    std::size_t count = 0;
    while (!destination || count < capacity) {
        const auto value = source[count];
        if (value > 255) {
            *__error_nid_postfix() = 86;
            return static_cast<std::size_t>(-1);
        }
        if (destination) destination[count] = static_cast<char>(value);
        if (value == 0) break;
        ++count;
    }
    return count;
}

}
