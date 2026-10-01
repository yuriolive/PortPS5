// Window-thread pad input: SDL keyboard, mouse and game controller handling.
// Subsystem: video/input. Owned by the VideoOut present loop; not thread-safe,
// all methods run on that single thread and publish to libScePad through the
// extern "C" *_nid_postfix entry points (see libScePad/include/PadState.hpp).
#ifndef CORE_LIBS_PRX_LIBSCEVIDEOOUT_PADINPUT_HPP
#define CORE_LIBS_PRX_LIBSCEVIDEOOUT_PADINPUT_HPP

#include "SDL_events.h"
#include "SDL_gamecontroller.h"
#include "prx/libScePad/include/InputMapping.hpp"
#include "prx/libScePad/include/PadSlotTable.hpp"
#include "prx/libScePad/include/PadState.hpp"
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
    // Mirrors the guest's rumble / light bar / trigger request for `slot` onto
    // the host controller. Runs on the window thread that owns the SDL joystick.
    void applyOutput(std::size_t slot);
    // Turns the controller's accelerometer and gyro on or off to match the
    // guest's scePadSetMotionSensorState request.
    void enableSensors(std::size_t slot);
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
        // Output mirroring: the sequence last applied (0xFFFFFFFF forces the
        // first fetch after attach so a replugged pad gets the current request),
        // whether rumble / a custom LED colour / a trigger effect is currently set on the host pad,
        // and when SDL's timed rumble must be refreshed.
        std::uint32_t outputSequence = 0xFFFFFFFFu;
        PadOutputState output{};
        bool pending = false;
        bool rumbleActive = false;
        bool ledOverridden = false;
        bool triggerEffectActive = false;
        std::chrono::steady_clock::time_point nextRumbleRefresh{};
    };
    Pad::SlotTable slotTable;
    std::array<ControllerSlot, 4> controllers{};
};

#endif
