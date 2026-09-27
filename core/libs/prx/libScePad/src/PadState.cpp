#include "PadInternal.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Pad {

PadManager& PadManager::Get() {
    static PadManager instance;
    return instance;
}

PadManager::PadManager() {
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i].opened = false;
        slots[i].userId = -1;
        slots[i].type = 0;
        slots[i].connected = (i == 0); // Slot 0 has default keyboard/mouse virtual pad
        slots[i].connectedCount = (i == 0 ? 1 : 0);
        slots[i].lastTimestamp = 0;
        slots[i].lastData = {};
        slots[i].lastData.acceleration_y = 1.0f;
        slots[i].lastData.orientation_w = 1.0f;
        slots[i].lastData.connected = (i == 0);
        slots[i].lastData.connected_count = (i == 0 ? 1 : 0);
        slots[i].lastData.left_stick_x = 128;
        slots[i].lastData.left_stick_y = 128;
        slots[i].lastData.right_stick_x = 128;
        slots[i].lastData.right_stick_y = 128;
    }
}

void PadManager::Initialize() {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    if (!initialized) {
        processStartTime = sceKernelGetProcessTime();
        slots[0].lastTimestamp = processStartTime;
        slots[0].lastData.timestamp = processStartTime;
        initialized = true;
    }
}

int PadManager::Open(int userId, int type, int index, int& outHandle) {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    if (!initialized) {
        Initialize();
    }
    if (index < 0 || index >= PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_ARG;
    }
    const bool personalPort = (type == PAD_PORT_TYPE_STANDARD || type == PAD_PORT_TYPE_SPECIAL);
    const bool systemRemote = (userId == PAD_USER_ID_SYSTEM && type == PAD_PORT_TYPE_REMOTE);
    if (!personalPort && !systemRemote) {
        return PAD_ERROR_INVALID_ARG;
    }

    auto& slot = slots[static_cast<std::size_t>(index)];
    slot.opened = true;
    slot.userId = userId;
    slot.type = type;
    if (index == 0) {
        slot.connected = true;
        if (slot.connectedCount == 0) slot.connectedCount = 1;
    }
    outHandle = index + 1; // 1-based handle
    return PAD_OK;
}

int PadManager::Close(int handle) {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    slot.opened = false;
    return PAD_OK;
}

int PadManager::Read(int handle, PadData* data, int num) {
    if (data == nullptr || num <= 0) {
        return PAD_ERROR_INVALID_ARG;
    }
    const int result = ReadState(handle, data);
    if (result != PAD_OK) {
        return result;
    }
    return 1;
}

int PadManager::ReadState(int handle, PadData* data) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    if (data == nullptr) {
        return PAD_ERROR_INVALID_ARG;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    if (!initialized) {
        return PAD_ERROR_NOT_INITIALIZED;
    }

    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }

    // Refresh timestamp monotonically on every read
    const std::uint64_t currentProcessTime = sceKernelGetProcessTime();
    if (currentProcessTime > slot.lastTimestamp) {
        slot.lastTimestamp = currentProcessTime;
    } else {
        ++slot.lastTimestamp;
    }

    if (handle == 1) {
        // Slot 0 merges hostInputState
        slot.lastData.buttons = hostInputState.buttons;
        slot.lastData.left_stick_x = hostInputState.sticks[0];
        slot.lastData.left_stick_y = hostInputState.sticks[1];
        slot.lastData.right_stick_x = hostInputState.sticks[2];
        slot.lastData.right_stick_y = hostInputState.sticks[3];
        slot.lastData.analog_buttons_l2 = (hostInputState.buttons & 0x100) != 0 ? 255 : 0;
        slot.lastData.analog_buttons_r2 = (hostInputState.buttons & 0x200) != 0 ? 255 : 0;
        if (hostInputState.touchLeft || hostInputState.touchRight) {
            slot.lastData.buttons |= 0x100000;
            slot.lastData.touch_data_touch_num = 1;
            slot.lastData.touch_data_touch0_x = hostInputState.touchRight ? 1440 : 480;
            slot.lastData.touch_data_touch0_y = 471;
        } else {
            slot.lastData.touch_data_touch_num = 0;
        }
    }

    slot.lastData.connected = slot.connected;
    slot.lastData.connected_count = slot.connectedCount;
    slot.lastData.timestamp = slot.lastTimestamp;

    *data = slot.lastData;
    return PAD_OK;
}

int PadManager::GetControllerInformation(int handle, PadControllerInformation* info) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    if (info == nullptr) {
        return PAD_ERROR_INVALID_ARG;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);

    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }

    std::memset(info, 0, sizeof(*info));
    info->touchPadInfo.pixelDensity = 44.86f;
    info->touchPadInfo.resolution.x = 1920;
    info->touchPadInfo.resolution.y = 943;
    info->stickInfo.deadZoneLeft = 2;
    info->stickInfo.deadZoneRight = 2;
    info->connectionType = PAD_CONNECTION_TYPE_LOCAL;
    info->connectedCount = slot.connectedCount;
    info->connected = slot.connected;
    info->deviceClass = PAD_DEVICE_CLASS_STANDARD;
    return PAD_OK;
}

int PadManager::SetMotionSensorState(int handle, bool enable) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    slot.motionSensorEnabled = enable;
    return PAD_OK;
}

int PadManager::SetVibration(int handle, const PadVibrationParam* param) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    if (param == nullptr) {
        return PAD_ERROR_INVALID_ARG;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    slot.vibration = *param;
    return PAD_OK;
}

int PadManager::SetLightBar(int handle, const PadLightBarParam* param) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    if (param == nullptr) {
        return PAD_ERROR_INVALID_ARG;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    slot.lightBar = *param;
    return PAD_OK;
}

int PadManager::ResetLightBar(int handle) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    slot.lightBar = {};
    return PAD_OK;
}

void PadManager::PublishInput(const PadInputState& input) {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    hostInputState = input;
}

void PadManager::ReportInputFailure(std::exception_ptr error) {
    if (!error) return;
    std::lock_guard lock(mutex);
    if (!failure) failure = error;
}

void PadManager::TestSetSlotConnected(int slot, bool connected) {
    if (slot < 0 || slot >= PAD_MAX_SLOTS) return;
    std::lock_guard lock(mutex);
    auto& s = slots[static_cast<std::size_t>(slot)];
    if (s.connected != connected) {
        s.connected = connected;
        if (connected) {
            ++s.connectedCount;
        }
    }
}

void Initialize() {
    PadManager::Get().Initialize();
}

PadData ReadState() {
    PadData data{};
    const int res = PadManager::Get().ReadState(PAD_HANDLE, &data);
    if (res != PAD_OK) {
        throw std::runtime_error("Pad: read state failed");
    }
    return data;
}

} // namespace Pad

extern "C" void PadPublishInput_nid_postfix(const PadInputState& input) {
    Pad::PadManager::Get().PublishInput(input);
}

extern "C" void PadReportInputFailure_nid_postfix(std::exception_ptr error) {
    Pad::PadManager::Get().ReportInputFailure(error);
}
