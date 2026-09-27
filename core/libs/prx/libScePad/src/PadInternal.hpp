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
    void ReportInputFailure(std::exception_ptr error);

    // Test hook: direct manipulation of slots for deterministic unit testing
    void TestSetSlotConnected(int slot, bool connected);

private:
    PadManager();

    std::mutex mutex;
    bool initialized = false;
    std::exception_ptr failure;
    std::array<PadSlotState, PAD_MAX_SLOTS> slots;
    PadInputState hostInputState;
    std::uint64_t processStartTime = 0;
};

} // namespace Pad

#endif // CORE_LIBS_PRX_LIBSCEPAD_PADINTERNAL_HPP
