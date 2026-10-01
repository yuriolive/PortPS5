// Unit tests: the bundled SDL is built for DualSense input (docs/spec/input.md §Target design 1).
//
// Guards the CMake options in the root CMakeLists.txt: without the HIDAPI
// joystick driver SDL falls back to a generic joystick for a DualSense, so there
// is no light bar, touchpad or trigger-effect output (docs/spec/input.md §Failure
// modes). The check is a link-time reference to the driver object of the static
// SDL library: it only links when SDL_HIDAPI and SDL_JOYSTICK_HIDAPI were really
// built. Reading the SDL_JOYSTICK_HIDAPI macro would not work, because on Windows
// the consumer-visible SDL_config.h is the hand-written SDL_config_windows.h that
// defines it unconditionally. No controller or SDL initialisation is needed.
#define SDL_MAIN_HANDLED

#include "SDL.h"
#include "SDL_hints.h"

#include <gtest/gtest.h>

// Internal driver table entry of the static SDL library (src/joystick/hidapi/
// SDL_hidapijoystick.c). Declared opaquely: only its address is taken.
extern "C" {
extern char SDL_HIDAPI_JoystickDriver;
}

// Invariant: the HIDAPI joystick driver object is part of the linked SDL, so
// USB DualSense output (light bar, rumble, effects) can reach the controller.
// With SDL_HIDAPI OFF this test fails to link, which is the intended signal.
TEST(PadSdlBuild, HidapiJoystickDriverIsLinked) {
    // volatile keeps the compiler from folding the non-null address away, which
    // would also drop the link-time reference this test exists to create.
    const void* volatile driver = &SDL_HIDAPI_JoystickDriver;
    EXPECT_NE(driver, nullptr);
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
