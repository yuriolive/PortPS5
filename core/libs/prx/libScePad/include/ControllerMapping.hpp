// Pure SDL GameController -> PS5 pad translation (no device, no global state).
//
// Subsystem: input (docs/spec/input.md §Target design 4-5). Owned by libScePad;
// consumed by libSceVideoOut's window-thread poller and by unit tests, which feed
// synthetic ControllerSample values so no SDL device or game data is needed.
// Thread-safety: all functions are stateless and reentrant.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_CONTROLLERMAPPING_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_CONTROLLERMAPPING_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "SDL_gamecontroller.h"

#include "PadInputTypes.hpp"
#include "PadState.hpp"

namespace Pad {

// Raw SDL readings for one controller, index = SDL enum value.
struct ControllerSample {
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> buttons{};
    std::array<std::int16_t, SDL_CONTROLLER_AXIS_MAX> axes{};
    // Optional sensors and touchpad, filled only when the controller has them.
    bool hasMotion = false;
    std::array<float, 3> accel{0.0f, kStandardGravity, 0.0f}; // m/s^2, SDL sensor units
    std::array<float, 3> gyro{0.0f, 0.0f, 0.0f};              // rad/s
    std::array<PadTouchPoint, 2> touch{};
};

// Returns the PS5 digital bit for an SDL button, or 0 when it has no digital
// counterpart. South=Cross, East=Circle, West=Square, North=Triangle (SDL's
// canonical layout). SDL reports L2/R2 as axes, never buttons; they are handled
// by NormalizeTrigger and the digital bit is derived in BuildControllerState.
// The touchpad click maps to the TouchPad bit (no finger tracking, see spec).
constexpr std::uint32_t ControllerButtonBit(int sdlButton) {
    switch (sdlButton) {
        case SDL_CONTROLLER_BUTTON_A: return static_cast<std::uint32_t>(PadButton::Cross);
        case SDL_CONTROLLER_BUTTON_B: return static_cast<std::uint32_t>(PadButton::Circle);
        case SDL_CONTROLLER_BUTTON_X: return static_cast<std::uint32_t>(PadButton::Square);
        case SDL_CONTROLLER_BUTTON_Y: return static_cast<std::uint32_t>(PadButton::Triangle);
        case SDL_CONTROLLER_BUTTON_START: return static_cast<std::uint32_t>(PadButton::Options);
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return static_cast<std::uint32_t>(PadButton::L3);
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return static_cast<std::uint32_t>(PadButton::R3);
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return static_cast<std::uint32_t>(PadButton::L1);
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return static_cast<std::uint32_t>(PadButton::R1);
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return static_cast<std::uint32_t>(PadButton::Up);
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return static_cast<std::uint32_t>(PadButton::Down);
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return static_cast<std::uint32_t>(PadButton::Left);
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return static_cast<std::uint32_t>(PadButton::Right);
        case SDL_CONTROLLER_BUTTON_TOUCHPAD: return static_cast<std::uint32_t>(PadButton::TouchPad);
        default: return 0;
    }
}

// Trigger: SDL 0..32767 -> pad 0..255 (rounded; negative noise clamps to 0).
constexpr std::uint8_t NormalizeTrigger(std::int16_t raw) {
    const int v = raw < 0 ? 0 : raw;
    return static_cast<std::uint8_t>((v * 255 + 16383) / 32767);
}

// Single axis: SDL -32768..32767 -> pad 0..255 with 128 as centre. Linear
// scaling uses 32768 as the divisor so that -32768 -> 0 and 32767 -> 255.
constexpr std::uint8_t NormalizeAxis(std::int16_t raw) {
    const int v = (static_cast<int>(raw) + 32768) / 256; // 0..255, raw 0 -> 128
    return static_cast<std::uint8_t>(v > 255 ? 255 : v);
}

// Stick pair with a radial dead zone. `deadzone` is a fraction of full
// deflection (spec default 0.08, `[input] deadzone`). Inside the zone both
// axes are centred; outside, magnitude is rescaled so output starts at 0 at
// the zone edge (no jump). Result is {x, y} in pad units, 128 = centre.
inline std::array<std::uint8_t, 2> NormalizeStick(std::int16_t rawX, std::int16_t rawY, double deadzone) {
    const double dz = std::clamp(deadzone, 0.0, 0.5);
    const double x = rawX / 32768.0;
    const double y = rawY / 32768.0;
    const double mag = std::hypot(x, y);
    if (mag <= dz || mag == 0.0) {
        return {128, 128};
    }
    // Rescale along the radial direction, but clamp each axis (not the vector
    // magnitude) so square-gated pads and diagonal corners still reach full
    // deflection instead of being shrunk onto the unit circle.
    const double scale = (mag - dz) / (1.0 - dz) / mag;
    const double nx = std::clamp(x * scale, -1.0, 1.0);
    const double ny = std::clamp(y * scale, -1.0, 1.0);
    // Asymmetric so that -1 -> 0, 0 -> 128, +1 -> 255 exactly.
    const auto toByte = [](double n) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(n < 0 ? 128.0 + n * 128.0 : 128.0 + n * 127.0), 0L, 255L));
    };
    return {toByte(nx), toByte(ny)};
}

// Full translation of one controller sample into a PadInputState.
// Digital L2/R2 bits are set once the analog trigger passes ~12% (30/255),
// matching the threshold that keeps resting-trigger noise from registering.
inline PadInputState BuildControllerState(const ControllerSample& sample, double deadzone) {
    PadInputState state;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
        if (sample.buttons[static_cast<std::size_t>(b)]) state.buttons |= ControllerButtonBit(b);
    }
    const auto left = NormalizeStick(sample.axes[SDL_CONTROLLER_AXIS_LEFTX], sample.axes[SDL_CONTROLLER_AXIS_LEFTY], deadzone);
    const auto right = NormalizeStick(sample.axes[SDL_CONTROLLER_AXIS_RIGHTX], sample.axes[SDL_CONTROLLER_AXIS_RIGHTY], deadzone);
    state.sticks = {left[0], left[1], right[0], right[1]};
    state.triggers = {NormalizeTrigger(sample.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]),
                      NormalizeTrigger(sample.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT])};
    state.analogTriggers = true;
    state.hasMotion = sample.hasMotion;
    state.accel = sample.accel;
    state.gyro = sample.gyro;
    state.touch = sample.touch;
    if (state.triggers[0] > 30) state.buttons |= static_cast<std::uint32_t>(PadButton::L2);
    if (state.triggers[1] > 30) state.buttons |= static_cast<std::uint32_t>(PadButton::R2);
    return state;
}

} // namespace Pad

#endif
