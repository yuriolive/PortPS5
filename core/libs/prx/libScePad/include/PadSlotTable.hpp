// Pure pad slot assignment for controller hot-plug (no SDL, no global state).
//
// Subsystem: input (docs/spec/input.md §Target design 3, §Failure modes). The
// VideoOut window thread owns one SlotTable and feeds it SDL device added and
// removed events; unit tests drive it with synthetic instance ids and GUIDs.
// Policy: a reconnecting device returns to the slot it last held when that slot
// is free, any other device takes the lowest free slot (so a replacement pad
// lands in slot 0, the only slot single-player titles read, instead of being
// parked behind a reservation for the pad that left). A fifth simultaneous
// controller is refused, never an error.
// Thread-safety: none; single-threaded (window thread) use only.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADSLOTTABLE_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADSLOTTABLE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pad {

/** Opaque 16-byte device identity (SDL_JoystickGUID bytes). */
using DeviceGuid = std::array<std::uint8_t, 16>;

/** Outcome of SlotTable::Attach. */
enum class AttachStatus {
    Assigned,   ///< `slot` holds the pad slot
    Duplicate,  ///< the instance id is already attached; nothing changed
    Full,       ///< every slot is occupied; the device is ignored
};

/** Result of SlotTable::Attach. */
struct AttachResult {
    AttachStatus status = AttachStatus::Full;
    std::size_t slot = 0;  ///< valid only when status == Assigned
};

/** Four-slot controller table with per-slot memory of the last device GUID. */
class SlotTable {
public:
    /** Number of pad slots (matches PAD_MAX_SLOTS). */
    static constexpr std::size_t kSlots = 4;

    /**
     * @brief Assigns a slot to a newly connected device.
     *
     * Order: (1) a free slot last held by the same GUID, (2) the lowest free
     * slot (its memory is replaced).
     *
     * @param instanceId SDL joystick instance id; unique per connection.
     * @param guid Device identity used to restore a previous slot.
     * @return Assigned with the slot, Duplicate if the id is attached, or Full.
     */
    AttachResult Attach(std::int32_t instanceId, const DeviceGuid& guid) {
        if (SlotOf(instanceId) != kNone) return {AttachStatus::Duplicate, 0};
        std::size_t chosen = kNone;
        for (std::size_t i = 0; i < kSlots && chosen == kNone; ++i) {
            if (!slots_[i].occupied && slots_[i].hasMemory && slots_[i].remembered == guid) chosen = i;
        }
        for (std::size_t i = 0; i < kSlots && chosen == kNone; ++i) {
            if (!slots_[i].occupied) chosen = i;
        }
        if (chosen == kNone) return {AttachStatus::Full, 0};
        slots_[chosen] = {true, instanceId, true, guid};
        return {AttachStatus::Assigned, chosen};
    }

    /**
     * @brief Frees the slot held by a disconnected device.
     *
     * The slot remembers the device GUID so it can reclaim the same slot.
     *
     * @param instanceId SDL joystick instance id.
     * @return The freed slot, or kNone if the id was not attached.
     */
    std::size_t Detach(std::int32_t instanceId) {
        const std::size_t slot = SlotOf(instanceId);
        if (slot == kNone) return kNone;
        slots_[slot].occupied = false;
        slots_[slot].instanceId = -1;
        return slot;
    }

    /**
     * @brief Looks up the slot of an attached device.
     * @param instanceId SDL joystick instance id.
     * @return The slot, or kNone.
     */
    std::size_t SlotOf(std::int32_t instanceId) const {
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (slots_[i].occupied && slots_[i].instanceId == instanceId) return i;
        }
        return kNone;
    }

    /** Sentinel returned by Detach and SlotOf for "no slot". */
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

private:
    struct Slot {
        bool occupied = false;
        std::int32_t instanceId = -1;
        bool hasMemory = false;
        DeviceGuid remembered{};
    };
    std::array<Slot, kSlots> slots_{};
};

} // namespace Pad

#endif
