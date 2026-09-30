// Unit tests: SDL game controller -> scePadRead translation (docs/spec/input.md).
//
// Drives Pad::BuildControllerState with synthetic ControllerSample values and
// publishes the result through PadPublishControllerInput_nid_postfix, then reads
// it back through the guest-facing scePadRead export. No SDL device is opened
// and no game data is used.
#define SDL_MAIN_HANDLED

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libScePad/include/ControllerMapping.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadInputTypes.hpp"
#include "prx/libScePad/include/PadState.hpp"

#include <gtest/gtest.h>

extern "C" {
int APS5_VABI scePadInit_nid_postfix(void) noexcept;
int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept;
int APS5_VABI scePadClose_nid_postfix(int handle) noexcept;
int APS5_VABI scePadRead_nid_postfix(int handle, PadData* data, int num) noexcept;
}

namespace {

constexpr std::uint32_t Bit(Pad::PadButton b) { return static_cast<std::uint32_t>(b); }

// PadManager is a process singleton: reset keyboard and every controller slot
// to neutral/disconnected around each test so cases stay independent.
class ControllerInputTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(scePadInit_nid_postfix(), PAD_OK);
        ResetAll();
        ASSERT_EQ(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 0, nullptr), 1);
    }
    void TearDown() override { ResetAll(); }

    static void ResetAll() {
        PadPublishInput_nid_postfix(PadInputState{});
        for (int s = 0; s < PAD_MAX_SLOTS; ++s) {
            PadSetControllerConnected_nid_postfix(s, false);
            scePadClose_nid_postfix(s + 1);
        }
    }
};

} // namespace

// Invariant: every face/shoulder/dpad/stick-click/start SDL button lands on the
// documented PS5 bit, and unmapped buttons contribute nothing.
TEST(ControllerMapping, ButtonTable) {
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_A), Bit(Pad::PadButton::Cross));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_B), Bit(Pad::PadButton::Circle));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_X), Bit(Pad::PadButton::Square));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_Y), Bit(Pad::PadButton::Triangle));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_START), Bit(Pad::PadButton::Options));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), Bit(Pad::PadButton::L1));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), Bit(Pad::PadButton::R1));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_LEFTSTICK), Bit(Pad::PadButton::L3));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_RIGHTSTICK), Bit(Pad::PadButton::R3));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_DPAD_UP), Bit(Pad::PadButton::Up));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_DPAD_DOWN), Bit(Pad::PadButton::Down));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_DPAD_LEFT), Bit(Pad::PadButton::Left));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), Bit(Pad::PadButton::Right));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_TOUCHPAD), Bit(Pad::PadButton::TouchPad));
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_GUIDE), 0u);
    EXPECT_EQ(Pad::ControllerButtonBit(SDL_CONTROLLER_BUTTON_BACK), 0u);
}

// Invariant: triggers scale 0..32767 -> 0..255 exactly at the endpoints, are
// monotonic, and never underflow on negative driver noise.
TEST(ControllerMapping, TriggerScaling) {
    EXPECT_EQ(Pad::NormalizeTrigger(0), 0);
    EXPECT_EQ(Pad::NormalizeTrigger(32767), 255);
    EXPECT_EQ(Pad::NormalizeTrigger(-500), 0);
    EXPECT_NEAR(Pad::NormalizeTrigger(16384), 128, 1);
    int prev = 0;
    for (int v = 0; v <= 32767; v += 97) {
        const int n = Pad::NormalizeTrigger(static_cast<std::int16_t>(v));
        EXPECT_GE(n, prev);
        prev = n;
    }
}

// Invariant: sticks map -32768..32767 to 0..255 with rest at 128; the radial
// dead zone swallows small deflection and rescales so there is no output jump.
TEST(ControllerMapping, StickScalingAndDeadzone) {
    auto centre = Pad::NormalizeStick(0, 0, 0.08);
    EXPECT_EQ(centre[0], 128);
    EXPECT_EQ(centre[1], 128);

    auto full = Pad::NormalizeStick(32767, -32768, 0.08);
    EXPECT_GE(full[0], 254);
    EXPECT_LE(full[1], 1);

    // 5% deflection is inside an 8% dead zone on both axes.
    auto inside = Pad::NormalizeStick(1600, -1600, 0.08);
    EXPECT_EQ(inside[0], 128);
    EXPECT_EQ(inside[1], 128);

    // Just outside the zone the output is still close to centre (rescaled).
    auto edge = Pad::NormalizeStick(static_cast<std::int16_t>(0.09 * 32768), 0, 0.08);
    EXPECT_GT(edge[0], 128);
    EXPECT_LT(edge[0], 140);

    // Zero dead zone is raw linear scaling.
    EXPECT_EQ(Pad::NormalizeStick(-32768, 0, 0.0)[0], 0);

    // A full diagonal stays within 0..255 (magnitude is clamped to 1).
    auto diag = Pad::NormalizeStick(32767, 32767, 0.08);
    EXPECT_GT(diag[0], 128);
}

// Invariant: a controller press published to slot 0 reaches the guest through
// scePadRead with the right digital bits, sticks and analog triggers.
TEST_F(ControllerInputTest, ScePadReadSeesControllerState) {
    Pad::ControllerSample s;
    s.buttons[SDL_CONTROLLER_BUTTON_A] = true;
    s.buttons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = true;
    s.axes[SDL_CONTROLLER_AXIS_LEFTX] = 32767;
    s.axes[SDL_CONTROLLER_AXIS_RIGHTY] = -32768;
    s.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 32767;
    s.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 16384;
    PadSetControllerConnected_nid_postfix(0, true);
    PadPublishControllerInput_nid_postfix(0, Pad::BuildControllerState(s, 0.08));

    PadData d{};
    ASSERT_EQ(scePadRead_nid_postfix(1, &d, 1), 1);
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::Cross), 0u);
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::Left), 0u);
    EXPECT_EQ(d.buttons & Bit(Pad::PadButton::Circle), 0u);
    EXPECT_GE(d.left_stick_x, 254);
    EXPECT_EQ(d.left_stick_y, 128);
    EXPECT_LE(d.right_stick_y, 1);
    EXPECT_EQ(d.analog_buttons_l2, 255);
    EXPECT_NEAR(d.analog_buttons_r2, 128, 1);
    // Analog triggers past the threshold also raise the digital L2/R2 bits.
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::L2), 0u);
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::R2), 0u);
}

// Invariant: detaching a controller returns slot 0 to the (neutral) keyboard
// pad, which stays connected, and connectedCount increments on reconnect.
TEST_F(ControllerInputTest, DisconnectClearsControllerState) {
    Pad::ControllerSample s;
    s.buttons[SDL_CONTROLLER_BUTTON_B] = true;
    PadSetControllerConnected_nid_postfix(0, true);
    PadPublishControllerInput_nid_postfix(0, Pad::BuildControllerState(s, 0.08));
    PadData d{};
    ASSERT_EQ(scePadRead_nid_postfix(1, &d, 1), 1);
    ASSERT_NE(d.buttons & Bit(Pad::PadButton::Circle), 0u);
    const auto countBefore = d.connected_count;

    PadSetControllerConnected_nid_postfix(0, false);
    ASSERT_EQ(scePadRead_nid_postfix(1, &d, 1), 1);
    EXPECT_EQ(d.buttons & Bit(Pad::PadButton::Circle), 0u);
    EXPECT_TRUE(d.connected);
    EXPECT_EQ(d.left_stick_x, 128);

    PadSetControllerConnected_nid_postfix(0, true);
    ASSERT_EQ(scePadRead_nid_postfix(1, &d, 1), 1);
    EXPECT_GT(d.connected_count, countBefore);
}

// Invariant (spec Failure modes): keyboard and controller merge on slot 0 -
// buttons OR, sticks furthest from centre win, keyboard R2 still forces 255.
TEST_F(ControllerInputTest, KeyboardAndControllerMerge) {
    PadInputState kb;
    kb.buttons = Bit(Pad::PadButton::Square) | Bit(Pad::PadButton::R2);
    kb.sticks = {0, 128, 128, 128}; // keyboard: left stick fully left
    PadPublishInput_nid_postfix(kb);

    Pad::ControllerSample s;
    s.buttons[SDL_CONTROLLER_BUTTON_A] = true;
    s.axes[SDL_CONTROLLER_AXIS_LEFTX] = 8000;  // controller: small right push
    s.axes[SDL_CONTROLLER_AXIS_LEFTY] = 32767; // controller: full down
    PadSetControllerConnected_nid_postfix(0, true);
    PadPublishControllerInput_nid_postfix(0, Pad::BuildControllerState(s, 0.08));

    PadData d{};
    ASSERT_EQ(scePadRead_nid_postfix(1, &d, 1), 1);
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::Square), 0u);
    EXPECT_NE(d.buttons & Bit(Pad::PadButton::Cross), 0u);
    EXPECT_EQ(d.left_stick_x, 0);   // keyboard deflection is larger
    EXPECT_GE(d.left_stick_y, 250); // controller deflection is larger
    EXPECT_EQ(d.analog_buttons_r2, 255);
}

// Invariant: slots 1..3 open only while a controller occupies them, carry their
// own state, and do not leak into slot 0.
TEST_F(ControllerInputTest, SecondControllerUsesSlotOne) {
    EXPECT_EQ(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 1, nullptr),
              PAD_ERROR_RESOURCE_ALLOCATION_FAILED);

    PadSetControllerConnected_nid_postfix(1, true);
    ASSERT_EQ(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 1, nullptr), 2);

    Pad::ControllerSample s;
    s.buttons[SDL_CONTROLLER_BUTTON_Y] = true;
    PadPublishControllerInput_nid_postfix(1, Pad::BuildControllerState(s, 0.08));

    PadData p1{}, p0{};
    ASSERT_EQ(scePadRead_nid_postfix(2, &p1, 1), 1);
    ASSERT_EQ(scePadRead_nid_postfix(1, &p0, 1), 1);
    EXPECT_NE(p1.buttons & Bit(Pad::PadButton::Triangle), 0u);
    EXPECT_EQ(p0.buttons & Bit(Pad::PadButton::Triangle), 0u);
    EXPECT_TRUE(p1.connected);

    PadSetControllerConnected_nid_postfix(1, false);
    ASSERT_EQ(scePadRead_nid_postfix(2, &p1, 1), 1);
    EXPECT_FALSE(p1.connected);
}

// Invariant: out-of-range slot indices are ignored, not UB.
TEST_F(ControllerInputTest, InvalidSlotIgnored) {
    PadPublishControllerInput_nid_postfix(-1, PadInputState{});
    PadPublishControllerInput_nid_postfix(PAD_MAX_SLOTS, PadInputState{});
    PadSetControllerConnected_nid_postfix(99, true);
    SUCCEED();
}

// Invariant (review finding): publishing a controller sample to a slot that was
// never explicitly connected must still connect it and bump connectedCount
// exactly once, without double-counting on later samples or a later
// SetControllerConnected(true).
TEST_F(ControllerInputTest, FirstPublishEstablishesConnection) {
    PadData d{};
    PadPublishControllerInput_nid_postfix(1, PadInputState{});
    ASSERT_EQ(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 1, nullptr), 2);
    ASSERT_EQ(scePadRead_nid_postfix(2, &d, 1), 1);
    EXPECT_TRUE(d.connected);
    EXPECT_EQ(d.connected_count, 1);

    PadPublishControllerInput_nid_postfix(1, PadInputState{});
    PadSetControllerConnected_nid_postfix(1, true);
    ASSERT_EQ(scePadRead_nid_postfix(2, &d, 1), 1);
    EXPECT_EQ(d.connected_count, 1);
}
