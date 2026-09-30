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
    bool motionSensorEnabled = false;
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

    void PublishInput(const PadInputState& input);
    // Controller source for `slot` (0..3). Merged with keyboard/mouse on slot 0.
    void PublishControllerInput(int slot, const PadInputState& input);
    void SetControllerConnected(int slot, bool connected);
    void ReportInputFailure(std::exception_ptr error);

    // Test hook: direct manipulation of slots for deterministic unit testing
    void TestSetSlotConnected(int slot, bool connected);

private:
    PadManager();

    void InitializeInternal();

    std::mutex mutex;
    bool initialized = false;
    std::exception_ptr failure;
    std::array<PadSlotState, PAD_MAX_SLOTS> slots;
    PadInputState hostInputState;
    std::uint64_t processStartTime = 0;
};

} // namespace Pad

#endif // CORE_LIBS_PRX_LIBSCEPAD_PADINTERNAL_HPP
