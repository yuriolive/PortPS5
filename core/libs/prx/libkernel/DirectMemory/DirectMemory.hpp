#ifndef CORE_LIBS_PRX_LIBKERNEL_DIRECTMEMORY_DIRECTMEMORY_HPP
#define CORE_LIBS_PRX_LIBKERNEL_DIRECTMEMORY_DIRECTMEMORY_HPP

#include <cstdint>
#include <cstddef>

static constexpr size_t DIRECT_MEMORY_SIZE = 13824ULL * 1024 * 1024;
static constexpr size_t PS5_PAGE_SIZE = 0x4000;

static constexpr int SCE_KERNEL_ERROR_EINVAL = -2147418107;
static constexpr int SCE_KERNEL_ERROR_EAGAIN = -2147418110;
static constexpr int SCE_KERNEL_ERROR_ENOMEM = -2147418105;
static constexpr int SCE_KERNEL_ERROR_EACCES = -2147418108;
static constexpr int SCE_KERNEL_ERROR_EFAULT = -2147418103;

int DirectMemoryAlloc(int64_t searchStart, int64_t searchEnd, size_t len, size_t alignment, int64_t* physOut);
void DirectMemoryFree(int64_t start, size_t len);
int DoMapDirect(void** addr, size_t len, int prot, int flags, int64_t physStart, size_t alignment);
int DoMapAnon(void** addr, size_t len, int prot, int flags);
int DoMprotect(const void* addr, size_t len, int prot);
int DoMunmap(void* addr, size_t len);
int DoReserveVirtual(void** addr, size_t len, size_t alignment);

#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out);
int APS5_VABI sceKernelAllocateMainDirectMemory(size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out);
int APS5_VABI sceKernelAvailableDirectMemorySize(int64_t search_start, int64_t search_end, size_t alignment, int64_t* phys_addr_out, size_t* size_out);
size_t APS5_VABI sceKernelGetDirectMemorySize(void);
int APS5_VABI sceKernelMapDirectMemory(void** addr, size_t len, int prot, int flags, int64_t direct_memory_start, size_t alignment);
int APS5_VABI sceKernelMapFlexibleMemory(void** addr_in_out, size_t len, int prot, int flags);
int APS5_VABI sceKernelMprotect(const void* addr, size_t len, int prot);
int APS5_VABI sceKernelMunmap(uint64_t vaddr, size_t len);
int APS5_VABI sceKernelReleaseDirectMemory(int64_t start, size_t len);
int APS5_VABI sceKernelReserveVirtualRange(void** addr, size_t len, int flags, size_t alignment);
}

#endif