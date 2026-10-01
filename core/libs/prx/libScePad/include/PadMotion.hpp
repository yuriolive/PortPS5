// Pure motion and touchpad maths for libScePad (no device, no global state).
//
// Subsystem: input (docs/spec/input.md §Target design 4). Owned by libScePad;
// PadManager keeps one MotionFusion and one TouchIdTracker per pad slot and calls
// these under its mutex, while unit tests feed synthetic samples directly.
// Units follow SDL's controller sensors: acceleration in m/s^2 (+Y up when the
// pad lies flat, so rest = +9.80665 on Y) and angular velocity in rad/s. The
// guest ScePadData reports acceleration in g, hence ToGravityUnits.
// Thread-safety: functions are reentrant; the state structs are plain data and
// must be guarded by the caller.
//
// Mahony filter provenance: AnyPS5 main@4c349efe (GPL-2.0), re-expressed here
// with the gain constants named. The sign conventions are unverified against a
// physical DualSense; they are only exercised by synthetic samples in CI.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADMOTION_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADMOTION_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Pad {

/** Standard gravity in m/s^2, used to convert SDL acceleration to guest g units. */
constexpr float kStandardGravity = 9.80665f;
/** Longest integration step; a stalled window thread must not spin the filter. */
constexpr float kMaxFusionStepSeconds = 0.1f;
/** Accelerometer magnitudes below this (m/s^2) are treated as free fall and ignored for tilt correction. */
constexpr float kMinTiltCorrectionAccel = 1.0f;
/** Mahony proportional gain. */
constexpr float kMahonyKp = 1.5f;
/** Mahony integral gain (gyro bias estimate). */
constexpr float kMahonyKi = 0.02f;
/** Touchpad resolution reported to the guest (matches scePadGetControllerInformation). */
constexpr std::uint16_t kTouchWidth = 1920;
constexpr std::uint16_t kTouchHeight = 943;

/** One finger on the touchpad in guest coordinates (0..1919, 0..942). */
struct PadTouchPoint {
    bool active = false;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool operator==(const PadTouchPoint&) const = default;
};

/** Unit quaternion; the identity is the pad lying flat and face up. */
struct Orientation {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/** Orientation estimate plus the Mahony integral (gyro bias) term. */
struct MotionFusion {
    Orientation q;
    std::array<float, 3> bias{};
};

/**
 * @brief Converts SDL acceleration (m/s^2) to the guest's g units.
 *
 * @param accel Acceleration in m/s^2.
 * @return Acceleration in g (rest = 1.0 on the up axis).
 */
constexpr std::array<float, 3> ToGravityUnits(const std::array<float, 3>& accel) {
    return {accel[0] / kStandardGravity, accel[1] / kStandardGravity, accel[2] / kStandardGravity};
}

/** The sensor reading of a pad lying flat and still. */
constexpr std::array<float, 3> RestAcceleration() { return {0.0f, kStandardGravity, 0.0f}; }

/** Resets `fusion` to the identity orientation with no bias. */
inline void ResetFusion(MotionFusion& fusion) { fusion = MotionFusion{}; }

/**
 * @brief Advances the orientation estimate by one sample (Mahony filter).
 *
 * Non-finite inputs and non-positive `dt` leave the state unchanged, so a
 * malformed sample can never poison the quaternion with NaN.
 *
 * @param fusion Estimate to update in place.
 * @param dt Seconds since the previous sample; clamped to kMaxFusionStepSeconds.
 * @param accel Accelerometer, m/s^2.
 * @param gyro Gyroscope, rad/s.
 */
inline void FuseMotion(MotionFusion& fusion, float dt, const std::array<float, 3>& accel, const std::array<float, 3>& gyro) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(accel[static_cast<std::size_t>(i)]) || !std::isfinite(gyro[static_cast<std::size_t>(i)])) return;
    }
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, kMaxFusionStepSeconds);
    Orientation& o = fusion.q;
    float gx = gyro[0], gy = gyro[1], gz = gyro[2];
    const float an = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (an > kMinTiltCorrectionAccel) {
        const float ax = accel[0] / an, ay = accel[1] / an, az = accel[2] / an;
        // Gravity direction predicted by the current orientation in the body frame (rest pose: +Y).
        const float vx = 2.0f * (o.x * o.y + o.w * o.z);
        const float vy = o.w * o.w - o.x * o.x + o.y * o.y - o.z * o.z;
        const float vz = 2.0f * (o.y * o.z - o.w * o.x);
        const float ex = ay * vz - az * vy;
        const float ey = az * vx - ax * vz;
        const float ez = ax * vy - ay * vx;
        fusion.bias[0] += kMahonyKi * ex * dt;
        fusion.bias[1] += kMahonyKi * ey * dt;
        fusion.bias[2] += kMahonyKi * ez * dt;
        gx += kMahonyKp * ex + fusion.bias[0];
        gy += kMahonyKp * ey + fusion.bias[1];
        gz += kMahonyKp * ez + fusion.bias[2];
    }
    const float hx = 0.5f * gx * dt, hy = 0.5f * gy * dt, hz = 0.5f * gz * dt;
    const Orientation q = o;
    o.w = q.w - q.x * hx - q.y * hy - q.z * hz;
    o.x = q.x + q.w * hx + q.y * hz - q.z * hy;
    o.y = q.y + q.w * hy - q.x * hz + q.z * hx;
    o.z = q.z + q.w * hz + q.x * hy - q.y * hx;
    const float n = std::sqrt(o.w * o.w + o.x * o.x + o.y * o.y + o.z * o.z);
    if (n < 1e-6f) {
        o = Orientation{};
    } else {
        o.w /= n;
        o.x /= n;
        o.y /= n;
        o.z /= n;
    }
}

/**
 * @brief Scales an SDL normalised touchpad finger (0..1) to guest coordinates.
 *
 * Out-of-range values clamp to the pad edge and NaN maps to 0, so a bad
 * driver sample can never produce a coordinate outside the reported resolution.
 *
 * @param nx Normalised X.
 * @param ny Normalised Y.
 * @return The finger, active, at 0..1919 / 0..942.
 */
inline PadTouchPoint ScaleTouchFinger(float nx, float ny) {
    const auto scale = [](float v, float extent) {
        if (!(v == v)) return 0.0f; // NaN
        return std::clamp(v, 0.0f, 1.0f) * extent;
    };
    PadTouchPoint point;
    point.active = true;
    point.x = static_cast<std::uint16_t>(scale(nx, static_cast<float>(kTouchWidth - 1)));
    point.y = static_cast<std::uint16_t>(scale(ny, static_cast<float>(kTouchHeight - 1)));
    return point;
}

/**
 * @brief Hands out a fresh 7-bit touch id each time a finger goes down.
 *
 * ScePadData touch ids identify one continuous contact; a lifted finger that
 * touches again gets a new id so titles can tell taps apart.
 */
struct TouchIdTracker {
    std::uint8_t next = 0;
    std::array<bool, 2> previous{};
    std::array<std::uint8_t, 2> ids{};

    /**
     * @brief Records which fingers are down this sample.
     * @param active Per-finger contact state.
     */
    void Update(const std::array<bool, 2>& active) {
        for (std::size_t i = 0; i < active.size(); ++i) {
            if (active[i] && !previous[i]) ids[i] = static_cast<std::uint8_t>(next++ & 0x7F);
            previous[i] = active[i];
        }
    }
};

} // namespace Pad

#endif
