// Unit tests: the bundled SDL is built for DualSense input (docs/spec/input.md §Target design 1).
//
// Guards the CMake options in the root CMakeLists.txt: without the HIDAPI
// joystick driver SDL falls back to a generic joystick for a DualSense, so there
// is no light bar, touchpad or trigger-effect output (docs/spec/input.md §Failure
// modes). The checks read SDL's generated configuration macros, so they need no
// controller and no SDL initialisation.
#define SDL_MAIN_HANDLED

#include "SDL.h"
#include "SDL_config.h"
#include "SDL_hints.h"

#include <gtest/gtest.h>

// Invariant: the SDL joystick subsystem and its HIDAPI driver are compiled in,
// so USB DualSense output (light bar, rumble, effects) reaches the controller.
TEST(PadSdlBuild, HidapiJoystickDriverIsCompiledIn) {
#ifdef SDL_JOYSTICK_DISABLED
    FAIL() << "SDL is built without the joystick subsystem";
#endif
#ifdef SDL_JOYSTICK_HIDAPI
    SUCCEED();
#else
    FAIL() << "SDL is built without the HIDAPI joystick driver: a DualSense gets no light bar, touchpad or trigger effects";
#endif
}

// Invariant: the pinned SDL is new enough to carry the HIDAPI PS5 driver
// (SDL 2.0.14 or later), which is what the pad output path relies on.
TEST(PadSdlBuild, PinnedSdlHasPs5Driver) {
    EXPECT_TRUE(SDL_VERSION_ATLEAST(2, 0, 14));
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5
    SUCCEED();
#else
    FAIL() << "SDL headers lack SDL_HINT_JOYSTICK_HIDAPI_PS5";
#endif
}
