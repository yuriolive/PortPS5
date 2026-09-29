/**
 * @file DirectMemory.hpp
 * @brief Declarations of PS5 kernel direct memory management and memory pool exports.
 * 
 * Defines host-backed physical memory allocation, virtual mapping, protection,
 * and memory pool interfaces exposed via System V ABI (APS5_VABI).
 */

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

#include "MemoryPool.hpp"
int DoMapDirect(void** addr, size_t len, int prot, int flags, int64_t physStart, size_t alignment);
int DoMapAnon(void** addr, size_t len, int prot, int flags);
int DoMprotect(const void* addr, size_t len, int prot);
int DoMunmap(void* addr, size_t len);
int DoReserveVirtual(void** addr, size_t len, size_t alignment);

#include "prx/libc/include/general/VabiMacros.hpp"

struct KernelMemoryPoolBatchEntry;
struct KernelMemoryPoolBlockStats;

extern "C" {
/**
 * @brief Allocates a range of physical direct memory within a specified address window.
 * @param search_start Lower bound of search window.
 * @param search_end Upper bound of search window.
 * @param len Size in bytes to allocate (must be page aligned).
 * @param alignment Required physical alignment.
 * @param memory_type PS5 memory type / attribute flags.
 * @param phys_addr_out Pointer receiving the allocated physical address.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out);

/**
 * @brief Allocates direct memory from the main physical direct memory space.
 * @param len Size in bytes to allocate (must be page aligned).
 * @param alignment Required physical alignment.
 * @param memory_type PS5 memory type / attribute flags.
 * @param phys_addr_out Pointer receiving the allocated physical address.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelAllocateMainDirectMemory(size_t len, size_t alignment, int memory_type, int64_t* phys_addr_out);

/**
 * @brief Queries available contiguous direct memory size within a search range.
 * @param search_start Lower bound of search window.
 * @param search_end Upper bound of search window.
 * @param alignment Required physical alignment.
 * @param phys_addr_out Output pointer receiving the start address of the free range.
 * @param size_out Output pointer receiving the contiguous free size in bytes.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelAvailableDirectMemorySize(int64_t search_start, int64_t search_end, size_t alignment, int64_t* phys_addr_out, size_t* size_out);

/**
 * @brief Returns the total size of physical direct memory in bytes.
 * @return Direct memory pool capacity in bytes.
 */
size_t APS5_VABI sceKernelGetDirectMemorySize(void);

/**
 * @brief Maps allocated physical direct memory into the guest virtual address space.
 * @param addr Virtual address hint or fixed address pointer.
 * @param len Size of mapping in bytes.
 * @param prot Protection flags (read/write/exec).
 * @param flags Mapping flags.
 * @param direct_memory_start Physical address to map.
 * @param alignment Alignment requirements.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMapDirectMemory(void** addr, size_t len, int prot, int flags, int64_t direct_memory_start, size_t alignment);

/**
 * @brief Maps anonymous flexible memory into the guest address space.
 * @param addr_in_out Pointer to base virtual address hint/result.
 * @param len Size of mapping in bytes.
 * @param prot Protection flags.
 * @param flags Mapping flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMapFlexibleMemory(void** addr_in_out, size_t len, int prot, int flags);

/**
 * @brief Changes virtual memory protection flags.
 * @param addr Target virtual address.
 * @param len Size of memory range.
 * @param prot New protection flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMprotect(const void* addr, size_t len, int prot);

/**
 * @brief Unmaps virtual memory pages.
 * @param vaddr Base virtual address.
 * @param len Size of range to unmap.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMunmap(uint64_t vaddr, size_t len);

/**
 * @brief Releases physical direct memory back to the direct memory allocator.
 * @param start Physical start address.
 * @param len Size in bytes to release.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelReleaseDirectMemory(int64_t start, size_t len);

/**
 * @brief Reserves a virtual address range without allocating physical backing.
 * @param addr Pointer receiving the reserved virtual address.
 * @param len Size in bytes to reserve.
 * @param flags Reservation flags.
 * @param alignment Virtual address alignment.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelReserveVirtualRange(void** addr, size_t len, int flags, size_t alignment);

/**
 * @brief Executes a batch of memory pool operations (commit, decommit, protect).
 * @param entries Array of batch operation descriptors.
 * @param num_entries Number of entries in the array.
 * @param num_entries_out Output receiving number of processed entries.
 * @param flags Operational flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolBatch(const KernelMemoryPoolBatchEntry* entries, int num_entries, int* num_entries_out, int flags);

/**
 * @brief Commits backing memory pages for a memory pool range.
 * @param addr Base address to commit.
 * @param len Size in bytes to commit.
 * @param type Memory pool type.
 * @param prot Protection flags.
 * @param flags Operational flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolCommit(void* addr, size_t len, int type, int prot, int flags);

/**
 * @brief Decommits memory pages from a memory pool range.
 * @param addr Base address to decommit.
 * @param len Size in bytes to decommit.
 * @param flags Operational flags.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolDecommit(void* addr, size_t len, int flags);

/**
 * @brief Expands the physical backing space of a memory pool.
 * @param search_start Lower bound of physical search range.
 * @param search_end Upper bound of physical search range.
 * @param len Size in bytes to expand.
 * @param alignment Physical alignment requirement.
 * @param phys_addr_out Output receiving allocated physical address.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolExpand(int64_t search_start, int64_t search_end, size_t len, size_t alignment, int64_t* phys_addr_out);

/**
 * @brief Retrieves block statistics for the memory pool.
 * @param output Pointer to block statistics structure.
 * @param output_size Size of the output structure buffer.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolGetBlockStats(KernelMemoryPoolBlockStats* output, size_t output_size);

/**
 * @brief Reserves a virtual address range for memory pool usage.
 * @param addr_in Optional requested address hint.
 * @param len Size in bytes to reserve.
 * @param alignment Alignment requirements.
 * @param flags Reservation flags.
 * @param addr_out Pointer receiving the reserved virtual address.
 * @return 0 on success, or SCE error code on failure.
 */
int APS5_VABI sceKernelMemoryPoolReserve(void* addr_in, size_t len, size_t alignment, int flags, void** addr_out);
}


#endif