// core/libs/prx/libSceAgcDriver/Graphics/include/SnapshotValidity.hpp
//
// Subsystem: AGC driver, per-draw prepare path (docs/spec/gpu-driver.md, "Per-draw CPU cost", slice 2).
// Purpose: decides whether a cached copy ("snapshot") of guest memory is still current, so the TextureCache can
// skip the whole-texture compare when the write tracker proves nothing wrote the range since the snapshot.
// Contract (docs/spec/guest-memory.md, IWriteTracker::Collect): a nonzero generation that equals the stored one
// proves no CPU write and no recorded GPU write (MarkWritten) touched the range since it was stored. Zero means
// "unknown" and always falls back to comparing bytes, so a null tracker, a NullTracker, non-write-watch memory
// and a failed walk all keep the exact pre-existing behaviour.
// Threading: stateless apart from the caller's stamp; Collect is thread-safe in every tracker.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_SNAPSHOTVALIDITY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_SNAPSHOTVALIDITY_HPP

#include "prx/libSceAgcDriver/Graphics/include/BytesEqual.hpp"
#include "prx/libc/include/WriteTracker.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace AgcDriver::Graphics {

/**
 * @brief Reads the tracker generation for a range that is about to be snapshotted.
 * @param tracker Write tracker, or nullptr when none is wired.
 * @param address First byte of the guest range.
 * @param bytes Length of the range.
 * @return Newest block generation, or 0 (unknown) without a tracker.
 * Call this BEFORE copying the guest bytes: a write that lands between the two calls then carries a newer
 * generation than the stamp, so the next check sees a change and compares bytes. The reverse order could stamp a
 * generation that already includes a write the snapshot missed.
 */
inline std::uint64_t StampSnapshot(PortPS5::GuestMemory::IWriteTracker* tracker, std::uint64_t address, std::size_t bytes) {
    return tracker != nullptr ? tracker->Collect(address, bytes) : 0;
}

/**
 * @brief Reports whether a snapshot still equals live guest memory, skipping the compare when provable.
 * @param tracker Write tracker, or nullptr when none is wired.
 * @param address First byte of the live guest range.
 * @param snapshot Cached bytes; its size is the compared range.
 * @param stamp In: generation stored with the snapshot (0 = unknown). Out: refreshed after a successful compare.
 * @return true when the snapshot is current. A nonzero matching generation returns true with no byte compare; any
 * other case compares with BytesEqual. On a mismatch the stamp is reset to 0 and the caller must discard the snapshot.
 */
inline bool SnapshotStillValid(PortPS5::GuestMemory::IWriteTracker* tracker, std::uint64_t address, std::span<const std::byte> snapshot, std::uint64_t& stamp) {
    const auto current = StampSnapshot(tracker, address, snapshot.size());
    if (current != 0 && current == stamp) return true;
    if (!BytesEqual(reinterpret_cast<const void*>(address), snapshot.data(), snapshot.size())) {
        stamp = 0;
        return false;
    }
    // Equal bytes at generation `current`: remember it so the next draw can skip the compare.
    stamp = current;
    return true;
}

}

#endif
