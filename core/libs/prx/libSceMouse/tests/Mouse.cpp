// Unit tests for libSceMouse — input subsystem scope (M2-gated).
//
// Covers the return-code contract for open/read/close, SDL event routing
// (motion, buttons, wheel, focus, close), and 64-entry ring overflow.
//
// DISABLED: PortPS5 M1 keeps every libSceMouse export as an Unsupported()
// stub (Export.cpp; no gate title imports Mouse at boot, docs/spec/input.md),
// so these cases abort at runtime. They compile in CI to pin the backend API
// and enable with the M2 mouse-export implementation. Ported from AnyPS5
// upstream/main core/libs/prx/libSceMouse/tests/Mouse.cpp (Require/abort +
// manual main() converted to GoogleTest, wired via portps5_add_gtest).
//
// To run locally once exports land:
//   mouse_tests.exe --gtest_also_run_disabled_tests --gtest_filter='Mouse*'
// No GPU and no game data needed.
//
// Ref: docs/spec/input.md

// SDL renames main() to SDL_main() unless told otherwise; GTest owns main here.
#define SDL_MAIN_HANDLED

#include "prx/libSceMouse/include/mouse_structs.h"
#include "prx/libSceMouse/include/MouseState.hpp"
#include "prx/libSceVideoOut/include/MouseInput.hpp"
#include "SDL_events.h"
#include "SDL_mouse.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>

extern "C" {
// Initialises the mouse subsystem. Returns MOUSE_OK on success.
int APS5_VABI sceMouseInit() noexcept;
// Opens a mouse port. Returns MOUSE_HANDLE or a MOUSE_ERROR_* code.
int APS5_VABI sceMouseOpen(int userId, std::int32_t type, std::int32_t index, const void* param) noexcept;
// Closes a mouse handle. Returns MOUSE_OK or MOUSE_ERROR_INVALID_HANDLE.
int APS5_VABI sceMouseClose(std::int32_t handle) noexcept;
// Reads up to num queued frames. Returns the frame count or an error code.
int APS5_VABI sceMouseRead(std::int32_t handle, MouseData* data, std::int32_t num) noexcept;
}

namespace {

// Opening before init is rejected; init is idempotent; bad index,
// unsupported behavior flags and double-open are rejected with INVALID_ARG;
// unknown handles, null storage and over-capacity reads are rejected. A fresh
// open yields one connected frame with no buttons, then an empty queue.
TEST(MouseLifecycle, DISABLED_InitOpenCloseErrors) {
    MouseData data[64]{};
    EXPECT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_ERROR_NOT_INITIALIZED);
    EXPECT_EQ(sceMouseInit(), MOUSE_OK);
    EXPECT_EQ(sceMouseInit(), MOUSE_OK);
    EXPECT_EQ(sceMouseOpen(1, 0, 1, nullptr), MOUSE_ERROR_INVALID_ARG);
    MouseOpenParam unsupported{};
    unsupported.behaviorFlag = 2;
    EXPECT_EQ(sceMouseOpen(1, 0, 0, &unsupported), MOUSE_ERROR_INVALID_ARG);
    EXPECT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_HANDLE);
    EXPECT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_ERROR_ALREADY_OPENED);
    EXPECT_EQ(sceMouseRead(42, data, 1), MOUSE_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, nullptr, 1), MOUSE_ERROR_INVALID_ARG);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 65), MOUSE_ERROR_INVALID_ARG);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 1);
    EXPECT_TRUE(data[0].connected);
    EXPECT_EQ(data[0].buttons, 0);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_OK);
}

// SDL motion routed to another window is ignored; motion, right-button press
// and flipped wheel on the bound window queue three frames with translated
// axes, buttons and wheel/tilt; timestamps are non-decreasing; extended
// buttons (X1) that have no guest mapping are dropped.
TEST(MouseInput, DISABLED_MotionButtonWheelRouting) {
    MouseData data[64]{};
    ASSERT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_HANDLE);
    MouseInput input;
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = 7;
    event.motion.xrel = 9;
    event.motion.yrel = -4;
    input.HandleEvent(event, 8);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    input.HandleEvent(event, 7);

    event = {};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = 7;
    event.button.button = SDL_BUTTON_RIGHT;
    input.HandleEvent(event, 7);
    event = {};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.windowID = 7;
    event.wheel.x = 2;
    event.wheel.y = -3;
    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 3), 3);
    EXPECT_EQ(data[0].x_axis, 9);
    EXPECT_EQ(data[0].y_axis, -4);
    EXPECT_EQ(data[0].buttons, 0);
    EXPECT_EQ(data[1].buttons, 2);
    EXPECT_EQ(data[2].buttons, 2);
    EXPECT_EQ(data[2].wheel, 3);
    EXPECT_EQ(data[2].tilt, -2);
    EXPECT_LE(data[0].timestamp, data[2].timestamp);
    event = {};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.windowID = 7;
    event.button.button = SDL_BUTTON_X1;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_OK);
}

// Focus loss queues a neutral connected frame and suppresses motion;
// window close queues a disconnected frame; refocus reconnects.
TEST(MouseInput, DISABLED_FocusAndCloseSemantics) {
    MouseData data[64]{};
    ASSERT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_HANDLE);
    MouseInput input;
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.windowID = 7;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 1);
    EXPECT_TRUE(data[0].connected);
    EXPECT_EQ(data[0].buttons, 0);
    event.type = SDL_MOUSEMOTION;
    event.motion.xrel = 5;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    event.window.event = SDL_WINDOWEVENT_CLOSE;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 1);
    EXPECT_FALSE(data[0].connected);
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    input.HandleEvent(event, 7);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 1);
    EXPECT_TRUE(data[0].connected);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_OK);
}

// Publishing 70 frames into the 64-entry ring keeps the newest 64 in order
// (first visible x_axis 6 through 69); closing invalidates the handle, and a
// merged-mode open yields a neutral frame.
TEST(MouseLifecycle, DISABLED_RingOverflowAndMergedOpen) {
    MouseData data[64]{};
    ASSERT_EQ(sceMouseOpen(1, 0, 0, nullptr), MOUSE_HANDLE);
    for (int i = 0; i < 70; ++i) {
        MouseInputEvent motion{};
        motion.x = i;
        MousePublishInput_nid_postfix(motion);
    }
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 64), 64);
    EXPECT_EQ(data[0].x_axis, 6);
    EXPECT_EQ(data[63].x_axis, 69);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 0);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_OK);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), MOUSE_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_ERROR_INVALID_HANDLE);
    MouseOpenParam merged{};
    merged.behaviorFlag = MOUSE_OPEN_PARAM_MERGED;
    EXPECT_EQ(sceMouseOpen(1, 0, 0, &merged), MOUSE_HANDLE);
    EXPECT_EQ(sceMouseRead(MOUSE_HANDLE, data, 1), 1);
    EXPECT_EQ(data[0].buttons, 0);
    EXPECT_EQ(sceMouseClose(MOUSE_HANDLE), MOUSE_OK);
}

}  // namespace
