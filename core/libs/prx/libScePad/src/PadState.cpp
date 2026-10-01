// libScePad PadManager implementation: slot lifecycle, scePadRead merge logic
// and cross-prx publish entry points.
// Subsystem: input (docs/spec/input.md). Slot 0 merges keyboard/mouse with its
// controller; slots 1..3 are controller-only and open only while connected.
// Threading: one short mutex per PadManager call; no guest-visible host locks.
#include "PadInternal.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include <algorithm>
#include <cstdlib>
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
    InitializeInternal();
}

void PadManager::InitializeInternal() {
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
        InitializeInternal();
    }
    // Slot 0 is always backed by the keyboard/mouse virtual pad. Higher slots
    // only have an input stream while a physical controller occupies them, so
    // opening one without a controller would hand out a dead handle.
    if (index < 0 || index >= PAD_MAX_SLOTS ||
        (index != 0 && !slots[static_cast<std::size_t>(index)].connected)) {
        return PAD_ERROR_RESOURCE_ALLOCATION_FAILED;
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
    // A closed handle must not leave the host pad rumbling or showing a stale
    // light bar / trigger effect: drop back to defaults and let the poller see it.
    ResetOutput(slot);
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

    // Merge sources into the guest-visible sample. Slot 0 = keyboard/mouse
    // virtual pad + controller; slots 1..3 = controller only.
    // Merge rules (docs/spec/input.md §Failure modes): buttons OR, sticks take
    // the value furthest from centre, triggers take the larger value.
    PadInputState merged;
    if (handle == 1) merged = hostInputState;
    if (slot.controllerPresent) {
        const PadInputState& c = slot.controllerInput;
        merged.buttons |= c.buttons;
        for (std::size_t i = 0; i < merged.sticks.size(); ++i) {
            const int dm = std::abs(static_cast<int>(merged.sticks[i]) - 128);
            const int dc = std::abs(static_cast<int>(c.sticks[i]) - 128);
            if (dc > dm) merged.sticks[i] = c.sticks[i];
        }
        merged.triggers = c.triggers;
        merged.analogTriggers = true;
    }
    slot.lastData.buttons = merged.buttons;
    slot.lastData.left_stick_x = merged.sticks[0];
    slot.lastData.left_stick_y = merged.sticks[1];
    slot.lastData.right_stick_x = merged.sticks[2];
    slot.lastData.right_stick_y = merged.sticks[3];
    // Keyboard has no analog triggers: synthesise 0/255 from the digital bits.
    // A controller's analog value wins, but a keyboard R2 still forces 255.
    const auto kbTrigger = [&](std::uint32_t bit) { return (merged.buttons & bit) != 0 ? 255 : 0; };
    slot.lastData.analog_buttons_l2 = static_cast<std::uint8_t>(
        merged.analogTriggers ? std::max<int>(merged.triggers[0], handle == 1 && (hostInputState.buttons & 0x100) ? 255 : 0)
                              : kbTrigger(0x100));
    slot.lastData.analog_buttons_r2 = static_cast<std::uint8_t>(
        merged.analogTriggers ? std::max<int>(merged.triggers[1], handle == 1 && (hostInputState.buttons & 0x200) ? 255 : 0)
                              : kbTrigger(0x200));
    FillMotionAndTouch(slot, handle == 1 ? &hostInputState : nullptr, currentProcessTime);

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
    if (slot.output.motionEnabled != enable) {
        slot.output.motionEnabled = enable;
        ++slot.output.sequence;
    }
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
    if (slot.output.vibrationLarge != param->large_motor || slot.output.vibrationSmall != param->small_motor) {
        slot.output.vibrationLarge = param->large_motor;
        slot.output.vibrationSmall = param->small_motor;
        ++slot.output.sequence;
    }
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
    slot.output.lightBarValid = true;
    slot.output.lightBar = {param->r, param->g, param->b};
    ++slot.output.sequence;
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
    slot.output.lightBarValid = false;
    slot.output.lightBar = {};
    ++slot.output.sequence;
    return PAD_OK;
}

int PadManager::CheckOpenHandle(int handle) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    return slots[static_cast<std::size_t>(handle - 1)].opened ? PAD_OK : PAD_ERROR_INVALID_HANDLE;
}

int PadManager::ResetOrientation(int handle) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    Pad::ResetFusion(slot.fusion);
    return PAD_OK;
}

int PadManager::SetVibrationMode(int handle, int mode) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    // 0 = desktop (USB) layout, 1 = embedded controller layout; both drive the
    // same host rumble motors, so the value is validated and otherwise ignored.
    if (mode != 0 && mode != 1) {
        return PAD_ERROR_INVALID_ARG;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    if (!slots[static_cast<std::size_t>(handle - 1)].opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    return PAD_OK;
}

int PadManager::SetTriggerEffect(int handle, const Pad::TriggerEffectUpdate& update) {
    if (handle < 1 || handle > PAD_MAX_SLOTS) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& slot = slots[static_cast<std::size_t>(handle - 1)];
    if (!slot.opened) {
        return PAD_ERROR_INVALID_HANDLE;
    }
    for (std::size_t trigger = 0; trigger < 2; ++trigger) {
        if ((update.mask & (1u << trigger)) != 0) slot.output.trigger[trigger] = update.request[trigger];
    }
    if (update.mask != 0) {
        slot.output.triggerTouched = true;
        ++slot.output.sequence;
    }
    return PAD_OK;
}

bool PadManager::FetchOutput(int slot, std::uint32_t* seenSequence, PadOutputState* out) {
    if (slot < 0 || slot >= PAD_MAX_SLOTS || seenSequence == nullptr || out == nullptr) return false;
    std::lock_guard lock(mutex);
    const auto& s = slots[static_cast<std::size_t>(slot)];
    if (*seenSequence == s.output.sequence) return false;
    *seenSequence = s.output.sequence;
    *out = s.output;
    return true;
}

/**
 * Returns a slot's output request to its defaults (no rumble, default light
 * bar, no trigger effect, motion on) and bumps the sequence so the window thread
 * pushes the neutral state to the host pad. Caller holds `mutex`.
 */
void PadManager::ResetOutput(PadSlotState& slot) {
    const std::uint32_t next = slot.output.sequence + 1;
    slot.output = PadOutputState{};
    slot.output.sequence = next;
    slot.vibration = {};
    slot.lightBar = {};
}

/**
 * Fills the motion, orientation and touch fields of `slot.lastData`.
 *
 * Motion is live only when the controller delivered sensor data and the guest
 * has the sensors enabled; otherwise the pad reports the rest pose and the
 * fusion estimate is reset so a later enable does not start from stale state.
 * Touch fingers come from the controller's touchpad; when none is down, slot 0
 * falls back to the keyboard TouchLeft/TouchRight emulation (`host`). Caller
 * holds `mutex`; `now` is the process time in microseconds.
 */
void PadManager::FillMotionAndTouch(PadSlotState& slot, const PadInputState* host, std::uint64_t now) {
    PadData& d = slot.lastData;
    const bool live = slot.controllerPresent && slot.controllerInput.hasMotion && slot.output.motionEnabled;
    std::array<float, 3> accel = Pad::RestAcceleration();
    std::array<float, 3> gyro{0.0f, 0.0f, 0.0f};
    if (live) {
        accel = slot.controllerInput.accel;
        gyro = slot.controllerInput.gyro;
        const float dt = slot.lastFuseTime != 0 && now > slot.lastFuseTime ? static_cast<float>(now - slot.lastFuseTime) * 1e-6f : 0.0f;
        Pad::FuseMotion(slot.fusion, dt, accel, gyro);
    } else {
        Pad::ResetFusion(slot.fusion);
    }
    slot.lastFuseTime = now;
    d.orientation_x = slot.fusion.q.x;
    d.orientation_y = slot.fusion.q.y;
    d.orientation_z = slot.fusion.q.z;
    d.orientation_w = slot.fusion.q.w;
    const auto g = Pad::ToGravityUnits(accel);
    d.acceleration_x = g[0];
    d.acceleration_y = g[1];
    d.acceleration_z = g[2];
    d.angular_velocity_x = gyro[0];
    d.angular_velocity_y = gyro[1];
    d.angular_velocity_z = gyro[2];

    d.touch_data_touch0_x = d.touch_data_touch0_y = d.touch_data_touch1_x = d.touch_data_touch1_y = 0;
    d.touch_data_touch0_id = d.touch_data_touch1_id = 0;
    std::uint8_t touchNum = 0;
    if (slot.controllerPresent) {
        slot.touchIds.Update({slot.controllerInput.touch[0].active, slot.controllerInput.touch[1].active});
        for (std::size_t i = 0; i < 2; ++i) {
            const Pad::PadTouchPoint& tp = slot.controllerInput.touch[i];
            if (!tp.active) continue;
            if (touchNum == 0) {
                d.touch_data_touch0_x = tp.x;
                d.touch_data_touch0_y = tp.y;
                d.touch_data_touch0_id = slot.touchIds.ids[i];
            } else {
                d.touch_data_touch1_x = tp.x;
                d.touch_data_touch1_y = tp.y;
                d.touch_data_touch1_id = slot.touchIds.ids[i];
            }
            ++touchNum;
        }
    } else {
        slot.touchIds.Update({false, false});
    }
    if (touchNum == 0 && host != nullptr && (host->touchLeft || host->touchRight)) {
        d.buttons |= 0x100000;
        touchNum = 1;
        d.touch_data_touch0_x = host->touchRight ? 1440 : 480;
        d.touch_data_touch0_y = 471;
    }
    d.touch_data_touch_num = touchNum;
}

void PadManager::PublishInput(const PadInputState& input) {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    hostInputState = input;
}

void PadManager::PublishControllerInput(int slot, const PadInputState& input) {
    if (slot < 0 || slot >= PAD_MAX_SLOTS) return;
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& s = slots[static_cast<std::size_t>(slot)];
    // First sample from a controller establishes the full connected state even
    // if SetControllerConnected was never called, and counts as one (re)connect.
    if (!s.controllerPresent) {
        ++s.connectedCount;
    }
    s.controllerPresent = true;
    s.connected = true;
    s.controllerInput = input;
}

void PadManager::SetControllerConnected(int slot, bool connected) {
    if (slot < 0 || slot >= PAD_MAX_SLOTS) return;
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
    auto& s = slots[static_cast<std::size_t>(slot)];
    if (connected == s.controllerPresent) return;
    s.controllerPresent = connected;
    s.controllerInput = {};
    if (connected) {
        // connectedCount increments on every (re)connect (spec §State model).
        ++s.connectedCount;
        s.connected = true;
    } else if (slot != 0) {
        // Slot 0 keeps reporting the keyboard/mouse virtual pad.
        s.connected = false;
    }
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

/**
 * Restores `slot` to its constructed state (closed, no controller, initial
 * connectedCount, zeroed timestamp baseline). Test-only: production code never
 * resets a slot. Thread-safe (takes the manager mutex). Out-of-range slots are
 * ignored. The timestamp is zeroed because ReadState advances it monotonically
 * from max(process time, lastTimestamp), so a stale value would leak one test's
 * timestamp sequence into the next.
 */
void PadManager::TestResetSlot(int slot) {
    if (slot < 0 || slot >= PAD_MAX_SLOTS) return;
    std::lock_guard lock(mutex);
    auto& s = slots[static_cast<std::size_t>(slot)];
    s.opened = false;
    s.controllerPresent = false;
    s.controllerInput = {};
    s.lastTimestamp = 0;
    s.output = PadOutputState{};
    s.vibration = {};
    s.lightBar = {};
    s.fusion = {};
    s.touchIds = {};
    s.lastFuseTime = 0;
    // Slot 0 is the always-present keyboard/mouse pad (count 1); others start at 0.
    s.connected = (slot == 0);
    s.connectedCount = static_cast<std::uint8_t>(slot == 0 ? 1 : 0);
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

extern "C" void PadPublishControllerInput_nid_postfix(int slot, const PadInputState& input) {
    Pad::PadManager::Get().PublishControllerInput(slot, input);
}

extern "C" bool PadFetchOutput_nid_postfix(int slot, std::uint32_t* seenSequence, PadOutputState* out) {
    return Pad::PadManager::Get().FetchOutput(slot, seenSequence, out);
}

extern "C" void PadSetControllerConnected_nid_postfix(int slot, bool connected) {
    Pad::PadManager::Get().SetControllerConnected(slot, connected);
}
