// libScePad internal PadManager: per-slot state behind scePadRead and friends.
// Subsystem: input (docs/spec/input.md). Owns four pad slots (handle = slot+1),
// the keyboard/mouse host sample (slot 0) and per-slot controller samples.
// Threading: every method takes the single `mutex`; scePad* exports run on guest
// threads while publishers run on the VideoOut window thread.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADINTERNAL_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADINTERNAL_HPP

#include <array>
#include <cstdint>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadMotion.hpp"
#include "prx/libScePad/include/PadOutputMapping.hpp"
#include "prx/libScePad/include/PadState.hpp"

namespace Pad {

struct PadSlotState {
    bool opened = false;
    int userId = -1;
    int type = 0;
    bool connected = false;
    uint8_t connectedCount = 0;
    std::uint64_t lastTimestamp = 0;
    PadData lastData{};
    PadVibrationParam vibration{};
    PadLightBarParam lightBar{};
    // Guest output requests for this slot (rumble, light bar, triggers, motion
    // enable), fetched by the window thread through FetchOutput.
    PadOutputState output;
    Pad::MotionFusion fusion;         // orientation estimate for this slot's controller
    Pad::TouchIdTracker touchIds;     // contact ids for this slot's touchpad fingers
    std::uint64_t lastFuseTime = 0;   // process time (us) of the previous fusion step
    bool controllerPresent = false;   // a physical SDL controller owns this slot
    PadInputState controllerInput;    // latest controller sample for this slot
};

class PadManager {
public:
    static PadManager& Get();

    void Initialize();
    int Open(int userId, int type, int index, int& outHandle);
    int Close(int handle);
    int Read(int handle, PadData* data, int num);
    int ReadState(int handle, PadData* data);
    int GetControllerInformation(int handle, PadControllerInformation* info);
    int SetMotionSensorState(int handle, bool enable);
    int SetVibration(int handle, const PadVibrationParam* param);
    int SetLightBar(int handle, const PadLightBarParam* param);
    int ResetLightBar(int handle);
    /**
     * @brief Validates a handle without touching state.
     * @param handle Pad handle (slot + 1).
     * @return PAD_OK, or PAD_ERROR_INVALID_HANDLE for out-of-range or closed handles.
     */
    int CheckOpenHandle(int handle);
    int ResetOrientation(int handle);
    int SetVibrationMode(int handle, int mode);
    /**
     * @brief Applies a parsed guest trigger effect update to a slot.
     *
     * @param handle Pad handle (slot + 1).
     * @param update Parsed update; only triggers selected in its mask change.
     * @return PAD_OK, PAD_ERROR_INVALID_HANDLE for a bad or closed handle.
     */
    int SetTriggerEffect(int handle, const Pad::TriggerEffectUpdate& update);
    /**
     * @brief Copies a slot's output request when it changed since `*seenSequence`.
     *
     * @param slot Slot 0..PAD_MAX_SLOTS-1; out of range returns false.
     * @param seenSequence In/out: last sequence the caller applied; updated on copy.
     * @param out Receives the request on a change.
     * @return true when a copy was made.
     */
    bool FetchOutput(int slot, std::uint32_t* seenSequence, PadOutputState* out);

    void PublishInput(const PadInputState& input);
    // Controller source for `slot` (0..3). Merged with keyboard/mouse on slot 0.
    void PublishControllerInput(int slot, const PadInputState& input);
    void SetControllerConnected(int slot, bool connected);
    void ReportInputFailure(std::exception_ptr error);

    // Test hook: direct manipulation of slots for deterministic unit testing
    void TestSetSlotConnected(int slot, bool connected);
    /**
     * @brief Test hook: return a slot to its freshly-constructed state.
     *
     * Closes the slot, drops any controller, and restores connectedCount, the
     * connected flag and the timestamp baseline so singleton state does not leak
     * between tests.
     *
     * @param slot Slot index 0..PAD_MAX_SLOTS-1; out-of-range values are ignored.
     */
    void TestResetSlot(int slot);

private:
    PadManager();

    void InitializeInternal();
    void ResetOutput(PadSlotState& slot);
    void FillMotionAndTouch(PadSlotState& slot, const PadInputState* host, std::uint64_t now);

    std::mutex mutex;
    bool initialized = false;
    std::exception_ptr failure;
    std::array<PadSlotState, PAD_MAX_SLOTS> slots;
    PadInputState hostInputState;
    std::uint64_t processStartTime = 0;
};

} // namespace Pad

#endif // CORE_LIBS_PRX_LIBSCEPAD_PADINTERNAL_HPP
