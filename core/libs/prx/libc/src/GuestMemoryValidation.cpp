// GuestMemoryValidation.cpp: implementation of the guest-pointer range checks
// declared in GuestMemoryValidation.hpp.
//
// Subsystem: guest memory. Registered allocations are authoritative for the
// guest protection (GuestAllocations::Cover). Anything the registry does not
// cover (thread stacks, TLS, data of modules loaded after the main image) is
// checked against the host mapping so a legitimate stack buffer is not
// rejected. The host query is Win32 VirtualQuery on Windows and
// /proc/self/maps on Linux (the Linux path exists so the logic is unit-tested
// on Linux hosts). Nothing here throws: the exports are called from
// APS5_VABI functions where a host exception would unwind through guest
// frames.

#include "prx/libc/include/GuestMemoryValidation.hpp"
#include "prx/libc/include/GuestAllocations.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace GuestMemoryValidation {
namespace {

#ifdef _WIN32
/**
 * Checks [first, last) against the Win32 virtual address space, region by
 * region. Memory must be MEM_COMMIT and not guarded; the page protection
 * bits decide readable/writable (the same classification
 * GuestAllocationsRegisterMainImage uses for the main image).
 */
Status HostCheck(std::uint64_t first, std::uint64_t last, bool read, bool write) noexcept {
    auto cursor = first;
    while (cursor < last) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(cursor)), &info, sizeof(info)) != sizeof(info)) return Status::Unmapped;
        if (info.State != MEM_COMMIT) return Status::Unmapped;
        // PAGE_GUARD pages fault on first touch; treat as inaccessible rather than consuming the guard.
        if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return Status::AccessDenied;
        const auto protection = info.Protect & 0xffu;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool readable = writable || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
        if ((write && !writable) || (read && !readable)) return Status::AccessDenied;
        const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return Status::Unmapped;  // defensive: a non-advancing region would loop forever
        cursor = next;
    }
    return Status::Ok;
}
#else
/**
 * Checks [first, last) against /proc/self/maps. The file lists regions in
 * ascending order, so one pass with a moving cursor suffices. A region that
 * starts after the cursor is a hole (Unmapped); a region without the needed
 * permission bit is AccessDenied. Failing to open the file is reported as
 * Unmapped (fail closed).
 */
Status HostCheck(std::uint64_t first, std::uint64_t last, bool read, bool write) noexcept {
    std::FILE* maps = std::fopen("/proc/self/maps", "re");
    if (!maps) return Status::Unmapped;
    char line[512];
    auto cursor = first;
    while (cursor < last && std::fgets(line, sizeof(line), maps)) {
        unsigned long long start = 0, finish = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &finish, perms) != 3) continue;
        // A line may exceed the buffer; discard the remainder so it is not parsed as a new record.
        if (!std::strchr(line, '\n')) {
            int c;
            while ((c = std::fgetc(maps)) != EOF && c != '\n') {}
        }
        if (finish <= cursor) continue;
        if (start > cursor) break;  // hole: stays Unmapped
        const bool allowed = (!write || perms[1] == 'w') && (!read || perms[0] == 'r');
        if (!allowed) {
            std::fclose(maps);
            return Status::AccessDenied;
        }
        cursor = finish;
    }
    std::fclose(maps);
    return cursor >= last ? Status::Ok : Status::Unmapped;
}
#endif

}  // namespace

/**
 * Validation order: argument sanity first (so a wrapped range is never handed
 * to the registry), then a loop alternating registry coverage and host
 * fallback for unregistered gaps. Each iteration advances `cursor` past the
 * piece it just proved, so the loop ends after at most (registered ranges + 1)
 * iterations of progress.
 */
Status GuestMemoryValidateRange_nid_postfix(const void* pointer, std::size_t bytes, std::uint32_t access) noexcept {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
    const bool write = (access & kWrite) != 0;
    const bool read = (access & kRead) != 0;
    if (address == 0 || (!read && !write) || (access & ~(kRead | kWrite)) != 0) return Status::InvalidRange;
    if (bytes == 0) return Status::Ok;
    // Overflow-safe: compare against the headroom instead of computing address + bytes first.
    if (static_cast<std::uint64_t>(bytes) > std::numeric_limits<std::uint64_t>::max() - address) return Status::InvalidRange;
    const auto end = address + bytes;
    auto cursor = address;
    while (cursor < end) {
        std::uint64_t gapStart = 0, gapEnd = 0;
        // A writable registered range is always readable (GuestAllocationsAdd enforces it), so
        // asking for write permission when `write` is set also covers a combined read+write request.
        const auto coverage = GuestAllocations::GuestAllocationsCover_nid_postfix(cursor, end - cursor, write, &gapStart, &gapEnd);
        if (coverage == GuestAllocations::Coverage::Covered) return Status::Ok;
        if (coverage == GuestAllocations::Coverage::Denied) return Status::AccessDenied;
        if (gapStart > cursor) {
            cursor = gapStart;  // [cursor, gapStart) is registered with the needed access
            continue;
        }
        // The registry does not know [gapStart, gapEnd): fall back to the host mapping.
        const auto status = HostCheck(gapStart, gapEnd, read, write);
        if (status != Status::Ok) return status;
        cursor = gapEnd;
    }
    return Status::Ok;
}

}  // namespace GuestMemoryValidation
