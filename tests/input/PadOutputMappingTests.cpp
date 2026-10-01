// Unit tests: guest pad output -> DualSense translation (docs/spec/input.md §Output calls).
//
// Pins the pure encoders in PadOutputMapping.hpp on synthetic buffers: rumble
// amplitude scaling, the DualSense trigger effect blocks for every
// ScePadTriggerEffectMode, guest ScePadTriggerEffectParam parsing (including
// short and malformed buffers) and the SDL DS5 effects report offsets. Expected
// bytes were derived by hand from the documented block layout, not from the
// implementation. No SDL device, guest memory or game data is used.
// Behaviour oracle: AnyPS5 main@4c349efe / 85517679 (GPL-2.0).
#include "prx/libScePad/include/PadOutputMapping.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

using Block = std::array<std::uint8_t, Pad::kTriggerEffectBlockSize>;

// Builds a 56-byte guest command: little-endian mode, 4 padding bytes, payload.
std::array<std::uint8_t, Pad::kTriggerCommandSize> Command(std::uint32_t mode, std::initializer_list<std::uint8_t> payload) {
    std::array<std::uint8_t, Pad::kTriggerCommandSize> c{};
    for (std::size_t i = 0; i < 4; ++i) c[i] = static_cast<std::uint8_t>((mode >> (8 * i)) & 0xFF);
    std::size_t at = Pad::kTriggerCommandDataOffset;
    for (const std::uint8_t b : payload) c[at++] = b;
    return c;
}

} // namespace

// Invariant: rumble amplitudes scale by exactly 257 so 255 reaches SDL's full
// 65535 and 0 stays silent; the large motor is SDL's low-frequency motor.
TEST(PadOutputMapping, RumbleScalesToSdlRange) {
    const Pad::SdlRumble full = Pad::RumbleToSdl(255, 255);
    EXPECT_EQ(full.lowFrequency, 65535);
    EXPECT_EQ(full.highFrequency, 65535);
    const Pad::SdlRumble mixed = Pad::RumbleToSdl(1, 0);
    EXPECT_EQ(mixed.lowFrequency, 257);
    EXPECT_EQ(mixed.highFrequency, 0);
    const Pad::SdlRumble small = Pad::RumbleToSdl(0, 128);
    EXPECT_EQ(small.lowFrequency, 0);
    EXPECT_EQ(small.highFrequency, 128 * 257);
}

// Invariant: the trigger rumble fallback is 32 per guest strength step, off for
// 0 and saturating at 255 for 8 and above (never wrapping the uint8).
TEST(PadOutputMapping, TriggerFallbackStrengthSaturates) {
    EXPECT_EQ(Pad::TriggerFallbackStrength(0), 0);
    EXPECT_EQ(Pad::TriggerFallbackStrength(1), 32);
    EXPECT_EQ(Pad::TriggerFallbackStrength(7), 224);
    EXPECT_EQ(Pad::TriggerFallbackStrength(8), 255);
    EXPECT_EQ(Pad::TriggerFallbackStrength(200), 255);
}

// Invariant: zone encoding writes mode, a little-endian zone mask, a 3-bit
// strength-1 field per zone and the frequency; strengths of 0 or above 8 never
// activate a zone, and a vibration block with frequency 0 collapses to "off".
TEST(PadOutputMapping, EncodeTriggerZonesLayout) {
    const Block feedback = Pad::EncodeTriggerZones(0x21, {0, 0, 0, 0, 8, 8, 8, 8, 8, 8}, 0);
    EXPECT_EQ(feedback, (Block{0x21, 0xF0, 0x03, 0x00, 0xF0, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00}));

    const Block vibration = Pad::EncodeTriggerZones(0x26, {0, 0, 3, 3, 3, 3, 3, 3, 3, 3}, 40);
    EXPECT_EQ(vibration, (Block{0x26, 0xFC, 0x03, 0x80, 0x24, 0x49, 0x12, 0x00, 0x00, 0x28, 0x00}));

    // Out-of-range strengths (0 and 9) are inactive: only zone 1 (strength 2) stays.
    const Block filtered = Pad::EncodeTriggerZones(0x21, {0, 2, 9, 0, 0, 0, 0, 0, 0, 0}, 0);
    EXPECT_EQ(filtered[1], 0x02);
    EXPECT_EQ(filtered[3], 0x08); // (2 - 1) << (3 * 1)

    EXPECT_EQ(Pad::EncodeTriggerZones(0x26, {0, 0, 3, 3, 3, 3, 3, 3, 3, 3}, 0)[0], Pad::kDs5TriggerOff);
    EXPECT_EQ(Pad::EncodeTriggerZones(0x21, {}, 0)[0], Pad::kDs5TriggerOff);
}

// Invariant: mode 1 (feedback) engages every zone from the position onward at
// the requested strength; a position above 9 or strength outside 1..8 is "off".
TEST(PadOutputMapping, DecodeFeedback) {
    Pad::TriggerRequest req;
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(1, {4, 8}).data(), req));
    EXPECT_TRUE(req.valid);
    EXPECT_EQ(req.mode, 1u);
    EXPECT_EQ(req.effect, (Block{0x21, 0xF0, 0x03, 0x00, 0xF0, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(req.fallback, 255);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(1, {4, 0}).data(), req));
    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.effect[0], Pad::kDs5TriggerOff);
    EXPECT_EQ(req.fallback, 0);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(1, {10, 5}).data(), req));
    EXPECT_FALSE(req.valid);
}

// Invariant: mode 2 (weapon) marks the start and end zones in the mask and
// stores strength-1; start outside 2..7, end <= start or > 8 is "off".
TEST(PadOutputMapping, DecodeWeapon) {
    Pad::TriggerRequest req;
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(2, {2, 6, 5}).data(), req));
    EXPECT_TRUE(req.valid);
    EXPECT_EQ(req.effect[0], 0x25);
    EXPECT_EQ(req.effect[1], 0x44); // zones 2 and 6
    EXPECT_EQ(req.effect[2], 0x00);
    EXPECT_EQ(req.effect[3], 4);    // strength 5 - 1
    EXPECT_EQ(req.fallback, 160);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(2, {1, 6, 5}).data(), req));
    EXPECT_FALSE(req.valid);
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(2, {6, 6, 5}).data(), req));
    EXPECT_FALSE(req.valid);
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(2, {3, 9, 5}).data(), req));
    EXPECT_FALSE(req.valid);
}

// Invariant: mode 3 (vibration) uses mode byte 0x26 with the frequency in byte
// 9; frequency 0 turns it off even with a valid amplitude.
TEST(PadOutputMapping, DecodeVibration) {
    Pad::TriggerRequest req;
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(3, {2, 3, 40}).data(), req));
    EXPECT_TRUE(req.valid);
    EXPECT_EQ(req.effect, (Block{0x26, 0xFC, 0x03, 0x80, 0x24, 0x49, 0x12, 0x00, 0x00, 0x28, 0x00}));
    EXPECT_EQ(req.fallback, 96);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(3, {2, 3, 0}).data(), req));
    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.fallback, 0);
}

// Invariant: modes 4, 5 and 6 spread per-zone data correctly: multi-position
// feedback copies ten strengths, slope feedback interpolates linearly between
// start and end then holds the end strength, multi-position vibration reads the
// frequency first and ten amplitudes after it.
TEST(PadOutputMapping, DecodeMultiPositionAndSlope) {
    Pad::TriggerRequest req;
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(4, {1, 2, 3, 4, 5, 6, 7, 8, 0, 0}).data(), req));
    EXPECT_EQ(req.effect, (Block{0x21, 0xFF, 0x00, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(req.fallback, 255);

    // Slope zones 2..6 rise 1,2,3,4,5 then hold 5: strengths {0,0,1,2,3,4,5,5,5,5}.
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(5, {2, 6, 1, 5}).data(), req));
    EXPECT_EQ(req.effect, (Block{0x21, 0xFC, 0x03, 0x00, 0xA2, 0x91, 0x24, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(req.fallback, 160);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(6, {40, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3}).data(), req));
    EXPECT_EQ(req.effect, (Block{0x26, 0xFC, 0x03, 0x80, 0x24, 0x49, 0x12, 0x00, 0x00, 0x28, 0x00}));
    EXPECT_EQ(req.fallback, 96);

    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(6, {0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}).data(), req));
    EXPECT_FALSE(req.valid);
}

// Invariant: mode 0 is a valid "off" request and any mode above 6 is rejected
// without touching the output request (a misread struct, not an effect).
TEST(PadOutputMapping, DecodeOffAndInvalidMode) {
    Pad::TriggerRequest req;
    req.fallback = 77;
    ASSERT_TRUE(Pad::DecodeTriggerCommand(Command(0, {1, 1, 1}).data(), req));
    EXPECT_FALSE(req.valid);
    EXPECT_EQ(req.fallback, 0);

    req.fallback = 77;
    EXPECT_FALSE(Pad::DecodeTriggerCommand(Command(7, {1, 1, 1}).data(), req));
    EXPECT_EQ(req.fallback, 77);
    // A high mode byte in an upper position also exceeds the maximum (little-endian u32).
    EXPECT_FALSE(Pad::DecodeTriggerCommand(Command(0x100, {}).data(), req));
}

// Invariant: ParseTriggerEffectParam rejects null and short buffers (so the
// export can never read past what the guest provided), mask bits above 1, and a
// bad mode in a selected command; it ignores garbage in an unselected command.
TEST(PadOutputMapping, ParseTriggerEffectParamValidation) {
    std::vector<std::uint8_t> param(Pad::kTriggerEffectParamSize, 0);
    const auto put = [&](std::size_t trigger, const std::array<std::uint8_t, Pad::kTriggerCommandSize>& c) {
        std::copy(c.begin(), c.end(), param.begin() + 8 + trigger * Pad::kTriggerCommandSize);
    };
    Pad::TriggerEffectUpdate update;

    EXPECT_FALSE(Pad::ParseTriggerEffectParam(nullptr, Pad::kTriggerEffectParamSize, update));
    EXPECT_FALSE(Pad::ParseTriggerEffectParam(param.data(), Pad::kTriggerEffectParamSize - 1, update));
    EXPECT_FALSE(Pad::ParseTriggerEffectParam(param.data(), 0, update));

    param[0] = 0x04; // unknown trigger bit
    EXPECT_FALSE(Pad::ParseTriggerEffectParam(param.data(), param.size(), update));

    param[0] = 0x02; // R2 only
    put(0, Command(0xFFFFFFFFu, {})); // garbage in the unselected L2 command
    put(1, Command(1, {0, 8}));
    ASSERT_TRUE(Pad::ParseTriggerEffectParam(param.data(), param.size(), update));
    EXPECT_EQ(update.mask, 0x02);
    EXPECT_TRUE(update.request[1].valid);
    EXPECT_FALSE(update.request[0].valid);

    param[0] = 0x03; // both selected: the garbage L2 mode is now an error
    EXPECT_FALSE(Pad::ParseTriggerEffectParam(param.data(), param.size(), update));
}

// Invariant: the SDL DS5EffectsState payload is 47 bytes, enables only the two
// trigger bits (so SDL's own rumble and LED state is untouched), and puts the
// right trigger block at offset 10 and the left at offset 21.
TEST(PadOutputMapping, Ds5TriggerReportOffsets) {
    Pad::TriggerRequest left;
    Pad::TriggerRequest right;
    left.effect = Block{0xA1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    right.effect = Block{0xB1, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    const auto report = Pad::BuildDs5TriggerEffects(left, right);
    ASSERT_EQ(report.size(), 47u);
    EXPECT_EQ(report[0], 0x0C);
    for (std::size_t i = 1; i < 10; ++i) EXPECT_EQ(report[i], 0) << "byte " << i;
    EXPECT_EQ(report[10], 0xB1);
    EXPECT_EQ(report[20], 20);
    EXPECT_EQ(report[21], 0xA1);
    EXPECT_EQ(report[31], 10);
    for (std::size_t i = 32; i < report.size(); ++i) EXPECT_EQ(report[i], 0) << "byte " << i;
}
