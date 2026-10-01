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
/**
 * @brief Unmaps [pointer, pointer + bytes), which may span several allocations and unregistered gaps.
 * @param mutation Handle from GuestAllocationsBegin.
 * @param pointer First byte of the range.
 * @param bytes Length of the range; must be non-zero and must not wrap.
 * @param apply Called once per registered piece before the registry is updated, as
 *        apply(piece, pieceBytes, allocationBase, lastFragment). It performs the host unmap of
 *        exactly that piece. If it throws, earlier pieces stay unmapped and the registry matches.
 * @note Like FreeBSD munmap, holes inside the range are ignored. Throws std::runtime_error if
 *       nothing in the range is registered, if any piece is not releasable (image memory; checked
 *       before the first apply), or if a piece is pinned by an active GPU command.
 */
void GuestAllocationsUnmap_nid_postfix(void* mutation, const void* pointer, std::size_t bytes, const std::function<void(const void*, std::size_t, const void*, bool)>& apply);
Lease GuestAllocationsAcquire_nid_postfix();
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
    void Unmap(const void* pointer, std::size_t bytes, const std::function<void(const void*, std::size_t, const void*, bool)>& apply) { GuestAllocationsUnmap_nid_postfix(handle, pointer, bytes, apply); }
    void Protect(const void* pointer, std::size_t bytes, bool readable, bool writable, const std::function<void()>& apply) { GuestAllocationsProtect_nid_postfix(handle, pointer, bytes, readable, writable, apply); }

private:
    void* handle;
};

}

#endif
