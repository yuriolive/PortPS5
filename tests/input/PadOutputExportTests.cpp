// Unit tests: libScePad output, motion and touch exports (docs/spec/input.md §Output calls).
//
// Drives the guest-facing scePad* exports and the PadManager output queue with
// synthetic controller samples: vibration, light bar, trigger effects, motion
// enable/orientation reset, touchpad fingers, per-slot isolation, handle and
// argument error codes, and the reset-on-close contract. The window thread's
// SDL side is not exercised (it needs a device); PadFetchOutput_nid_postfix is
// the seam it consumes. No game data or recorded input is used.
#define SDL_MAIN_HANDLED

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libScePad/include/ControllerMapping.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/include/PadOutputMapping.hpp"
#include "prx/libScePad/include/PadState.hpp"
#include "prx/libScePad/src/PadInternal.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

extern "C" {
int APS5_VABI scePadInit_nid_postfix(void) noexcept;
int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept;
int APS5_VABI scePadClose_nid_postfix(int handle) noexcept;
int APS5_VABI scePadReadState(int handle, PadData* data) noexcept;
int APS5_VABI scePadSetVibration(int handle, const PadVibrationParam* param) noexcept;
int APS5_VABI scePadSetVibrationMode(int handle, int mode) noexcept;
int APS5_VABI scePadSetLightBar(int handle, const PadLightBarParam* param) noexcept;
int APS5_VABI scePadResetLightBar(int handle) noexcept;
int APS5_VABI scePadSetTriggerEffect(int handle, const void* param) noexcept;
int APS5_VABI scePadSetMotionSensorState(int handle, bool enable) noexcept;
int APS5_VABI scePadResetOrientation(int handle) noexcept;
int APS5_VABI scePadGetTriggerEffectState(int handle, PadTriggerEffectStateInformation* info) noexcept;
int APS5_VABI scePadDeviceClassGetExtendedInformation(int handle, PadDeviceClassExtendedInformation* info) noexcept;
int APS5_VABI scePadDeviceClassParseData(int handle, const PadData* data, PadDeviceClassData* classData) noexcept;
}

namespace {

constexpr int kUser = 0x10000000;

// Slot 0 is always connected through the keyboard virtual pad; reset every slot
// around each test because PadManager is a process singleton.
class PadOutputTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(scePadInit_nid_postfix(), PAD_OK);
        ResetAll();
        ASSERT_EQ(scePadOpen_nid_postfix(kUser, PAD_PORT_TYPE_STANDARD, 0, nullptr), 1);
    }
    void TearDown() override { ResetAll(); }

    static void ResetAll() {
        PadPublishInput_nid_postfix(PadInputState{});
        for (int s = 0; s < PAD_MAX_SLOTS; ++s) {
            PadSetControllerConnected_nid_postfix(s, false);
            scePadClose_nid_postfix(s + 1);
            Pad::PadManager::Get().TestResetSlot(s);
        }
    }

    // Fetches slot `slot`'s output from a never-seen sequence so the first call always copies.
    static PadOutputState Fetch(int slot) {
        std::uint32_t seen = 0xFFFFFFFFu;
        PadOutputState out;
        EXPECT_TRUE(PadFetchOutput_nid_postfix(slot, &seen, &out));
        return out;
    }

    // Builds a guest ScePadTriggerEffectParam: mask, then per-trigger {mode, payload...}.
    static std::array<std::uint8_t, Pad::kTriggerEffectParamSize> Param(std::uint8_t mask, std::uint32_t modeL2, std::uint32_t modeR2,
                                                                       std::initializer_list<std::uint8_t> payload) {
        std::array<std::uint8_t, Pad::kTriggerEffectParamSize> p{};
        p[0] = mask;
        const std::uint32_t modes[2] = {modeL2, modeR2};
        for (std::size_t t = 0; t < 2; ++t) {
            std::memcpy(p.data() + 8 + t * Pad::kTriggerCommandSize, &modes[t], 4);
            std::size_t at = 8 + t * Pad::kTriggerCommandSize + Pad::kTriggerCommandDataOffset;
            for (const std::uint8_t b : payload) p[at++] = b;
        }
        return p;
    }
};

} // namespace

// Invariant: scePadSetVibration records the motor amplitudes for the slot's
// output queue, bumps the sequence only on a real change, and rejects bad
// handles and null parameters with the SCE error codes.
TEST_F(PadOutputTest, VibrationReachesOutputQueueOnChangeOnly) {
    const PadVibrationParam v{200, 50};
    ASSERT_EQ(scePadSetVibration(1, &v), PAD_OK);
    std::uint32_t seen = 0xFFFFFFFFu;
    PadOutputState out;
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out));
    EXPECT_EQ(out.vibrationLarge, 200);
    EXPECT_EQ(out.vibrationSmall, 50);
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "no change since the last fetch";

    ASSERT_EQ(scePadSetVibration(1, &v), PAD_OK);
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "identical request must not bump the sequence";

    const PadVibrationParam off{0, 0};
    ASSERT_EQ(scePadSetVibration(1, &off), PAD_OK);
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out));
    EXPECT_EQ(out.vibrationLarge, 0);

    EXPECT_EQ(scePadSetVibration(1, nullptr), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetVibration(99, &v), PAD_ERROR_INVALID_HANDLE);
    EXPECT_EQ(scePadSetVibration(2, &v), PAD_ERROR_INVALID_HANDLE) << "slot 1 was never opened";
}

// Invariant: repeating the same light bar colour (many titles do it every frame)
// or resetting an already-default light bar must not bump the output sequence,
// otherwise the window thread re-sends the whole output state, including raw
// trigger effect reports that SDL does not de-duplicate.
TEST_F(PadOutputTest, LightBarRepeatDoesNotBumpSequence) {
    const PadLightBarParam c{10, 20, 30};
    ASSERT_EQ(scePadSetLightBar(1, &c), PAD_OK);
    std::uint32_t seen = 0xFFFFFFFFu;
    PadOutputState out;
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out));
    ASSERT_EQ(scePadSetLightBar(1, &c), PAD_OK);
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "same colour";
    const PadLightBarParam other{10, 20, 31};
    ASSERT_EQ(scePadSetLightBar(1, &other), PAD_OK);
    EXPECT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "colour changed";
    ASSERT_EQ(scePadResetLightBar(1), PAD_OK);
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out));
    ASSERT_EQ(scePadResetLightBar(1), PAD_OK);
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "already default";
}

// Invariant: a light bar request is flagged valid with its colour, and a reset
// returns to "default colour" so the poller restores it on the host pad.
TEST_F(PadOutputTest, LightBarSetAndReset) {
    const PadLightBarParam c{10, 20, 30};
    ASSERT_EQ(scePadSetLightBar(1, &c), PAD_OK);
    PadOutputState out = Fetch(0);
    EXPECT_TRUE(out.lightBarValid);
    EXPECT_EQ(out.lightBar, (std::array<std::uint8_t, 3>{10, 20, 30}));

    ASSERT_EQ(scePadResetLightBar(1), PAD_OK);
    out = Fetch(0);
    EXPECT_FALSE(out.lightBarValid);
    EXPECT_EQ(scePadResetLightBar(7), PAD_ERROR_INVALID_HANDLE);
}

// Invariant: scePadSetTriggerEffect updates only the selected triggers, encodes
// the DualSense block, and returns INVALID_ARG for null, an unknown mask bit or
// a mode above 6, INVALID_HANDLE for a closed or out-of-range handle.
TEST_F(PadOutputTest, TriggerEffectDecodesSelectedTrigger) {
    const auto param = Param(0x02, 0, 1, {4, 8}); // R2 only: feedback from zone 4 at strength 8
    ASSERT_EQ(scePadSetTriggerEffect(1, param.data()), PAD_OK);
    const PadOutputState out = Fetch(0);
    EXPECT_TRUE(out.triggerTouched);
    EXPECT_TRUE(out.trigger[1].valid);
    EXPECT_EQ(out.trigger[1].effect[0], 0x21);
    EXPECT_FALSE(out.trigger[0].valid) << "L2 was not selected";

    const auto bothOff = Param(0x03, 0, 0, {});
    ASSERT_EQ(scePadSetTriggerEffect(1, bothOff.data()), PAD_OK);
    const PadOutputState cleared = Fetch(0);
    EXPECT_FALSE(cleared.trigger[1].valid);

    EXPECT_EQ(scePadSetTriggerEffect(1, nullptr), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetTriggerEffect(1, Param(0x04, 0, 0, {}).data()), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetTriggerEffect(1, Param(0x01, 9, 0, {}).data()), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetTriggerEffect(0, param.data()), PAD_ERROR_INVALID_HANDLE);
    EXPECT_EQ(scePadSetTriggerEffect(3, param.data()), PAD_ERROR_INVALID_HANDLE);
}

// Invariant: scePadSetVibrationMode accepts only 0 (desktop) and 1 (embedded).
TEST_F(PadOutputTest, VibrationModeValidation) {
    EXPECT_EQ(scePadSetVibrationMode(1, 0), PAD_OK);
    EXPECT_EQ(scePadSetVibrationMode(1, 1), PAD_OK);
    EXPECT_EQ(scePadSetVibrationMode(1, 2), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetVibrationMode(1, -1), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadSetVibrationMode(5, 0), PAD_ERROR_INVALID_HANDLE);
}

// Invariant: closing a handle drops its output request to defaults and bumps the
// sequence, so the window thread silences rumble and clears the light bar and
// trigger effect instead of leaving the host pad stuck.
TEST_F(PadOutputTest, CloseResetsOutputToDefaults) {
    const PadVibrationParam v{255, 255};
    const PadLightBarParam c{1, 2, 3};
    const auto trig = Param(0x03, 1, 1, {0, 8});
    ASSERT_EQ(scePadSetVibration(1, &v), PAD_OK);
    ASSERT_EQ(scePadSetLightBar(1, &c), PAD_OK);
    ASSERT_EQ(scePadSetTriggerEffect(1, trig.data()), PAD_OK);
    std::uint32_t seen = 0xFFFFFFFFu;
    PadOutputState out;
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out));
    ASSERT_EQ(out.vibrationLarge, 255);

    ASSERT_EQ(scePadClose_nid_postfix(1), PAD_OK);
    ASSERT_TRUE(PadFetchOutput_nid_postfix(0, &seen, &out)) << "close must be visible to the poller";
    EXPECT_EQ(out.vibrationLarge, 0);
    EXPECT_EQ(out.vibrationSmall, 0);
    EXPECT_FALSE(out.lightBarValid);
    EXPECT_FALSE(out.triggerTouched);
    EXPECT_FALSE(out.trigger[0].valid);
    EXPECT_TRUE(out.motionEnabled);
}

// Invariant: output is per slot. A request on slot 1 never shows up in slot 0's
// queue, and fetching an out-of-range slot or with null pointers returns false.
TEST_F(PadOutputTest, OutputIsPerSlotAndArgumentsAreChecked) {
    PadSetControllerConnected_nid_postfix(1, true);
    ASSERT_EQ(scePadOpen_nid_postfix(kUser, PAD_PORT_TYPE_STANDARD, 1, nullptr), 2);
    const PadVibrationParam v{9, 8};
    ASSERT_EQ(scePadSetVibration(2, &v), PAD_OK);
    EXPECT_EQ(Fetch(1).vibrationLarge, 9);
    EXPECT_EQ(Fetch(0).vibrationLarge, 0);

    std::uint32_t seen = 0xFFFFFFFFu;
    PadOutputState out;
    EXPECT_FALSE(PadFetchOutput_nid_postfix(-1, &seen, &out));
    EXPECT_FALSE(PadFetchOutput_nid_postfix(PAD_MAX_SLOTS, &seen, &out));
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, nullptr, &out));
    EXPECT_FALSE(PadFetchOutput_nid_postfix(0, &seen, nullptr));
}

// Invariant: motion data reaches scePadReadState only while a controller delivers
// it and the guest has the sensors enabled. Acceleration converts to g and the
// gyro passes through; disabling the sensors returns the rest pose and is
// visible to the poller (motionEnabled) so the host sensors can be switched off.
TEST_F(PadOutputTest, MotionIsLiveOnlyWhenEnabledAndPresent) {
    PadSetControllerConnected_nid_postfix(0, true);
    PadInputState s;
    s.hasMotion = true;
    s.accel = {0.0f, Pad::kStandardGravity / 2.0f, Pad::kStandardGravity};
    s.gyro = {0.25f, -0.5f, 1.0f};
    PadPublishControllerInput_nid_postfix(0, s);

    PadData d{};
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_NEAR(d.acceleration_x, 0.0f, 1e-6f);
    EXPECT_NEAR(d.acceleration_y, 0.5f, 1e-6f);
    EXPECT_NEAR(d.acceleration_z, 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(d.angular_velocity_x, 0.25f);
    EXPECT_FLOAT_EQ(d.angular_velocity_y, -0.5f);
    EXPECT_FLOAT_EQ(d.angular_velocity_z, 1.0f);

    ASSERT_EQ(scePadSetMotionSensorState(1, false), PAD_OK);
    EXPECT_FALSE(Fetch(0).motionEnabled);
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_FLOAT_EQ(d.acceleration_y, 1.0f);
    EXPECT_FLOAT_EQ(d.acceleration_x, 0.0f);
    EXPECT_FLOAT_EQ(d.angular_velocity_y, 0.0f);
    EXPECT_FLOAT_EQ(d.orientation_w, 1.0f);
}

// Invariant: a keyboard-only slot (no controller) reports the rest pose, the
// behaviour titles saw before motion plumbing existed.
TEST_F(PadOutputTest, KeyboardOnlySlotReportsRestPose) {
    PadData d{};
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_FLOAT_EQ(d.acceleration_y, 1.0f);
    EXPECT_FLOAT_EQ(d.orientation_w, 1.0f);
    EXPECT_FLOAT_EQ(d.angular_velocity_x, 0.0f);
}

// Invariant: scePadResetOrientation succeeds on an open handle and returns the
// SCE handle error otherwise; after a reset the orientation is the identity.
TEST_F(PadOutputTest, ResetOrientationReturnsToIdentity) {
    PadSetControllerConnected_nid_postfix(0, true);
    PadInputState s;
    s.hasMotion = true;
    s.gyro = {0.0f, 3.0f, 0.0f};
    PadPublishControllerInput_nid_postfix(0, s);
    PadData d{};
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    ASSERT_EQ(scePadResetOrientation(1), PAD_OK);
    EXPECT_EQ(scePadResetOrientation(0), PAD_ERROR_INVALID_HANDLE);
    EXPECT_EQ(scePadResetOrientation(4), PAD_ERROR_INVALID_HANDLE);
    s.gyro = {0.0f, 0.0f, 0.0f};
    PadPublishControllerInput_nid_postfix(0, s);
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_NEAR(d.orientation_w, 1.0f, 1e-3f);
    EXPECT_NEAR(d.orientation_y, 0.0f, 1e-3f);
}

// Invariant: controller touchpad fingers are reported with their coordinates
// and distinct ids; with no finger down, slot 0 falls back to the keyboard
// TouchLeft/TouchRight emulation and sets the touchpad click bit.
TEST_F(PadOutputTest, TouchFingersAndKeyboardFallback) {
    PadSetControllerConnected_nid_postfix(0, true);
    PadInputState s;
    s.touch[0] = Pad::ScaleTouchFinger(0.5f, 0.25f);
    s.touch[1] = Pad::ScaleTouchFinger(1.0f, 1.0f);
    PadPublishControllerInput_nid_postfix(0, s);
    PadData d{};
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_EQ(d.touch_data_touch_num, 2);
    EXPECT_EQ(d.touch_data_touch0_x, 959);
    EXPECT_EQ(d.touch_data_touch0_y, 235);
    EXPECT_EQ(d.touch_data_touch1_x, 1919);
    EXPECT_EQ(d.touch_data_touch1_y, 942);
    EXPECT_NE(d.touch_data_touch0_id, d.touch_data_touch1_id);

    PadPublishControllerInput_nid_postfix(0, PadInputState{});
    PadInputState kb;
    kb.touchRight = true;
    PadPublishInput_nid_postfix(kb);
    ASSERT_EQ(scePadReadState(1, &d), PAD_OK);
    EXPECT_EQ(d.touch_data_touch_num, 1);
    EXPECT_EQ(d.touch_data_touch0_x, 1440);
    EXPECT_NE(d.buttons & 0x100000u, 0u);
}

// Invariant: BuildControllerState carries the sensor and touchpad fields of a
// sample through unchanged, so the window thread's sampling reaches scePadRead.
TEST(ControllerMappingExtras, MotionAndTouchPassThrough) {
    Pad::ControllerSample sample;
    sample.hasMotion = true;
    sample.accel = {1.0f, 2.0f, 3.0f};
    sample.gyro = {4.0f, 5.0f, 6.0f};
    sample.touch[1] = Pad::ScaleTouchFinger(0.0f, 1.0f);
    const PadInputState state = Pad::BuildControllerState(sample, 0.08);
    EXPECT_TRUE(state.hasMotion);
    EXPECT_EQ(state.accel, (std::array<float, 3>{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(state.gyro, (std::array<float, 3>{4.0f, 5.0f, 6.0f}));
    EXPECT_FALSE(state.touch[0].active);
    EXPECT_TRUE(state.touch[1].active);
    EXPECT_EQ(state.touch[1].y, 942);

    const PadInputState idle = Pad::BuildControllerState(Pad::ControllerSample{}, 0.08);
    EXPECT_FALSE(idle.hasMotion);
}

// Invariant: the device-class and trigger-state queries answer a standard pad
// (zeroed class data, neutral trigger state) instead of aborting, and validate
// handle and pointers with SCE error codes.
TEST_F(PadOutputTest, DeviceClassAndTriggerStateQueries) {
    PadTriggerEffectStateInformation ts;
    std::memset(&ts, 0xAB, sizeof(ts));
    ASSERT_EQ(scePadGetTriggerEffectState(1, &ts), PAD_OK);
    EXPECT_EQ(ts.state[0], 0);
    EXPECT_EQ(ts.state[1], 0);
    EXPECT_EQ(scePadGetTriggerEffectState(1, nullptr), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadGetTriggerEffectState(0, &ts), PAD_ERROR_INVALID_HANDLE);

    PadDeviceClassExtendedInformation ext;
    std::memset(&ext, 0xAB, sizeof(ext));
    ASSERT_EQ(scePadDeviceClassGetExtendedInformation(1, &ext), PAD_OK);
    EXPECT_EQ(ext.deviceClass, PAD_DEVICE_CLASS_STANDARD);
    EXPECT_EQ(ext.classData.data[0], 0);
    EXPECT_EQ(scePadDeviceClassGetExtendedInformation(1, nullptr), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadDeviceClassGetExtendedInformation(9, &ext), PAD_ERROR_INVALID_HANDLE);

    PadData data{};
    ASSERT_EQ(scePadReadState(1, &data), PAD_OK);
    PadDeviceClassData cd;
    std::memset(&cd, 0xAB, sizeof(cd));
    ASSERT_EQ(scePadDeviceClassParseData(1, &data, &cd), PAD_OK);
    EXPECT_EQ(cd.deviceClass, PAD_DEVICE_CLASS_STANDARD);
    EXPECT_EQ(cd.dataValid, data.connected);
    EXPECT_EQ(scePadDeviceClassParseData(1, nullptr, &cd), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadDeviceClassParseData(1, &data, nullptr), PAD_ERROR_INVALID_ARG);
    EXPECT_EQ(scePadDeviceClassParseData(-1, &data, &cd), PAD_ERROR_INVALID_HANDLE);
}
