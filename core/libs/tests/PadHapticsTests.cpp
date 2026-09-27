// Unit tests for libScePad: multi-slot assignment, monotonic timestamp advancement,
// and KytyPS5 PadHapticsTests (DualSense USB report parsing, radial deadzone calculation,
// rumble motor translation).
//
// Ref: docs/spec/input.md

#define SDL_MAIN_HANDLED
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libScePad/include/Pad.hpp"
#include "prx/libScePad/src/PadInternal.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void Require(bool value) {
    if (!value) std::abort();
}
#define REQUIRE(cond) Require(cond)

// Mathematics helper from KytyPS5 PadHapticsTests: radial deadzone calculation
// Inputs: raw normalized stick x, y in [-1.0f, 1.0f]
// Returns adjusted (x, y) with radial deadzone applied
std::pair<float, float> ApplyRadialDeadzone(float x, float y, float deadzone) {
    float mag = std::sqrt(x * x + y * y);
    if (mag <= deadzone) {
        return {0.0f, 0.0f};
    }
    // Scale remaining magnitude to [0.0, 1.0]
    float scaledMag = std::min((mag - deadzone) / (1.0f - deadzone), 1.0f);
    float normX = x / mag;
    float normY = y / mag;
    return {normX * scaledMag, normY * scaledMag};
}

// DualSense USB Report Parser simulation (KytyPS5 PadHapticsTests)
struct DualSenseUsbReport {
    uint8_t stickLeftX;
    uint8_t stickLeftY;
    uint8_t stickRightX;
    uint8_t stickRightY;
    uint8_t triggerL2;
    uint8_t triggerR2;
    uint32_t buttons;
};

void ParseDualSenseReport(const DualSenseUsbReport& report, PadData& outData) {
    outData.left_stick_x = report.stickLeftX;
    outData.left_stick_y = report.stickLeftY;
    outData.right_stick_x = report.stickRightX;
    outData.right_stick_y = report.stickRightY;
    outData.analog_buttons_l2 = report.triggerL2;
    outData.analog_buttons_r2 = report.triggerR2;
    outData.buttons = report.buttons;
    if (report.triggerL2 > 0) outData.buttons |= 0x0100; // L2
    if (report.triggerR2 > 0) outData.buttons |= 0x0200; // R2
}

} // namespace

extern "C" {
int APS5_VABI scePadInit_nid_postfix(void) noexcept;
int APS5_VABI scePadOpen_nid_postfix(int userId, int type, int index, const void* param) noexcept;
int APS5_VABI scePadClose_nid_postfix(int handle) noexcept;
int APS5_VABI scePadRead_nid_postfix(int handle, PadData* data, int num) noexcept;
int APS5_VABI scePadReadState(int handle, PadData* data) noexcept;
int APS5_VABI scePadGetControllerInformation(int handle, PadControllerInformation* info) noexcept;
int APS5_VABI scePadSetMotionSensorState(int handle, bool enable) noexcept;
int APS5_VABI scePadSetVibration(int handle, const PadVibrationParam* param) noexcept;
int APS5_VABI scePadSetLightBar(int handle, const PadLightBarParam* param) noexcept;
int APS5_VABI scePadResetLightBar(int handle) noexcept;
}

static void TestSlotLifecycle() {
    REQUIRE(scePadInit_nid_postfix() == PAD_OK);

    // Slot 0 (index 0)
    int h0 = scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 0, nullptr);
    REQUIRE(h0 == 1);

    // Invalid index
    REQUIRE(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, -1, nullptr) == PAD_ERROR_INVALID_ARG);
    REQUIRE(scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 4, nullptr) == PAD_ERROR_INVALID_ARG);

    // Additional slots (slots 1, 2, 3)
    int h1 = scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 1, nullptr);
    REQUIRE(h1 == 2);
    int h2 = scePadOpen_nid_postfix(0x10000000, PAD_PORT_TYPE_STANDARD, 2, nullptr);
    REQUIRE(h2 == 3);

    // Controller info on valid and invalid handles
    PadControllerInformation info{};
    REQUIRE(scePadGetControllerInformation(h0, &info) == PAD_OK);
    REQUIRE(info.connected);
    REQUIRE(info.connectedCount >= 1);

    REQUIRE(scePadGetControllerInformation(99, &info) == PAD_ERROR_INVALID_HANDLE);
    REQUIRE(scePadGetControllerInformation(h0, nullptr) == PAD_ERROR_INVALID_ARG);

    // Close slots
    REQUIRE(scePadClose_nid_postfix(h1) == PAD_OK);
    REQUIRE(scePadClose_nid_postfix(h2) == PAD_OK);
    REQUIRE(scePadClose_nid_postfix(99) == PAD_ERROR_INVALID_HANDLE);
}

static void TestMonotonicTimestampAdvancement() {
    int h0 = 1;
    PadData d1{}, d2{};
    REQUIRE(scePadReadState(h0, &d1) == PAD_OK);
    REQUIRE(scePadReadState(h0, &d2) == PAD_OK);
    REQUIRE(d2.timestamp >= d1.timestamp);
}

static void TestVibrationAndLightBar() {
    int h0 = 1;
    PadVibrationParam vib{128, 255};
    REQUIRE(scePadSetVibration(h0, &vib) == PAD_OK);
    REQUIRE(scePadSetVibration(99, &vib) == PAD_ERROR_INVALID_HANDLE);
    REQUIRE(scePadSetVibration(h0, nullptr) == PAD_ERROR_INVALID_ARG);

    PadLightBarParam light{0, 255, 128};
    REQUIRE(scePadSetLightBar(h0, &light) == PAD_OK);
    REQUIRE(scePadSetLightBar(99, &light) == PAD_ERROR_INVALID_HANDLE);
    REQUIRE(scePadSetLightBar(h0, nullptr) == PAD_ERROR_INVALID_ARG);
    REQUIRE(scePadResetLightBar(h0) == PAD_OK);
    REQUIRE(scePadResetLightBar(99) == PAD_ERROR_INVALID_HANDLE);
}

static void TestRadialDeadzoneKyty() {
    // Within 8% radial deadzone
    auto [x1, y1] = ApplyRadialDeadzone(0.04f, 0.04f, 0.08f);
    REQUIRE(x1 == 0.0f);
    REQUIRE(y1 == 0.0f);

    // Outside deadzone
    auto [x2, y2] = ApplyRadialDeadzone(0.5f, 0.0f, 0.08f);
    REQUIRE(x2 > 0.0f);
    REQUIRE(y2 == 0.0f);

    // Edge clamp
    auto [x3, y3] = ApplyRadialDeadzone(1.0f, 0.0f, 0.08f);
    REQUIRE(std::fabs(x3 - 1.0f) < 1e-4f);
}

static void TestDualSenseReportParsing() {
    DualSenseUsbReport report{};
    report.stickLeftX = 200;
    report.stickLeftY = 50;
    report.stickRightX = 128;
    report.stickRightY = 128;
    report.triggerL2 = 255;
    report.triggerR2 = 0;
    report.buttons = 0x4000; // Cross

    PadData outData{};
    ParseDualSenseReport(report, outData);
    REQUIRE(outData.left_stick_x == 200);
    REQUIRE(outData.left_stick_y == 50);
    REQUIRE(outData.analog_buttons_l2 == 255);
    REQUIRE(outData.analog_buttons_r2 == 0);
    REQUIRE((outData.buttons & 0x4000) != 0); // Cross
    REQUIRE((outData.buttons & 0x0100) != 0); // L2 analog bit set
}

int main() {
    TestSlotLifecycle();
    TestMonotonicTimestampAdvancement();
    TestVibrationAndLightBar();
    TestRadialDeadzoneKyty();
    TestDualSenseReportParsing();
    return 0;
}
