// libSceVideoOut SDL pad input handling and event translation.
//
// Translates SDL keyboard, mouse, and controller events to guest PadState data.
// Subsystem: video / input. Host-only: no guest-called exports directly in this file.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "SDL.h"
#include "prx/libSceVideoOut/include/PadInput.hpp"
#include "prx/libSceVideoOut/include/DisplayWindow.hpp"
#include "prx/libScePad/include/PadState.hpp"
#include "prx/libScePad/include/PadInputTypes.hpp"
#include "prx/libScePad/include/ControllerMapping.hpp"
#include "prx/libc/include/config/Config.hpp"
#include "prx/libc/include/general/LogMacros.hpp"
#include "prx/libScePad/include/PadOutputMapping.hpp"

namespace {
// Why typed config, not env: APS5_* reads are banned by policy;
// debug.ignore_host_input keeps measurement and recorded-input replay runs
// from reacting to stray keys or mouse buttons that reach the window.
// Why the verbatim wrappers: Loader:: methods hash under nid_patcher
// (libc has no --preserve-exports), so cross-prx callers use the
// _nid_no_patch free functions (Config.hpp) to survive prx load.
bool IgnoreHostInput() {
 if (!PortPS5_Config_Loader_IsInitialized_nid_no_patch()) {
  return false;
 }
 return PortPS5_Config_Loader_Get_nid_no_patch().debug.ignoreHostInput;
}

// Radial dead zone fraction from `[input] deadzone` (spec default 0.08).
double StickDeadzone() {
 if (!PortPS5_Config_Loader_IsInitialized_nid_no_patch()) {
  return 0.08;
 }
 return PortPS5_Config_Loader_Get_nid_no_patch().input.deadzone;
}
}

void PadInput::addController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == nullptr) return;
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    Pad::DeviceGuid guid{};
    const SDL_JoystickGUID sdlGuid = SDL_JoystickGetGUID(joystick);
    std::memcpy(guid.data(), sdlGuid.data, guid.size());
    // SlotTable owns the policy (lowest free slot, same-GUID reclaim, fifth
    // controller refused); duplicates and overflow just release this reference.
    const Pad::AttachResult result = slotTable.Attach(id, guid);
    if (result.status != Pad::AttachStatus::Assigned) {
        SDL_GameControllerClose(controller);
        return;
    }
    ControllerSlot& slot = controllers[result.slot];
    slot = {};
    slot.controller = controller;
    slot.instanceId = id;
    slot.pending = true;
    const char* name = SDL_GameControllerName(controller);
    APS5_LOG_OUT("Pad: slot %zu: %s (type %d, accel=%d gyro=%d, touchpads=%d, led=%d, trigger rumble=%d)",
        result.slot, name != nullptr ? name : "unknown", static_cast<int>(SDL_GameControllerGetType(controller)),
        SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE, SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE,
        SDL_GameControllerGetNumTouchpads(controller), SDL_GameControllerHasLED(controller) == SDL_TRUE,
        SDL_GameControllerHasRumbleTriggers(controller) == SDL_TRUE);
    PadSetControllerConnected_nid_postfix(static_cast<int>(result.slot), true);
    enableSensors(result.slot);
}

void PadInput::removeController(SDL_JoystickID instanceId) {
    const std::size_t i = slotTable.Detach(instanceId);
    if (i == Pad::SlotTable::kNone) return;
    SDL_GameControllerClose(controllers[i].controller);
    controllers[i] = {};
    PadSetControllerConnected_nid_postfix(static_cast<int>(i), false);
}

void PadInput::enableSensors(std::size_t slot) {
    SDL_GameController* c = controllers[slot].controller;
    if (c == nullptr) return;
    // Joystick sensors are independent of the SDL_SENSOR subsystem, so the
    // DualSense IMU works with SDL_SENSOR off; SDL ignores a type the pad lacks.
    const SDL_bool wanted = controllers[slot].output.motionEnabled ? SDL_TRUE : SDL_FALSE;
    if (SDL_GameControllerHasSensor(c, SDL_SENSOR_ACCEL) == SDL_TRUE) SDL_GameControllerSetSensorEnabled(c, SDL_SENSOR_ACCEL, wanted);
    if (SDL_GameControllerHasSensor(c, SDL_SENSOR_GYRO) == SDL_TRUE) SDL_GameControllerSetSensorEnabled(c, SDL_SENSOR_GYRO, wanted);
}

void PadInput::applyOutput(std::size_t index) {
    ControllerSlot& slot = controllers[index];
    SDL_GameController* c = slot.controller;
    if (c == nullptr) return;
    PadOutputState fetched;
    if (PadFetchOutput_nid_postfix(static_cast<int>(index), &slot.outputSequence, &fetched)) {
        const bool motionChanged = fetched.motionEnabled != slot.output.motionEnabled;
        slot.output = fetched;
        slot.pending = true;
        if (motionChanged) enableSensors(index);
    }
    const auto now = std::chrono::steady_clock::now();
    const PadOutputState& out = slot.output;
    const bool rumbling = out.vibrationLarge != 0 || out.vibrationSmall != 0;
    const bool isPs5 = SDL_GameControllerGetType(c) == SDL_CONTROLLER_TYPE_PS5;
    const bool triggerRumble = !isPs5 && (out.trigger[0].fallback != 0 || out.trigger[1].fallback != 0);
    if (!slot.pending) {
        // SDL rumble expires host-side after kRumbleDurationMs; keep it alive while the guest holds it.
        if (!((rumbling || triggerRumble) && now >= slot.nextRumbleRefresh)) return;
    }
    slot.pending = false;
    slot.nextRumbleRefresh = now + std::chrono::milliseconds(Pad::kRumbleRefreshMs);
    if (rumbling || slot.rumbleActive) {
        const Pad::SdlRumble r = Pad::RumbleToSdl(out.vibrationLarge, out.vibrationSmall);
        SDL_GameControllerRumble(c, r.lowFrequency, r.highFrequency, rumbling ? Pad::kRumbleDurationMs : 0);
        slot.rumbleActive = rumbling;
    }
    if (SDL_GameControllerHasLED(c) == SDL_TRUE) {
        if (out.lightBarValid) {
            SDL_GameControllerSetLED(c, out.lightBar[0], out.lightBar[1], out.lightBar[2]);
            slot.ledOverridden = true;
        } else if (slot.ledOverridden) {
            SDL_GameControllerSetLED(c, Pad::kDefaultLightBar[0], Pad::kDefaultLightBar[1], Pad::kDefaultLightBar[2]);
            slot.ledOverridden = false;
        }
    }
    // After the guest closes the pad the request resets to "off"; an effect that
    // was engaged must still be cleared on the host pad, hence triggerEffectActive.
    if (out.triggerTouched || slot.triggerEffectActive) {
        slot.triggerEffectActive = out.triggerTouched;
        if (isPs5) {
            const auto report = Pad::BuildDs5TriggerEffects(out.trigger[0], out.trigger[1]);
            SDL_GameControllerSendEffect(c, report.data(), static_cast<int>(report.size()));
        } else if (SDL_GameControllerHasRumbleTriggers(c) == SDL_TRUE) {
            SDL_GameControllerRumbleTriggers(c, static_cast<Uint16>(out.trigger[0].fallback * 257), static_cast<Uint16>(out.trigger[1].fallback * 257),
                triggerRumble ? Pad::kRumbleDurationMs : 0);
        }
    }
}

void PadInput::pollControllers(bool neutral) {
    const double deadzone = StickDeadzone();
    for (std::size_t i = 0; i < controllers.size(); ++i) {
        SDL_GameController* c = controllers[i].controller;
        if (c == nullptr) continue;
        Pad::ControllerSample sample;
        if (!neutral) {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                sample.buttons[static_cast<std::size_t>(b)] = SDL_GameControllerGetButton(c, static_cast<SDL_GameControllerButton>(b)) != 0;
            }
            for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
                sample.axes[static_cast<std::size_t>(a)] = SDL_GameControllerGetAxis(c, static_cast<SDL_GameControllerAxis>(a));
            }
            if (SDL_GameControllerIsSensorEnabled(c, SDL_SENSOR_ACCEL) == SDL_TRUE && SDL_GameControllerIsSensorEnabled(c, SDL_SENSOR_GYRO) == SDL_TRUE) {
                std::array<float, 3> accel{};
                std::array<float, 3> gyro{};
                if (SDL_GameControllerGetSensorData(c, SDL_SENSOR_ACCEL, accel.data(), 3) == 0 &&
                    SDL_GameControllerGetSensorData(c, SDL_SENSOR_GYRO, gyro.data(), 3) == 0) {
                    sample.hasMotion = true;
                    sample.accel = accel;
                    sample.gyro = gyro;
                }
            }
            if (SDL_GameControllerGetNumTouchpads(c) > 0) {
                for (int finger = 0; finger < 2; ++finger) {
                    Uint8 down = 0;
                    float x = 0.0f;
                    float y = 0.0f;
                    float pressure = 0.0f;
                    if (SDL_GameControllerGetTouchpadFinger(c, 0, finger, &down, &x, &y, &pressure) != 0 || down == 0) continue;
                    sample.touch[static_cast<std::size_t>(finger)] = Pad::ScaleTouchFinger(x, y);
                }
            }
        }
        PadPublishControllerInput_nid_postfix(static_cast<int>(i), Pad::BuildControllerState(sample, deadzone));
        applyOutput(i);
    }
}

void PadInput::setMouseMode(bool enabled) {
    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) != 0) throw std::runtime_error(std::string("Pad: relative mouse mode failed: ") + SDL_GetError());
    int deltaX = 0;
    int deltaY = 0;
    SDL_GetRelativeMouseState(&deltaX, &deltaY);
    mouseEnabled = enabled;
    mouseStick = {128, 128};
    nextMousePoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(Pad::MousePollIntervalMs);
}

void PadInput::HandleEvent(const SDL_Event& event, DisplayWindow& window) {
    if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_CLOSE)) {
        pressed.fill(false);
        wheelReleaseTimes.fill({});
        if (mouseEnabled) setMouseMode(false);
        pollControllers(true);
        publish();
        return;
    }
    // Controllers are host input: with debug.ignore_host_input they are never
    // attached, so recorded-input replays see a deterministic pad.
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (!IgnoreHostInput()) addController(event.cdevice.which);
        return;
    }
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        removeController(event.cdevice.which);
        return;
    }
    if (IgnoreHostInput() && event.type == SDL_MOUSEWHEEL) {
        return;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        int direction = (event.wheel.y > 0) - (event.wheel.y < 0);
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) direction = -direction;
        if (direction == 0) return;
        const auto releaseTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(Pad::WheelPressDurationMs);
        for (std::size_t index = 0; index < Pad::InputMapping.size(); ++index) {
            const auto& binding = Pad::InputMapping[index];
            if (binding.wheelDirection == 0) continue;
            pressed[index] = binding.wheelDirection == direction;
            wheelReleaseTimes[index] = pressed[index] ? releaseTime : std::chrono::steady_clock::time_point{};
        }
        publish();
        return;
    }
    const bool keyboard = event.type == SDL_KEYDOWN || event.type == SDL_KEYUP;
    const bool mouse = event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP;
    if (!keyboard && !mouse) return;
    if (IgnoreHostInput()) {
        return;
    }
    if (keyboard && event.key.repeat != 0) return;
    const bool down = event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN;
    for (std::size_t index = 0; index < Pad::InputMapping.size(); ++index) {
        const auto& binding = Pad::InputMapping[index];
        const bool keyMatches = binding.key != SDL_SCANCODE_UNKNOWN && binding.key == event.key.keysym.scancode;
        const bool mouseMatches = binding.mouseButton != Pad::MouseButton::None && binding.mouseButton == static_cast<Pad::MouseButton>(event.button.button);
        const bool matches = keyboard ? keyMatches : mouseMatches;

        if (!matches) continue;
        if (binding.control == Pad::InputControl::ToggleFullscreen) {
            if (keyboard && down && !pressed[index] && window.Handle() != nullptr && event.key.windowID == SDL_GetWindowID(window.Handle())) window.ToggleFullscreen();
        }
        if (binding.control == Pad::InputControl::ToggleMouse && down && !pressed[index]) setMouseMode(!mouseEnabled);
        pressed[index] = down;
    }
    publish();
}

void PadInput::Update() {
    if (IgnoreHostInput()) {
        return;
    }
    pollControllers(SDL_GetKeyboardFocus() == nullptr);
    const auto now = std::chrono::steady_clock::now();
    bool released = false;
    for (std::size_t index = 0; index < Pad::InputMapping.size(); ++index) {
        if (Pad::InputMapping[index].wheelDirection == 0 || !pressed[index] || now < wheelReleaseTimes[index]) continue;
        pressed[index] = false;
        wheelReleaseTimes[index] = {};
        released = true;
    }
    if (released) publish();
    if (!mouseEnabled) return;
    if (SDL_GetKeyboardFocus() == nullptr) {
        pressed.fill(false);
        wheelReleaseTimes.fill({});
        setMouseMode(false);
        publish();
        return;
    }
    if (now < nextMousePoll) return;
    nextMousePoll = now + std::chrono::milliseconds(Pad::MousePollIntervalMs);
    int deltaX = 0;
    int deltaY = 0;
    SDL_GetRelativeMouseState(&deltaX, &deltaY);
    mouseStick = {128, 128};
    if (deltaX != 0 || deltaY != 0) {
        const double distance = std::hypot(deltaX, deltaY);
        const double scale = std::clamp(distance * Pad::MouseSensitivity + 16.0, 64.0, 128.0) / distance;
        const auto mapAxis = [scale](int delta) { return static_cast<std::uint8_t>(std::clamp(128L + std::lround(delta * scale), 0L, 255L)); };
        mouseStick = {mapAxis(deltaX), mapAxis(deltaY)};
    }
    publish();
}

void PadInput::publish() {
    PadInputState state;
    std::array<bool, 4> negative{};
    std::array<bool, 4> positive{};
    for (std::size_t index = 0; index < Pad::InputMapping.size(); ++index) {
        if (!pressed[index]) continue;
        const auto& binding = Pad::InputMapping[index];
        switch (binding.control) {
            case Pad::InputControl::Button: state.buttons |= static_cast<std::uint32_t>(binding.button); break;
            case Pad::InputControl::LeftStickLeft: negative[0] = true; break;
            case Pad::InputControl::LeftStickRight: positive[0] = true; break;
            case Pad::InputControl::LeftStickUp: negative[1] = true; break;
            case Pad::InputControl::LeftStickDown: positive[1] = true; break;
            case Pad::InputControl::RightStickLeft: negative[2] = true; break;
            case Pad::InputControl::RightStickRight: positive[2] = true; break;
            case Pad::InputControl::RightStickUp: negative[3] = true; break;
            case Pad::InputControl::RightStickDown: positive[3] = true; break;
            case Pad::InputControl::TouchLeft: state.touchLeft = true; break;
            case Pad::InputControl::TouchRight: state.touchRight = true; break;
            case Pad::InputControl::ToggleMouse: break;
            case Pad::InputControl::ToggleFullscreen: break;
        }
    }
    for (std::size_t axis = 0; axis < state.sticks.size(); ++axis) {
        state.sticks[axis] = negative[axis] == positive[axis] ? 128 : negative[axis] ? 0 : 255;
    }
    if (mouseEnabled) {
        state.sticks[2] = mouseStick[0];
        state.sticks[3] = mouseStick[1];
    }
    PadPublishInput_nid_postfix(state);
}
