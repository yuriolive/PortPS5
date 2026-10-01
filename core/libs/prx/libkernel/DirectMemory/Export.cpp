// Export.cpp: guest-callable libkernel direct/flexible memory exports
// (sceKernel*DirectMemory*, sceKernelMap*, sceKernelMunmap, sceKernelMprotect,
// sceKernelVirtualQuery and the not-yet-implemented siblings).
//
// Every export is APS5_VABI. Real SCE/POSIX errors are returned as codes from
// KernelErrors.hpp; the placement and unmap work lives in DirectMemory.cpp and
// the physical pool in MemoryPool.cpp. Semantics and open questions:
// docs/spec/guest-memory.md ("Mapping, unmap and direct-memory query semantics").
// Threading: the exports are stateless; the pool and the allocation registry
// take their own locks.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "DirectMemory.hpp"

extern "C" {

/**
 * Allocates a physical direct-memory block inside the search window and remembers its memory type so sceKernelDirectMemoryQuery can report it back. Returns 0, SCE_KERNEL_ERROR_EINVAL for bad arguments (window, length, alignment, null output) or SCE_KERNEL_ERROR_EAGAIN when no block fits.
 */
int APS5_VABI sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out) {
 if (search_start < 0 || search_end <= search_start || len == 0
  || (len & (PS5_PAGE_SIZE - 1)) || !phys_addr_out
  || (alignment != 0 && (alignment & (PS5_PAGE_SIZE - 1))))
  return SCE_KERNEL_ERROR_EINVAL;
 // The memory type is remembered so sceKernelDirectMemoryQuery can report it back.
 return DirectMemoryAlloc(search_start, search_end, len, alignment, memory_type, phys_addr_out);
}

/**
 * Allocates from the whole direct-memory aperture; same results as sceKernelAllocateDirectMemory.
 */
int APS5_VABI sceKernelAllocateMainDirectMemory(size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out) {
 return sceKernelAllocateDirectMemory(0, static_cast<int64_t>(DIRECT_MEMORY_SIZE), len, alignment, memory_type, phys_addr_out);
}

/**
 * Reports the largest contiguous free run inside the search window (aligned start and size). A window with nothing free reports size 0 at offset 0 and still returns 0. Returns SCE_KERNEL_ERROR_EINVAL for a null output, an empty window or an alignment that is not a power of two of at least one page.
 */
int APS5_VABI sceKernelAvailableDirectMemorySize(int64_t search_start, int64_t search_end, size_t alignment, int64_t* phys_addr_out, size_t* size_out) {
 if (!phys_addr_out || !size_out) return SCE_KERNEL_ERROR_EINVAL;
 const size_t align = alignment == 0 ? PS5_PAGE_SIZE : alignment;
 if (search_start < 0 || search_end <= search_start || align < PS5_PAGE_SIZE || (align & (align - 1)) != 0) return SCE_KERNEL_ERROR_EINVAL;
 // Largest contiguous free run inside the window (not the distance from the first free page to
 // the window end): a title sizes its next allocation from this and must not be promised more
 // than one allocation call can return. A window with nothing free reports 0 bytes at offset 0.
 DirectMemoryLargestFreeRun(static_cast<uint64_t>(search_start), static_cast<uint64_t>(search_end), align, phys_addr_out, size_out);
 return 0;
}

/**
 * Describes the allocated run containing the offset (or, with SCE_KERNEL_DMQ_FIND_NEXT, the next one): start, end and memory type, with adjacent same-type blocks merged. Returns SCE_KERNEL_ERROR_EINVAL for bad arguments and SCE_KERNEL_ERROR_EACCES for free memory or an offset past the aperture.
 */
int APS5_VABI sceKernelDirectMemoryQuery(int64_t offset, int flags, void* info, size_t info_size) {
 if (!info || offset < 0) return SCE_KERNEL_ERROR_EINVAL;
 if (info_size < sizeof(DirectMemoryQueryInfo)) return SCE_KERNEL_ERROR_EINVAL;
 // Free memory and offsets past the aperture answer EACCES (not a fabricated one-page block).
 if (static_cast<uint64_t>(offset) >= DIRECT_MEMORY_SIZE) return SCE_KERNEL_ERROR_EACCES;
 DirectMemoryBlock run{};
 if (!DirectMemoryQueryRun(static_cast<uint64_t>(offset), (flags & SCE_KERNEL_DMQ_FIND_NEXT) != 0, &run)) return SCE_KERNEL_ERROR_EACCES;
 auto* q = static_cast<DirectMemoryQueryInfo*>(info);
 q->start = static_cast<int64_t>(run.start);
 q->end = static_cast<int64_t>(run.end);
 q->memoryType = run.memoryType;
 return 0;
}

/**
 * Returns the size of the direct-memory aperture in bytes.
 */
size_t APS5_VABI sceKernelGetDirectMemorySize(void) {
 return DIRECT_MEMORY_SIZE;
}

/**
 * Maps an allocated direct-memory range into the guest address space (see DoMapDirect for flags, hint and fixed-address rules).
 */
int APS5_VABI sceKernelMapDirectMemory(void** addr, size_t len, int prot, int flags, int64_t direct_memory_start, size_t alignment) {
 (void)alignment;
 return DoMapDirect(addr, len, prot, flags, direct_memory_start, alignment);
}

/**
 * Like sceKernelMapDirectMemory with a type argument that is accepted and ignored.
 */
int APS5_VABI sceKernelMapDirectMemory2(void** addr, size_t len, int type, int prot, int flags, int64_t direct_memory_start, size_t alignment) {
 (void)type;
 return DoMapDirect(addr, len, prot, flags, direct_memory_start, alignment);
}

/**
 * Maps anonymous flexible memory (see DoMapAnon for flags, hint and fixed-address rules).
 */
int APS5_VABI sceKernelMapFlexibleMemory(void** addr_in_out, size_t len, int prot, int flags) {
 return DoMapAnon(addr_in_out, len, prot, flags);
}

/**
 * Like sceKernelMapDirectMemory; the name is accepted and not recorded (virtual range names are deferred, see the spec).
 */
int APS5_VABI sceKernelMapNamedDirectMemory(void** addr, size_t len, int prot, int flags, int64_t direct_memory_start, size_t alignment, const char* name) {
 (void)name;
 return DoMapDirect(addr, len, prot, flags, direct_memory_start, alignment);
}

/**
 * Like sceKernelMapFlexibleMemory; the name is accepted and not recorded.
 */
int32_t APS5_VABI sceKernelMapNamedFlexibleMemory(void** addr_in_out, size_t len, int prot, int flags, const char* name) {
 (void)name;
 return DoMapAnon(addr_in_out, len, prot, flags);
}

/**
 * Changes the guest protection of a page-rounded range (DoMprotect). Returns 0, SCE_KERNEL_ERROR_EINVAL, SCE_KERNEL_ERROR_EACCES or SCE_KERNEL_ERROR_EFAULT.
 */
int APS5_VABI sceKernelMprotect(const void* addr, size_t len, int prot) {
 return DoMprotect(addr, len, prot);
}

/**
 * Unmaps a page-aligned range; it may span several mappings, holes and unregistered gaps (DoMunmap). Returns 0 or SCE_KERNEL_ERROR_EINVAL (misaligned, wrapping, nothing registered, image memory or pinned by the GPU).
 */
int APS5_VABI sceKernelMunmap(uint64_t vaddr, size_t len) {
 return DoMunmap(reinterpret_cast<void*>(vaddr), len);
}

/**
 * Returns a physical direct-memory range to the pool. Returns SCE_KERNEL_ERROR_EINVAL for a negative or misaligned start, a zero or misaligned length, or a range past the aperture.
 */
int APS5_VABI sceKernelReleaseDirectMemory(int64_t start, size_t len) {
 // Range-check before touching the fixed-size page bitmap: start + len is compared via headroom so it cannot wrap.
 if (start < 0 || len == 0 || (static_cast<uint64_t>(start) & (PS5_PAGE_SIZE - 1)) || (len & (PS5_PAGE_SIZE - 1))
  || static_cast<uint64_t>(start) > DIRECT_MEMORY_SIZE || len > DIRECT_MEMORY_SIZE - static_cast<uint64_t>(start))
  return SCE_KERNEL_ERROR_EINVAL;
 DirectMemoryFree(start, len);
 return 0;
}

/**
 * Reserves an inaccessible virtual range without backing pages (DoReserveVirtual); flags are ignored.
 */
int APS5_VABI sceKernelReserveVirtualRange(void** addr, size_t len, int flags, size_t alignment) {
 (void)flags;
 return DoReserveVirtual(addr, len, alignment);
}

/**
 * Placeholder: answers with the containing page marked direct and read/write. A registry-backed query is deferred (see the spec).
 */
int APS5_VABI sceKernelVirtualQuery(const void* addr, int flags, VirtualQueryInfo* info, uint64_t info_size) {
 (void)flags;
 if (!info || info_size < sizeof(VirtualQueryInfo)) return SCE_KERNEL_ERROR_EINVAL;
 memset(info, 0, sizeof(VirtualQueryInfo));
 uintptr_t ptr = reinterpret_cast<uintptr_t>(addr);
 info->start = ptr & ~static_cast<uintptr_t>(PS5_PAGE_SIZE - 1);
 info->end = info->start + PS5_PAGE_SIZE;
 info->is_direct = 1;
 info->protection = 3;
 return 0;
}

// ---------------------------------------------------------------------------
// Moved as-is (not yet implemented) from the monolithic libkernel/Export.cpp.
// ---------------------------------------------------------------------------

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelCheckedReleaseDirectMemory(int64_t start, size_t len) {
 (void)start;
 (void)len;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelMtypeprotect(const void* addr, size_t len, int type, int prot) {
 (void)addr;
 (void)len;
 (void)type;
 (void)prot;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelQueryMemoryProtection(void* addr, void** start, void** end, int* prot) {
 (void)addr;
 (void)start;
 (void)end;
 (void)prot;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelIsStack(void* addr, void** start, void** end) {
 (void)addr;
 (void)start;
 (void)end;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelAvailableFlexibleMemorySize(size_t* size) {
 (void)size;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelConfiguredFlexibleMemorySize(size_t* size) {
 (void)size;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported); range names are deferred (bean portps5-k7qd).
 */
int APS5_VABI sceKernelSetVirtualRangeName(const void* addr, uint64_t len, const char* name) {
 (void)addr;
 (void)len;
 (void)name;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelGetPageTableStats(int* cpu_total, int* cpu_available, int* gpu_total, int* gpu_available) {
 (void)cpu_total;
 (void)cpu_available;
 (void)gpu_total;
 (void)gpu_available;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelGetPrtAperture(int index, void** addr, size_t* len) {
 (void)index;
 (void)addr;
 (void)len;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelSetPrtAperture(int index, void* addr, size_t len) {
 (void)index;
 (void)addr;
 (void)len;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelBatchMap(KernelBatchMapEntry* entries, int num_entries, int* num_entries_out) {
 (void)entries;
 (void)num_entries;
 (void)num_entries_out;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelBatchMap2(KernelBatchMapEntry* entries, int num_entries, int* num_entries_out, int flags) {
 (void)entries;
 (void)num_entries;
 (void)num_entries_out;
 (void)flags;
 NotImplemented_nid_no_patch(__func__);
 return 0;
}

}

extern "C" {

/**
 * Not implemented: aborts through the logging abort path (Unsupported), as for every unimplemented export.
 */
int APS5_VABI sceKernelMlock_nid_postfix(void* address, std::uint64_t length) {
    (void)address;
    (void)length;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
