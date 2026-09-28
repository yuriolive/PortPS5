// Mouse backend state — input subsystem scope (M2-gated), ported from AnyPS5.
// Ring buffer, open/init state, and the Mouse:: backend API used by
// mouse_impl.cpp. Guest-visible exports stay stubs until M2
// (docs/spec/input.md). Debt: shares the process-global mouseMutex in
// mouse_impl.cpp, which must go with the M2 work.
#ifndef CORE_LIBS_PRX_LIBSCEMOUSE_MOUSESTATE_HPP
#define CORE_LIBS_PRX_LIBSCEMOUSE_MOUSESTATE_HPP

#include "prx/libSceMouse/include/mouse_structs.h"

struct MouseInputEvent {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t wheel = 0;
    std::int32_t tilt = 0;
    std::uint32_t button = 0;
    bool pressed = false;
    bool resetButtons = false;
    bool connectionChange = false;
    bool connected = true;
};

namespace Mouse {
int Initialize();
int Open(int userId, std::int32_t type, std::int32_t index, const MouseOpenParam* param);
int Close(std::int32_t handle);
int Read(std::int32_t handle, MouseData* data, std::int32_t num);
void Publish(const MouseInputEvent& event);
}

extern "C" void MousePublishInput_nid_postfix(const MouseInputEvent& event);

#endif
