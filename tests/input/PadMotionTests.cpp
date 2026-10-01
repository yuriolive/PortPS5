// Unit tests: pad motion and touchpad maths (docs/spec/input.md §State model).
//
// Exercises the pure helpers in PadMotion.hpp with synthetic sensor samples:
// m/s^2 to g scaling, Mahony orientation fusion (rest, yaw integration, tilt
// correction, malformed input), touchpad coordinate scaling and touch id
// assignment. No controller, SDL or game data is involved. The fusion sign
// conventions are checked against hand-derived rotations only; behaviour on a
// physical DualSense is verified locally by the maintainer, not in CI.
#include "prx/libScePad/include/PadMotion.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace {

// Gravity direction the filter predicts in the body frame for orientation q.
std::array<float, 3> PredictedGravity(const Pad::Orientation& o) {
    return {2.0f * (o.x * o.y + o.w * o.z), o.w * o.w - o.x * o.x + o.y * o.y - o.z * o.z, 2.0f * (o.y * o.z - o.w * o.x)};
}

} // namespace

// Invariant: guest acceleration is in g, so SDL's rest reading (+9.80665 on Y)
// becomes exactly {0, 1, 0}, matching the constant rest pose the pad reported
// before sensors existed.
TEST(PadMotion, AccelerationScalesToGravityUnits) {
    const auto g = Pad::ToGravityUnits(Pad::RestAcceleration());
    EXPECT_FLOAT_EQ(g[0], 0.0f);
    EXPECT_FLOAT_EQ(g[1], 1.0f);
    EXPECT_FLOAT_EQ(g[2], 0.0f);
    const auto half = Pad::ToGravityUnits({4.903325f, -9.80665f, 19.6133f});
    EXPECT_NEAR(half[0], 0.5f, 1e-6f);
    EXPECT_NEAR(half[1], -1.0f, 1e-6f);
    EXPECT_NEAR(half[2], 2.0f, 1e-6f);
}

// Invariant: a still pad at rest keeps the identity orientation indefinitely
// (no drift from the correction term) and the quaternion stays unit length.
TEST(PadMotion, RestKeepsIdentity) {
    Pad::MotionFusion f;
    for (int i = 0; i < 1000; ++i) Pad::FuseMotion(f, 0.01f, Pad::RestAcceleration(), {0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(f.q.w, 1.0f, 1e-5f);
    EXPECT_NEAR(f.q.x, 0.0f, 1e-5f);
    EXPECT_NEAR(f.q.y, 0.0f, 1e-5f);
    EXPECT_NEAR(f.q.z, 0.0f, 1e-5f);
}

// Invariant: a yaw rate about the up axis (+Y) of pi/2 rad/s for one second
// turns the pad 90 degrees: q = (cos 45, 0, sin 45, 0). Gravity lies on the
// rotation axis, so tilt correction must not disturb it.
TEST(PadMotion, YawRateIntegrates) {
    Pad::MotionFusion f;
    constexpr float halfPi = 1.57079632679f;
    for (int i = 0; i < 100; ++i) Pad::FuseMotion(f, 0.01f, Pad::RestAcceleration(), {0.0f, halfPi, 0.0f});
    EXPECT_NEAR(f.q.w, 0.70710678f, 0.01f);
    EXPECT_NEAR(f.q.y, 0.70710678f, 0.01f);
    EXPECT_NEAR(f.q.x, 0.0f, 0.01f);
    EXPECT_NEAR(f.q.z, 0.0f, 0.01f);
}

// Invariant: with zero gyro, a tilted accelerometer pulls the estimate until its
// predicted gravity matches the measured direction (tilt correction), and the
// quaternion stays normalised while doing so.
TEST(PadMotion, TiltCorrectionConverges) {
    Pad::MotionFusion f;
    const std::array<float, 3> tilted{Pad::kStandardGravity, 0.0f, 0.0f}; // gravity along +X
    for (int i = 0; i < 1000; ++i) Pad::FuseMotion(f, 0.01f, tilted, {0.0f, 0.0f, 0.0f});
    const auto v = PredictedGravity(f.q);
    EXPECT_GT(v[0], 0.99f);
    const float norm = std::sqrt(f.q.w * f.q.w + f.q.x * f.q.x + f.q.y * f.q.y + f.q.z * f.q.z);
    EXPECT_NEAR(norm, 1.0f, 1e-4f);
}

// Invariant: a non-finite sensor value or a non-positive/NaN step is dropped
// whole, so one bad sample can never poison the orientation with NaN.
TEST(PadMotion, MalformedSamplesAreIgnored) {
    Pad::MotionFusion f;
    const float nan = std::nanf("");
    Pad::FuseMotion(f, 0.01f, {nan, 9.8f, 0.0f}, {0.0f, 0.0f, 0.0f});
    Pad::FuseMotion(f, 0.01f, Pad::RestAcceleration(), {0.0f, INFINITY, 0.0f});
    Pad::FuseMotion(f, nan, Pad::RestAcceleration(), {0.0f, 1.0f, 0.0f});
    Pad::FuseMotion(f, -1.0f, Pad::RestAcceleration(), {0.0f, 1.0f, 0.0f});
    Pad::FuseMotion(f, 0.0f, Pad::RestAcceleration(), {0.0f, 1.0f, 0.0f});
    EXPECT_FLOAT_EQ(f.q.w, 1.0f);
    EXPECT_FLOAT_EQ(f.q.y, 0.0f);
    EXPECT_FLOAT_EQ(f.bias[1], 0.0f);
}

// Invariant: an enormous step (a stalled window thread) is clamped to
// kMaxFusionStepSeconds, so it rotates exactly as far as a 0.1 s step.
TEST(PadMotion, StepIsClamped) {
    Pad::MotionFusion a;
    Pad::MotionFusion b;
    Pad::FuseMotion(a, 100.0f, Pad::RestAcceleration(), {0.0f, 1.0f, 0.0f});
    Pad::FuseMotion(b, Pad::kMaxFusionStepSeconds, Pad::RestAcceleration(), {0.0f, 1.0f, 0.0f});
    EXPECT_FLOAT_EQ(a.q.y, b.q.y);
    EXPECT_FLOAT_EQ(a.q.w, b.q.w);
}

// Invariant: ResetFusion returns to the identity with no bias, which is what
// scePadResetOrientation and a motion-disable rely on.
TEST(PadMotion, ResetRestoresIdentity) {
    Pad::MotionFusion f;
    for (int i = 0; i < 50; ++i) Pad::FuseMotion(f, 0.01f, {Pad::kStandardGravity, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    Pad::ResetFusion(f);
    EXPECT_FLOAT_EQ(f.q.w, 1.0f);
    EXPECT_FLOAT_EQ(f.q.x, 0.0f);
    EXPECT_FLOAT_EQ(f.bias[0], 0.0f);
}

// Invariant: touchpad scaling maps 0..1 onto 0..1919 / 0..942, clamps values
// outside the pad, and turns NaN into 0, so no coordinate exceeds the
// resolution reported in scePadGetControllerInformation.
TEST(PadMotion, TouchFingerScalingAndClamp) {
    const Pad::PadTouchPoint origin = Pad::ScaleTouchFinger(0.0f, 0.0f);
    EXPECT_TRUE(origin.active);
    EXPECT_EQ(origin.x, 0);
    EXPECT_EQ(origin.y, 0);
    const Pad::PadTouchPoint corner = Pad::ScaleTouchFinger(1.0f, 1.0f);
    EXPECT_EQ(corner.x, 1919);
    EXPECT_EQ(corner.y, 942);
    const Pad::PadTouchPoint mid = Pad::ScaleTouchFinger(0.5f, 0.5f);
    EXPECT_EQ(mid.x, 959);
    EXPECT_EQ(mid.y, 471);
    const Pad::PadTouchPoint wild = Pad::ScaleTouchFinger(-5.0f, 7.0f);
    EXPECT_EQ(wild.x, 0);
    EXPECT_EQ(wild.y, 942);
    const Pad::PadTouchPoint bad = Pad::ScaleTouchFinger(std::nanf(""), std::nanf(""));
    EXPECT_EQ(bad.x, 0);
    EXPECT_EQ(bad.y, 0);
}

// Invariant: a continuous contact keeps one id, a new touch after a lift gets a
// fresh id, two fingers never share one, and ids wrap within 7 bits.
TEST(PadMotion, TouchIdsIdentifyContacts) {
    Pad::TouchIdTracker t;
    t.Update({true, false});
    EXPECT_EQ(t.ids[0], 0);
    t.Update({true, true});
    EXPECT_EQ(t.ids[0], 0);
    EXPECT_EQ(t.ids[1], 1);
    t.Update({false, true});
    t.Update({true, true});
    EXPECT_EQ(t.ids[0], 2);
    EXPECT_EQ(t.ids[1], 1);

    Pad::TouchIdTracker wrap;
    for (int i = 0; i < 128; ++i) {
        wrap.Update({true, false});
        wrap.Update({false, false});
    }
    wrap.Update({true, false});
    EXPECT_EQ(wrap.ids[0], 0); // 129th contact wraps to id 0
}
