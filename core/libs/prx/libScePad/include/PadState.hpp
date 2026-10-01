// libScePad host-side input state and cross-prx publish entry points.
// Subsystem: input (docs/spec/input.md). PadInputState is the keyboard/mouse or
// controller sample handed to PadManager; the extern "C" functions are the only
// calls other prx (libSceVideoOut) make into libScePad, so they must stay
// extern "C" to survive NID patching. Thread-safety: callable from the window
// thread; PadManager serialises with one short mutex.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADSTATE_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADSTATE_HPP

#include <array>
#include <cstdint>
#include <exception>
#include "SceTypes.hpp"
#include "PadMotion.hpp"
#include "PadOutputMapping.hpp"

struct PadInputState {
    std::uint32_t buttons = 0;
    std::array<std::uint8_t, 4> sticks{128, 128, 128, 128};
    bool touchLeft = false;
    bool touchRight = false;
    // Analog L2/R2 (0..255). Only meaningful when analogTriggers is set by a
    // controller source; keyboard/mouse input leaves it false and the pad
    // layer synthesises 0/255 from the digital L2/R2 bits instead.
    std::array<std::uint8_t, 2> triggers{0, 0};
    bool analogTriggers = false;
    // Controller extras, neutral when the source has none. Motion is in SDL
    // units (accel m/s^2, gyro rad/s); see PadMotion.hpp.
    bool hasMotion = false;
    std::array<float, 3> accel = Pad::RestAcceleration();
    std::array<float, 3> gyro{0.0f, 0.0f, 0.0f};
    std::array<Pad::PadTouchPoint, 2> touch{};
};

// Guest output requests for one pad slot (rumble, light bar, adaptive
// triggers), polled by the window thread and mirrored onto the host controller.
// `sequence` is bumped on every change so the poller only re-sends on change.
struct PadOutputState {
    std::uint32_t sequence = 0;
    std::uint8_t vibrationLarge = 0;
    std::uint8_t vibrationSmall = 0;
    bool lightBarValid = false;  // false: the controller's default colour
    std::array<std::uint8_t, 3> lightBar{};
    std::array<Pad::TriggerRequest, 2> trigger{};  // 0 = L2, 1 = R2
    bool triggerTouched = false; // an effect was requested at least once
    bool motionEnabled = true;   // sensors on unless the guest disables them
};

namespace Pad {
void Initialize();
PadData ReadState();
}

extern "C" void PadPublishInput_nid_postfix(const PadInputState& input);
// Cross-prx entry points (extern "C" so they survive nid patching like the
// calls above). PublishController: one physical controller's state into pad
// slot 0..3, marking it connected. SetControllerConnected: attach/detach; slot 0
// keeps reporting the keyboard/mouse virtual pad after detach.
extern "C" void PadPublishControllerInput_nid_postfix(int slot, const PadInputState& input);
extern "C" void PadSetControllerConnected_nid_postfix(int slot, bool connected);
extern "C" void PadReportInputFailure_nid_postfix(std::exception_ptr error);
// Copies slot `slot`'s output request into *out and stores its sequence in
// *seenSequence when it differs from the value passed in; returns true when a
// copy was made. A null pointer or out-of-range slot returns false.
extern "C" bool PadFetchOutput_nid_postfix(int slot, std::uint32_t* seenSequence, PadOutputState* out);

#endif
