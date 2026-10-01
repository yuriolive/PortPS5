/**
 * @file GuestMemoryValidation.hpp
 * @brief Overflow-safe readable/writable range checks for guest pointers.
 *
 * Replacement libraries (PRXs) receive raw guest pointers they must treat as
 * untrusted (cpp-style.md, "Guest memory"). This API answers "may the host
 * read or write [pointer, pointer + bytes)?" without ever throwing, so an
 * APS5_VABI export can call it directly and map the status to its own error
 * code. Design and failure modes: docs/spec/guest-memory.md ("Validation API").
 *
 * Lifetime/threading: stateless; any thread may call it. The answer is a
 * point-in-time snapshot (a concurrent munmap/mprotect can invalidate it), so
 * it rejects obviously bad pointers but is not a substitute for the guest
 * keeping a buffer alive while a call uses it.
 */

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTMEMORYVALIDATION_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTMEMORYVALIDATION_HPP

#include <cstddef>
#include <cstdint>

namespace GuestMemoryValidation {

/**
 * @brief Access bits for GuestMemoryValidateRange_nid_postfix; combine with bitwise OR.
 */
enum Access : std::uint32_t {
    kRead = 1u,   ///< The host will read the range.
    kWrite = 2u,  ///< The host will write the range (does not imply a read).
};

/**
 * @brief Result of a range check. Callers translate it to their library's own error code.
 */
enum class Status : std::int32_t {
    Ok = 0,            ///< The whole range is mapped and grants the requested access.
    InvalidRange = 1,  ///< Null pointer, no access bits, or address + bytes wraps the address space.
    Unmapped = 2,      ///< Some byte of the range is not mapped.
    AccessDenied = 3,  ///< The range is mapped but a byte lacks the requested access.
};

extern "C" {
/**
 * @brief Checks that [pointer, pointer + bytes) is mapped with the requested access.
 * @param pointer First byte of the range (untrusted guest pointer).
 * @param bytes Length in bytes; 0 is always Status::Ok because nothing is touched
 *        (a null pointer with 0 bytes is still Status::InvalidRange, matching how
 *        PRXs treat a null buffer as an error regardless of size).
 * @param access Bitwise OR of Access values; 0 is Status::InvalidRange.
 * @return Status::Ok, Status::InvalidRange, Status::Unmapped or Status::AccessDenied.
 * @note Never throws and takes no caller-visible locks. Registered allocations are
 *       checked against the guest protection recorded in GuestAllocations; memory the
 *       registry does not know (thread stacks, TLS, module data) is checked against the
 *       host mapping instead.
 */
Status GuestMemoryValidateRange_nid_postfix(const void* pointer, std::size_t bytes, std::uint32_t access) noexcept;
}

/**
 * @brief Checks that the host may read [pointer, pointer + bytes).
 * @param pointer First byte of the range.
 * @param bytes Length in bytes.
 * @return See GuestMemoryValidateRange_nid_postfix.
 */
inline Status CheckReadable(const void* pointer, std::size_t bytes) noexcept {
    return GuestMemoryValidateRange_nid_postfix(pointer, bytes, kRead);
}

/**
 * @brief Checks that the host may write [pointer, pointer + bytes).
 * @param pointer First byte of the range.
 * @param bytes Length in bytes.
 * @return See GuestMemoryValidateRange_nid_postfix.
 */
inline Status CheckWritable(const void* pointer, std::size_t bytes) noexcept {
    return GuestMemoryValidateRange_nid_postfix(pointer, bytes, kWrite);
}

/**
 * @brief Convenience for validating a whole guest object (parameter structs).
 * @tparam T Type whose sizeof is checked.
 * @param object Pointer to the guest object.
 * @return CheckReadable(object, sizeof(T)).
 */
template <typename T>
inline Status CheckReadableObject(const T* object) noexcept {
    return CheckReadable(object, sizeof(T));
}

/**
 * @brief Convenience for validating a whole guest object the host will fill in.
 * @tparam T Type whose sizeof is checked.
 * @param object Pointer to the guest object.
 * @return CheckWritable(object, sizeof(T)).
 */
template <typename T>
inline Status CheckWritableObject(T* object) noexcept {
    return CheckWritable(object, sizeof(T));
}

}  // namespace GuestMemoryValidation

#endif
