#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADSTATE_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADSTATE_HPP

#include <array>
#include <cstdint>
#include <exception>
#include "SceTypes.hpp"

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

#endif
