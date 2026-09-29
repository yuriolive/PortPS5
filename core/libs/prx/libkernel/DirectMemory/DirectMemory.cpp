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

int ValidateLength(size_t len) {
    if (len == 0 || (len & (PS5_PAGE_SIZE - 1)) != 0) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

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

int ValidateRange(const void* addr, size_t len, size_t alignment) {
    int ret = ValidateLength(len);
    if (ret != 0) return ret;
    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    if (!addr || (start & (alignment - 1)) != 0 || len > std::numeric_limits<std::uintptr_t>::max() - start) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

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

void Unmap(void* addr, size_t len) {
    GuestMemoryBacking::GuestMemoryBackingUnmap_nid_postfix(addr, len);
}

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

int ValidateOutput(void** addr) {
    if (!addr) {
        return SCE_KERNEL_ERROR_EINVAL;
    }
    return 0;
}

}

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

