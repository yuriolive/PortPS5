// Pure guest pad output -> host DualSense translation (no device, no global state).
//
// Subsystem: input (docs/spec/input.md §Output calls). Owned by libScePad; the
// guest-facing exports (scePadSetTriggerEffect, scePadSetVibration) decode into
// the types below, and libSceVideoOut's window-thread poller turns them into SDL
// calls. Keeping the byte maths here (header-only, SDL-free) lets unit tests
// pin every encoding on synthetic buffers with no controller attached.
// Thread-safety: every function is stateless and reentrant.
//
// Layout provenance: the 56-byte ScePadTriggerEffectCommandData layout and the
// DualSense 11-byte trigger effect block follow AnyPS5 main@85517679 and
// 4c349efe (GPL-2.0). The SDL DS5EffectsState offsets are checked against the
// pinned SDL2 (src/joystick/hidapi/SDL_hidapi_ps5.c, DS5EffectsState_t).
// Adaptive triggers are post-1.0 (PRD §5): the encodings here are unverified on
// hardware and are only sent when SDL reports a PS5 controller.
#ifndef CORE_LIBS_PRX_LIBSCEPAD_PADOUTPUTMAPPING_HPP
#define CORE_LIBS_PRX_LIBSCEPAD_PADOUTPUTMAPPING_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Pad {

/** Size in bytes of one DualSense native trigger effect block. */
constexpr std::size_t kTriggerEffectBlockSize = 11;
/** Size in bytes of one guest ScePadTriggerEffectCommandData (u32 mode, 4 pad, 48 data). */
constexpr std::size_t kTriggerCommandSize = 56;
/** Offset of the command payload inside a command (after the u32 mode and 4 padding bytes). */
constexpr std::size_t kTriggerCommandDataOffset = 8;
/** Size of the guest ScePadTriggerEffectParam (u8 mask, 7 pad, two commands). */
constexpr std::size_t kTriggerEffectParamSize = 8 + 2 * kTriggerCommandSize;
/** Highest valid guest ScePadTriggerEffectMode value (multi-position vibration). */
constexpr std::uint32_t kTriggerModeMax = 6;
/** Size of SDL's DS5EffectsState report payload. */
constexpr std::size_t kDs5EffectsStateSize = 47;
/** DS5EffectsState::ucEnableBits1 bit: apply the right trigger effect block. */
constexpr std::uint8_t kDs5EnableRightTrigger = 0x04;
/** DS5EffectsState::ucEnableBits1 bit: apply the left trigger effect block. */
constexpr std::uint8_t kDs5EnableLeftTrigger = 0x08;
/** Light bar colour restored when the guest resets or closes the pad (DualSense blue; inferred from AnyPS5 85517679). */
constexpr std::array<std::uint8_t, 3> kDefaultLightBar{0, 64, 255};
/** How long (ms) an SDL rumble request lasts before it must be refreshed. */
constexpr std::uint32_t kRumbleDurationMs = 2000;
/** Refresh period (ms) while the guest holds a rumble request; below kRumbleDurationMs so it never lapses. */
constexpr std::uint32_t kRumbleRefreshMs = 700;
/** Native effect mode byte meaning "no effect". */
constexpr std::uint8_t kDs5TriggerOff = 0x05;

/**
 * @brief One trigger's requested effect, in DualSense native encoding plus a
 *        rumble fallback for pads without adaptive triggers.
 */
struct TriggerRequest {
    bool valid = false;                              ///< an effect other than "off" is engaged
    std::array<std::uint8_t, kTriggerEffectBlockSize> effect{kDs5TriggerOff}; ///< native effect block
    std::uint8_t fallback = 0;                       ///< 0..255 strength for trigger-rumble motors
    std::uint32_t mode = 0;                          ///< guest ScePadTriggerEffectMode
};

/** Host rumble amplitudes in SDL's 16-bit scale. */
struct SdlRumble {
    std::uint16_t lowFrequency = 0;   ///< SDL low-frequency (large) motor
    std::uint16_t highFrequency = 0;  ///< SDL high-frequency (small) motor
};

/**
 * @brief Converts the guest 8-bit motor amplitudes to SDL's 16-bit rumble scale.
 *
 * Multiplying by 257 maps 0 -> 0 and 255 -> 65535 exactly (257 = 65535 / 255).
 * The guest large motor is the low-frequency one.
 *
 * @param large Guest large motor amplitude, 0..255.
 * @param small Guest small motor amplitude, 0..255.
 * @return SDL amplitudes for SDL_GameControllerRumble.
 */
constexpr SdlRumble RumbleToSdl(std::uint8_t large, std::uint8_t small) {
    return {static_cast<std::uint16_t>(large * 257), static_cast<std::uint16_t>(small * 257)};
}

/**
 * @brief Maps a 1..8 guest strength to a 0..255 rumble fallback (32 per step).
 *
 * @param strength Guest strength, 0 meaning off. Values above 8 saturate at 255.
 * @return 0..255 amplitude.
 */
constexpr std::uint8_t TriggerFallbackStrength(std::uint8_t strength) {
    return strength == 0 ? 0 : static_cast<std::uint8_t>(std::min(255, 32 * std::min<int>(strength, 8)));
}

/**
 * @brief Encodes per-zone strengths into a DualSense effect block.
 *
 * Block layout: byte 0 = mode, bytes 1..2 = little-endian active-zone mask,
 * bytes 3..6 = little-endian 3-bit-per-zone strength-1 field, byte 9 =
 * frequency (vibration modes). Zones with strength 0 or above 8 are inactive.
 * No active zone, or vibration mode 0x26 with frequency 0, encodes "off".
 *
 * @param mode Native mode byte (0x21 feedback, 0x26 vibration).
 * @param strengths Up to 10 per-zone strengths (1..8; others inactive).
 * @param frequency Vibration frequency, ignored unless mode is 0x26.
 * @return The 11-byte block.
 */
inline std::array<std::uint8_t, kTriggerEffectBlockSize> EncodeTriggerZones(
    std::uint8_t mode, const std::array<std::uint8_t, 10>& strengths, std::uint8_t frequency) {
    std::array<std::uint8_t, kTriggerEffectBlockSize> block{kDs5TriggerOff};
    std::uint16_t zones = 0;
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < strengths.size(); ++i) {
        const std::uint8_t s = strengths[i];
        if (s == 0 || s > 8) continue;
        zones = static_cast<std::uint16_t>(zones | (1u << i));
        packed |= static_cast<std::uint32_t>(s - 1) << (i * 3);
    }
    if (zones == 0 || (mode == 0x26 && frequency == 0)) return block;
    block[0] = mode;
    block[1] = static_cast<std::uint8_t>(zones & 0xFF);
    block[2] = static_cast<std::uint8_t>(zones >> 8);
    for (std::size_t i = 0; i < 4; ++i) block[3 + i] = static_cast<std::uint8_t>((packed >> (8 * i)) & 0xFF);
    block[9] = frequency;
    return block;
}

/**
 * @brief Decodes one guest ScePadTriggerEffectCommandData into a request.
 *
 * Modes 1..6 are feedback, weapon, vibration, multi-position feedback, slope
 * feedback and multi-position vibration. Mode 0 turns the effect off. Payload
 * values outside an effect's documented range also yield "off" (never an
 * out-of-bounds read: only the 48 payload bytes are inspected).
 *
 * @param command Pointer to kTriggerCommandSize readable bytes.
 * @param out Receives the decoded request; untouched when the mode is invalid.
 * @return false when the mode exceeds kTriggerModeMax (struct was misread),
 *         true otherwise.
 */
inline bool DecodeTriggerCommand(const std::uint8_t* command, TriggerRequest& out) {
    const std::uint32_t mode = static_cast<std::uint32_t>(command[0]) | (static_cast<std::uint32_t>(command[1]) << 8) |
                               (static_cast<std::uint32_t>(command[2]) << 16) | (static_cast<std::uint32_t>(command[3]) << 24);
    if (mode > kTriggerModeMax) return false;
    const std::uint8_t* p = command + kTriggerCommandDataOffset;
    TriggerRequest req;
    req.mode = mode;
    const auto fromPosition = [&](std::uint8_t position, std::uint8_t strength) {
        std::array<std::uint8_t, 10> zones{};
        if (position <= 9 && strength >= 1 && strength <= 8) {
            for (std::size_t i = position; i < zones.size(); ++i) zones[i] = strength;
        }
        return zones;
    };
    switch (mode) {
        case 1: // feedback { position, strength }
            req.effect = EncodeTriggerZones(0x21, fromPosition(p[0], p[1]), 0);
            req.fallback = TriggerFallbackStrength(p[1]);
            break;
        case 2: // weapon { startPosition, endPosition, strength }
            if (p[0] >= 2 && p[0] <= 7 && p[1] > p[0] && p[1] <= 8 && p[2] >= 1 && p[2] <= 8) {
                const std::uint16_t zones = static_cast<std::uint16_t>((1u << p[0]) | (1u << p[1]));
                req.effect[0] = 0x25;
                req.effect[1] = static_cast<std::uint8_t>(zones & 0xFF);
                req.effect[2] = static_cast<std::uint8_t>(zones >> 8);
                req.effect[3] = static_cast<std::uint8_t>(p[2] - 1);
            }
            req.fallback = TriggerFallbackStrength(p[2]);
            break;
        case 3: // vibration { position, amplitude, frequency }
            req.effect = EncodeTriggerZones(0x26, fromPosition(p[0], p[1]), p[2]);
            req.fallback = p[2] == 0 ? 0 : TriggerFallbackStrength(p[1]);
            break;
        case 4: { // multi-position feedback { strength[10] }
            std::array<std::uint8_t, 10> strengths{};
            std::copy(p, p + 10, strengths.begin());
            req.effect = EncodeTriggerZones(0x21, strengths, 0);
            req.fallback = TriggerFallbackStrength(*std::max_element(strengths.begin(), strengths.end()));
            break;
        }
        case 5: { // slope feedback { startPosition, endPosition, startStrength, endStrength }
            std::array<std::uint8_t, 10> strengths{};
            if (p[0] <= 8 && p[1] > p[0] && p[1] <= 9 && p[2] >= 1 && p[2] <= 8 && p[3] >= 1 && p[3] <= 8) {
                const int dist = p[1] - p[0];
                for (int i = p[0]; i < 10; ++i) {
                    strengths[static_cast<std::size_t>(i)] =
                        i <= p[1] ? static_cast<std::uint8_t>(std::lround(p[2] + (p[3] - p[2]) * (i - p[0]) / static_cast<double>(dist))) : p[3];
                }
            }
            req.effect = EncodeTriggerZones(0x21, strengths, 0);
            req.fallback = TriggerFallbackStrength(std::max(p[2], p[3]));
            break;
        }
        case 6: { // multi-position vibration { frequency, amplitude[10] }
            std::array<std::uint8_t, 10> amplitudes{};
            std::copy(p + 1, p + 11, amplitudes.begin());
            req.effect = EncodeTriggerZones(0x26, amplitudes, p[0]);
            req.fallback = p[0] == 0 ? 0 : TriggerFallbackStrength(*std::max_element(amplitudes.begin(), amplitudes.end()));
            break;
        }
        default: break; // mode 0 = off
    }
    req.valid = req.effect[0] != kDs5TriggerOff;
    if (!req.valid) req.fallback = 0;
    out = req;
    return true;
}

/** Result of parsing a guest ScePadTriggerEffectParam. */
struct TriggerEffectUpdate {
    std::uint8_t mask = 0;                       ///< bit 0 = L2, bit 1 = R2
    std::array<TriggerRequest, 2> request{};     ///< decoded request per trigger (valid only if its mask bit is set)
};

/**
 * @brief Parses a guest ScePadTriggerEffectParam (u8 mask, 7 pad, 2 commands).
 *
 * @param bytes Guest buffer; must be at least `size` bytes.
 * @param size Readable size of `bytes`; below kTriggerEffectParamSize it is rejected.
 * @param out Receives the update on success.
 * @return true on success; false for a null/short buffer, mask bits above 1, or
 *         an out-of-range mode in a selected command (caller returns
 *         ORBIS_PAD_ERROR_INVALID_ARG).
 */
inline bool ParseTriggerEffectParam(const std::uint8_t* bytes, std::size_t size, TriggerEffectUpdate& out) {
    if (bytes == nullptr || size < kTriggerEffectParamSize) return false;
    const std::uint8_t mask = bytes[0];
    if ((mask & ~0x3u) != 0) return false;
    TriggerEffectUpdate update;
    update.mask = mask;
    for (std::size_t trigger = 0; trigger < 2; ++trigger) {
        // Only selected commands are decoded: the guest may leave the other
        // command uninitialised, and garbage there must not fail the call.
        if ((mask & (1u << trigger)) == 0) continue;
        if (!DecodeTriggerCommand(bytes + 8 + kTriggerCommandSize * trigger, update.request[trigger])) return false;
    }
    out = update;
    return true;
}

/**
 * @brief Builds the 47-byte DS5EffectsState payload for SDL_GameControllerSendEffect.
 *
 * Right trigger block at offset 10, left at offset 21 (SDL DS5EffectsState_t).
 * Only the trigger enable bits are set so SDL's own rumble/LED state is not
 * overwritten by this report.
 *
 * @param left L2 request.
 * @param right R2 request.
 * @return The payload.
 */
inline std::array<std::uint8_t, kDs5EffectsStateSize> BuildDs5TriggerEffects(const TriggerRequest& left, const TriggerRequest& right) {
    std::array<std::uint8_t, kDs5EffectsStateSize> report{};
    report[0] = kDs5EnableRightTrigger | kDs5EnableLeftTrigger;
    std::copy(right.effect.begin(), right.effect.end(), report.begin() + 10);
    std::copy(left.effect.begin(), left.effect.end(), report.begin() + 21);
    return report;
}

} // namespace Pad

#endif
