#ifndef CORE_LIBS_PRX_LIBSCEVIDEOOUT_PADINPUT_HPP
#define CORE_LIBS_PRX_LIBSCEVIDEOOUT_PADINPUT_HPP

#include "SDL_events.h"
#include "SDL_gamecontroller.h"
#include "prx/libScePad/include/InputMapping.hpp"
#include <array>
#include <chrono>

class DisplayWindow;

class PadInput {
public:
    void HandleEvent(const SDL_Event& event, DisplayWindow& window);
    void Update();

private:
    // Attach/detach an SDL game controller to the lowest free pad slot (0..3).
    void addController(int deviceIndex);
    void removeController(SDL_JoystickID instanceId);
    // Samples every attached controller and publishes it to its pad slot;
    // publishes neutral state instead when `neutral` (focus lost).
    void pollControllers(bool neutral);
    void publish();
    void setMouseMode(bool enabled);
    std::array<bool, Pad::InputMapping.size()> pressed{};
    std::array<std::chrono::steady_clock::time_point, Pad::InputMapping.size()> wheelReleaseTimes{};
    std::array<std::uint8_t, 2> mouseStick{128, 128};
    std::chrono::steady_clock::time_point nextMousePoll{};
    bool mouseEnabled = false;
    struct ControllerSlot {
        SDL_GameController* controller = nullptr;
        SDL_JoystickID instanceId = -1;
    };
    std::array<ControllerSlot, 4> controllers{};
};

#endif
