// Additional stdio exports: fopen_s, fgetpos, fsetpos.
//
// Subsystem: libc.prx stdio. They wrap the existing FileStream primitives (fopen/ftello/fseeko in
// FileAndHeap.cpp) and follow the FreeBSD ABI: fpos_t is a plain 64-bit offset, and the `_s` function
// returns an errno_t (guest numbering) instead of setting errno.
//
// Ported from AnyPS5 a599eca7. Differences from upstream: a null position pointer and a failing ftello are
// reported as return codes (EINVAL / -1) instead of throwing, because host exceptions thrown from libc.prx
// must not reach guest frames, and fopen_s converts the failure exception of fopen into a code.
//
// Threading: stateless; each call operates on the caller's stream.
#include <cstdint>
#include <cstdio>
#include <exception>

#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {
// Declaration of `fopen_nid_postfix`; its contract is documented at the definition.
FileStream* APS5_VABI fopen_nid_postfix(const char* filename, const char* mode);
// Declaration of `ftello_nid_postfix`; its contract is documented at the definition.
std::int64_t APS5_VABI ftello_nid_postfix(FileStream* stream);
// Declaration of `fseeko_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fseeko_nid_postfix(FileStream* stream, std::int64_t offset, int origin);
// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
int* APS5_VABI __error_nid_postfix();
}

namespace {
constexpr int GuestEnoent = 2;
constexpr int GuestEinval = 22;
}

extern "C" {

/// fopen_s: opens `filename` and stores the stream in `*result`. Returns 0 on success, EINVAL (22) for null
/// arguments, or ENOENT (2) when the open fails (the host open path reports failures as one class, so the
/// finer errno distinction is not available here). `*result` is nulled on every failure.
int APS5_VABI fopen_s_nid_postfix(FileStream** result, const char* filename, const char* mode) {
    if (!result) return GuestEinval;
    *result = nullptr;
    if (!filename || !mode) return GuestEinval;
    try {
        *result = fopen_nid_postfix(filename, mode);
    } catch (const std::exception&) {
        *result = nullptr;
    }
    return *result ? 0 : GuestEnoent;
}

/// fgetpos: stores the current position of `stream` in `*position` (a 64-bit offset, the FreeBSD fpos_t).
/// Returns 0, or -1 with guest errno EINVAL for a null position or an unpositionable stream.
int APS5_VABI fgetpos_nid_postfix(FileStream* stream, std::int64_t* position) {
    if (!position) { *__error_nid_postfix() = GuestEinval; return -1; }
    const std::int64_t offset = ftello_nid_postfix(stream);
    if (offset < 0) return -1;
    *position = offset;
    return 0;
}

/// fsetpos: seeks `stream` to the offset previously stored by fgetpos. Returns 0 on success, or -1 with guest
/// errno EINVAL for a null position (or the seek's own error).
int APS5_VABI fsetpos_nid_postfix(FileStream* stream, const std::int64_t* position) {
    if (!position) { *__error_nid_postfix() = GuestEinval; return -1; }
    return fseeko_nid_postfix(stream, *position, SEEK_SET);
}

}
