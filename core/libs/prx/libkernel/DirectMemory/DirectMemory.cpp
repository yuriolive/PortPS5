/**
 * @file DirectMemory.cpp
 * @brief Implementation of PS5 direct memory mapping, protection, and reservation helpers.
 * 
 * Interacts with host virtual memory subsystem (Windows VirtualAlloc/VirtualProtect or Linux mmap)
 * adhering to PS5 page granularity and error conventions.
 */

#include "prx/libkernel/DirectMemory/DirectMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestMemoryBacking.hpp"
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

#if defined(__linux__)
#include <sys/mman.h>
#else
#include <windows.h>

static constexpr int PROT_NONE = 0;
static constexpr int PROT_READ = 1;
static constexpr int PROT_WRITE = 2;
static constexpr int PROT_EXEC = 4;

static DWORD WinProtFromPosix(int prot) {
    if (prot == PROT_NONE) return PAGE_NOACCESS;
    if ((prot & PROT_EXEC) && (prot & PROT_WRITE)) return PAGE_EXECUTE_READWRITE;
    if ((prot & PROT_EXEC) && (prot & PROT_READ)) return PAGE_EXECUTE_READ;
    if (prot & PROT_EXEC) return PAGE_EXECUTE;
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    return PAGE_READONLY;
}

static int mprotect(void* addr, size_t len, int prot) {
    DWORD old;
    if (!VirtualProtect(addr, len, WinProtFromPosix(prot), &old))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "VirtualProtect failed");
    return 0;
}
#endif

namespace {

/**
 * @brief Validates that a length is non-zero and aligned to the PS5 page size.
 * @param len Length in bytes to validate.
 * @return 0 if valid, or SCE_KERNEL_ERROR_EINVAL.
 */
int ValidateLength(size_t len) {
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

/**
 * @brief Validates and resolves an alignment requirement to at least the PS5 page size.
 * @param alignment Requested alignment.
 * @param resolved Output receiving the resolved alignment.
 * @return 0 if valid, or SCE_KERNEL_ERROR_EINVAL.
 */
int ValidateAlignment(size_t alignment, size_t& resolved) {
    if (alignment == 0) {
        resolved = PS5_PAGE_SIZE;
        return 0;
    }
    if (alignment < PS5_PAGE_SIZE || (alignment & (alignment - 1)) != 0) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    resolved = alignment;
    return 0;
}

/**
 * @brief Validates an address range for pointer nullness, alignment, and integer overflow.
 * @param addr Base address of the range.
 * @param len Size in bytes.
 * @param alignment Required alignment.
 * @return 0 if valid, or SCE_KERNEL_ERROR_EINVAL.
 */
int ValidateRange(const void* addr, size_t len, size_t alignment) {
    int ret = ValidateLength(len);
    if (ret != 0) return ret;
    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    if (!addr || (start & (alignment - 1)) != 0 || len > std::numeric_limits<std::uintptr_t>::max() - start) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

/**
 * @brief Translates SCE protection bitmask flags into host/POSIX protection flags.
 * @param prot SCE protection flags.
 * @param linuxProt Output host protection flags.
 * @return 0 on success, or SCE_KERNEL_ERROR_EINVAL.
 */
int LinuxProtFromSce(int prot, int& linuxProt) {
    if ((prot & ~0xF7) != 0) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    int result = PROT_NONE;
    if (prot & 0x13) result |= PROT_READ;
    if (prot & 0x22) result |= PROT_READ | PROT_WRITE;
    if (prot & 4) result |= PROT_READ | PROT_EXEC;
    linuxProt = result;
    return 0;
}

/**
 * @brief Unmaps host memory backing pages.
 * @param addr Base virtual address.
 * @param len Size in bytes.
 */
void Unmap(void* addr, size_t len) {
    GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(addr, len);
}

/**
 * @brief Maps host memory pages with requested alignment and protection.
 * @param addr Base virtual address hint or fixed address.
 * @param len Size in bytes.
 * @param prot Host protection flags.
 * @param flags Mapping flags.
 * @param alignment Virtual alignment requirement.
 * @param mappedOut Output receiving the mapped virtual pointer.
 * @return 0 on success, or SCE error code on failure.
 */
int MapAligned(void* addr, size_t len, int prot, int flags, size_t alignment, void*& mappedOut) {
    int ret = ValidateLength(len);
    if (ret != 0) return ret;
    size_t resolvedAlign = 0;
    ret = ValidateAlignment(alignment, resolvedAlign);
    if (ret != 0) return ret;
    constexpr int guestMapFixed = 0x10;
    constexpr int guestMapNoCoalesce = 0x400000;
    if ((flags & ~(guestMapFixed | guestMapNoCoalesce)) != 0) return SCE_KERNEL_ERROR_EINVAL;
    if ((flags & guestMapFixed) != 0) {
        ret = ValidateRange(addr, len, resolvedAlign);
        if (ret != 0) return ret;
    } else if (addr != nullptr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    try {
        mappedOut = GuestMemoryBacking::GuestMemoryBackingMap_nid_postfix(addr, len, resolvedAlign, prot);
        return 0;
    } catch (const std::bad_alloc&) {
        return SCE_KERNEL_ERROR_ENOMEM;
    } catch (...) {
        return SCE_KERNEL_ERROR_ENOMEM;
    }
}

/**
 * @brief Validates an output pointer parameter for nullness.
 * @param addr Output pointer to validate.
 * @return 0 if non-null, or SCE_KERNEL_ERROR_EINVAL.
 */
int ValidateOutput(void** addr) {
    if (!addr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

}

/**
 * @brief Implementation of direct memory mapping into guest virtual space.
 * @param addr Base address pointer (in/out).
 * @param len Size in bytes to map.
 * @param prot Protection flags.
 * @param flags Mapping flags.
 * @param physStart Physical start address.
 * @param alignment Virtual alignment requirement.
 * @return 0 on success, or SCE error code on failure.
 */
int DoMapDirect(void** addr, size_t len, int prot, int flags, int64_t physStart, size_t alignment) {
    int ret = ValidateOutput(addr);
    if (ret != 0) return ret;
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;
    if (physStart < 0 || (static_cast<std::uint64_t>(physStart) & (PS5_PAGE_SIZE - 1)) != 0 || static_cast<std::uint64_t>(physStart) >= DIRECT_MEMORY_SIZE || len > DIRECT_MEMORY_SIZE - static_cast<std::uint64_t>(physStart)) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    int linuxProt = 0;
    ret = LinuxProtFromSce(prot, linuxProt);
    if (ret != 0) return ret;

    try {
        GuestAllocations::Mutation mutation;
        if (*addr != nullptr) mutation.RequireAvailable(*addr, len);
        void* mapped = nullptr;
        ret = MapAligned(*addr, len, linuxProt, flags, alignment, mapped);
        if (ret != 0) return ret;

        try {
            mutation.Add(mapped, len, (prot & 3) != 0, (prot & 2) != 0);
        } catch (...) {
            Unmap(mapped, len);
            return SCE_KERNEL_ERROR_ENOMEM;
        }
        *addr = mapped;
        return 0;
    } catch (...) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
}

/**
 * @brief Implementation of anonymous flexible memory mapping into guest virtual space.
 * @param addr Base address pointer (in/out).
 * @param len Size in bytes to map.
 * @param prot Protection flags.
 * @param flags Mapping flags.
 * @return 0 on success, or SCE error code on failure.
 */
int DoMapAnon(void** addr, size_t len, int prot, int flags) {
    int ret = ValidateOutput(addr);
    if (ret != 0) return ret;
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;
    int linuxProt = 0;
    ret = LinuxProtFromSce(prot, linuxProt);
    if (ret != 0) return ret;

    try {
        GuestAllocations::Mutation mutation;
        if (*addr != nullptr) mutation.RequireAvailable(*addr, len);
        void* mapped = nullptr;
        ret = MapAligned(*addr, len, linuxProt, flags, PS5_PAGE_SIZE, mapped);
        if (ret != 0) return ret;

        try {
            mutation.Add(mapped, len, (prot & 3) != 0, (prot & 2) != 0);
        } catch (...) {
            Unmap(mapped, len);
            return SCE_KERNEL_ERROR_ENOMEM;
        }
        *addr = mapped;
        return 0;
    } catch (...) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
}

/**
 * @brief Implementation of guest memory protection modification.
 * @param addr Start address of the range.
 * @param len Size in bytes of the range.
 * @param prot New protection flags.
 * @return 0 on success, or SCE error code on failure.
 */
int DoMprotect(const void* addr, size_t len, int prot) {
    const auto address = reinterpret_cast<std::uintptr_t>(addr);
    constexpr auto pageMask = static_cast<std::uintptr_t>(PS5_PAGE_SIZE - 1);
    const auto limit = std::numeric_limits<std::uintptr_t>::max();
    if (address == 0 || len == 0 || len > limit - address || address + len > limit - pageMask) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    int nativeProtection = 0;
    int ret = LinuxProtFromSce(prot, nativeProtection);
    if (ret != 0) return ret;

    const auto first = address & ~pageMask;
    const auto end = (address + len + pageMask) & ~pageMask;
    const auto bytes = static_cast<std::size_t>(end - first);
    const auto* pointer = reinterpret_cast<const void*>(first);

    try {
        GuestAllocations::Mutation mutation;
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory)) return SCE_KERNEL_ERROR_EFAULT;
        if (memory.Type == MEM_IMAGE) {
            if (memory.AllocationBase != GetModuleHandleW(nullptr)) return SCE_KERNEL_ERROR_EACCES;
            mutation.RegisterMainImage();
        }
#else
        mutation.RegisterMainImage();
#endif
        struct ProtectFailed {};
        try {
            mutation.Protect(pointer, bytes, (prot & 3) != 0, (prot & 2) != 0, [&] {
                if (mprotect(const_cast<void*>(pointer), bytes, nativeProtection) != 0) {
                    throw ProtectFailed{};
                }
            });
        } catch (const ProtectFailed&) {
            return SCE_KERNEL_ERROR_EFAULT;
        }
        return 0;
    } catch (...) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
}

/**
 * @brief Implementation of guest virtual memory unmapping.
 * @param addr Base address to unmap.
 * @param len Size in bytes to unmap.
 * @return 0 on success, or SCE error code on failure.
 */
int DoMunmap(void* addr, size_t len) {
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0 || !addr) return SCE_KERNEL_ERROR_EINVAL;
    try {
        GuestAllocations::Mutation mutation;
        mutation.Unmap(addr, len, [&](const void*, bool) {
            Unmap(addr, len);
        });
        return 0;
    } catch (...) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
}

/**
 * @brief Implementation of virtual address range reservation.
 * @param addr Output pointer receiving the reserved base address.
 * @param len Size in bytes to reserve.
 * @param alignment Virtual alignment requirement.
 * @return 0 on success, or SCE error code on failure.
 */
int DoReserveVirtual(void** addr, size_t len, size_t alignment) {
    int ret = ValidateOutput(addr);
    if (ret != 0) return ret;
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;

    try {
        GuestAllocations::Mutation mutation;
        void* mapped = nullptr;
        ret = MapAligned(nullptr, len, PROT_NONE, 0, alignment, mapped);
        if (ret != 0) return ret;

        try {
            mutation.Add(mapped, len, false, false);
        } catch (...) {
            Unmap(mapped, len);
            return SCE_KERNEL_ERROR_ENOMEM;
        }
        *addr = mapped;
        return 0;
    } catch (...) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
}

