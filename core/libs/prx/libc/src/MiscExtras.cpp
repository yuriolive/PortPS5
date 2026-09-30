// Small libc exports ported from AnyPS5 a599eca7 that do not belong to a larger family: strtoumax and asctime.
//
// Subsystem: libc.prx conversion/time helpers. Both forward to the host C library; the guest data model
// (LP64) and the host's agree for the types involved (uintmax_t is 64-bit on both), so no marshalling is
// needed. Kept in their own file so the legacy Strings.cpp / Time.cpp stay untouched.
//
// Threading: strtoumax is reentrant. asctime returns a process-wide static buffer, like the standard one.
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <ctime>

extern "C" {

/// strtoumax: parses an unsigned integer of up to 64 bits (same contract as std::strtoumax; ERANGE and
/// end-pointer behaviour are the host's).
std::uintmax_t APS5_VABI strtoumax_nid_postfix(const char* str, char** endptr, int base) {
    return std::strtoumax(str, endptr, base);
}

/// asctime: formats `timeptr` as "Www Mmm dd hh:mm:ss yyyy" plus a newline, in a static buffer shared by all
/// callers (standard non-reentrant contract; guests wanting reentrancy use asctime_r / strftime).
char* APS5_VABI asctime_nid_postfix(const std::tm* timeptr) {
    return std::asctime(timeptr);
}

}
