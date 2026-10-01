// core/libs/GuestRangeCheck.hpp
// Best-effort "is this guest range usable" probe for PRX exports, until the guest-memory range API
// exists (bean portps5-8l0d). On Windows it asks the VM manager whether the range is committed with
// the requested protection; elsewhere it only rejects null. Header-only and allocation-free, safe
// to call from any thread. Replace every user with the guest-memory API when it lands.

#ifndef CORE_LIBS_GUESTRANGECHECK_HPP
#define CORE_LIBS_GUESTRANGECHECK_HPP

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief Checks that `bytes` bytes at `pointer` can be read, or written when `write` is true.
 * @param pointer Guest pointer, untrusted.
 * @param bytes Range length; zero is accepted for any non-null pointer.
 * @param write True to require a writable range.
 * @return False for null, an address-space wrap, or (on Windows) an uncommitted, guarded or
 *         wrongly protected page anywhere in the range.
 */
inline bool GuestRangeUsable(const void* pointer, std::size_t bytes, bool write) {
    if (pointer == nullptr) {
        return false;
    }
#ifdef _WIN32
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = begin + bytes;
    if (end < begin) {
        return false;
    }
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    auto cursor = begin;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
            return false;
        }
        if ((info.Protect & PAGE_GUARD) != 0 || (info.Protect & (write ? kWritable : kReadable)) == 0) {
            return false;
        }
        cursor = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
#else
    (void)bytes;
    (void)write;
    return true;
#endif
}

#endif
