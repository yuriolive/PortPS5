/**
 * @file GuestAllocations.hpp
 * @brief Registry of guest-visible allocations (ranges, guest protection, GPU pins).
 *
 * Owned by libc; libkernel registers mappings, the heap registers blocks and the
 * AGC driver leases ranges. All mutation happens under the tracking mutex held by
 * Mutation. The `_nid_postfix` exports are Windows-ABI host calls between PRXs,
 * not guest-callable APS5_VABI functions.
 */

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTALLOCATIONS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTALLOCATIONS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

namespace GuestAllocations {

struct Range {
    std::uint64_t address;
    std::size_t bytes;
    bool readable;
    bool writable;
    std::uint64_t allocationAddress;
    std::size_t allocationBytes;
    bool releasable = true;
};

using Lease = std::vector<std::shared_ptr<const Range>>;

extern "C" {
void* GuestAllocationsBegin_nid_postfix();
void GuestAllocationsRegisterMainImage_nid_postfix(void* mutation);
void GuestAllocationsEnd_nid_postfix(void* mutation) noexcept;
void GuestAllocationsAdd_nid_postfix(void* mutation, void* pointer, std::size_t bytes, bool readable, bool writable);
void GuestAllocationsRequireUnpinned_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
void GuestAllocationsRequireAvailable_nid_postfix(void* mutation, const void* pointer, std::size_t bytes);
Range GuestAllocationsFind_nid_postfix(void* mutation, const void* pointer);
void GuestAllocationsRemove_nid_postfix(void* mutation, const void* pointer);
void GuestAllocationsProtect_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, bool readable, bool writable, const std::function<void()>& apply);
void GuestAllocationsUnmap_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, const std::function<void(const void*, bool)>& apply);
Lease GuestAllocationsAcquire_nid_postfix();
/**
 * @brief Classification returned by GuestAllocationsCover_nid_postfix.
 */
enum class Coverage : std::int32_t {
    Covered = 0,  ///< Every byte lies in registered ranges that grant the access.
    Denied = 1,   ///< A registered range overlapping the request lacks the access.
    Gap = 2,      ///< The first byte at or after the start is not registered.
};

/**
 * @brief Checks how the registry covers [address, address + bytes) without throwing.
 * @param address First byte of the request; the caller guarantees bytes != 0 and no wrap.
 * @param bytes Length of the request in bytes.
 * @param writable True to require write permission, false to require read permission.
 * @param gapStart Receives, for Coverage::Gap, the first uncovered byte.
 * @param gapEnd Receives, for Coverage::Gap, the exclusive end of the unregistered run that
 *        starts at gapStart (clamped to the request end).
 * @return Coverage::Covered, Coverage::Denied or Coverage::Gap.
 * @note Takes the registry mutex itself. Never throws; safe from any thread.
 */
Coverage GuestAllocationsCover_nid_postfix(std::uint64_t address, std::uint64_t bytes, bool writable, std::uint64_t* gapStart, std::uint64_t* gapEnd) noexcept;
}

class Mutation {
public:
    Mutation() : handle(GuestAllocationsBegin_nid_postfix()) {}
    ~Mutation() { GuestAllocationsEnd_nid_postfix(handle); }
    Mutation(const Mutation&) = delete;
    Mutation& operator=(const Mutation&) = delete;
    void RegisterMainImage() { GuestAllocationsRegisterMainImage_nid_postfix(handle); }
    void Add(void* pointer, std::size_t bytes, bool readable, bool writable) { GuestAllocationsAdd_nid_postfix(handle, pointer, bytes, readable, writable); }
    void RequireUnpinned(const void* pointer, std::size_t bytes) const { GuestAllocationsRequireUnpinned_nid_postfix(handle, pointer, bytes); }
    void RequireAvailable(const void* pointer, std::size_t bytes) const { GuestAllocationsRequireAvailable_nid_postfix(handle, pointer, bytes); }
    Range Find(const void* pointer) const { return GuestAllocationsFind_nid_postfix(handle, pointer); }
    void Remove(const void* pointer) { GuestAllocationsRemove_nid_postfix(handle, pointer); }
    void Unmap(const void* pointer, std::size_t bytes, const std::function<void(const void*, bool)>& apply) { GuestAllocationsUnmap_nid_postfix(handle, pointer, bytes, apply); }
    void Protect(const void* pointer, std::size_t bytes, bool readable, bool writable, const std::function<void()>& apply) { GuestAllocationsProtect_nid_postfix(handle, pointer, bytes, readable, writable, apply); }

private:
    void* handle;
};

}

#endif
