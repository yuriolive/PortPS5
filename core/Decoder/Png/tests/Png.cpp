// core/Decoder/Png/tests/Png.cpp
// GoogleTest suite for the shared PNG codec (Decoder/Png.hpp).
//
// All images are synthesised here; no game data is used
// (.agents/rules/legal-boundary.md). PNG is lossless, so round trips are
// compared byte for byte.

#include "Decoder/Png.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> MakePattern(std::uint32_t width, std::uint32_t height, std::uint32_t channels) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * channels);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i * 7 + i / 13);
    return pixels;
}

// Writes a big-endian u32 at `offset` (used to forge IHDR dimensions).
void PutBigEndian32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

// IHDR payload starts at signature(8) + length(4) + type(4) = 16.
constexpr std::size_t kIhdrData = 16;

}  // namespace

// Invariant: RGBA encode/decode is lossless and preserves geometry.
TEST(DecoderPng, RgbaRoundTripIsLossless) {
    const auto rgba = MakePattern(13, 9, 4);
    const auto png = Decoder::Png::Encode(rgba, 13, 9, 4);
    ASSERT_TRUE(png.has_value());
    const auto decoded = Decoder::Png::Decode(*png);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 13u);
    EXPECT_EQ(decoded->height, 9u);
    EXPECT_EQ(decoded->pixels, rgba);
}

// Invariant: Decode always expands to RGBA; RGB gets opaque alpha, gray is
// replicated into R=G=B, gray+alpha keeps its alpha.
TEST(DecoderPng, DecodeExpandsEveryChannelCountToRgba) {
    const std::vector<std::uint8_t> gray = {10, 200};
    const auto grayPng = Decoder::Png::Encode(gray, 2, 1, 1);
    ASSERT_TRUE(grayPng.has_value());
    auto decoded = Decoder::Png::Decode(*grayPng);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->pixels, (std::vector<std::uint8_t>{10, 10, 10, 255, 200, 200, 200, 255}));

    const std::vector<std::uint8_t> grayAlpha = {10, 50, 200, 60};
    const auto gaPng = Decoder::Png::Encode(grayAlpha, 2, 1, 2);
    ASSERT_TRUE(gaPng.has_value());
    decoded = Decoder::Png::Decode(*gaPng);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->pixels, (std::vector<std::uint8_t>{10, 10, 10, 50, 200, 200, 200, 60}));

    const std::vector<std::uint8_t> rgb = {1, 2, 3, 4, 5, 6};
    const auto rgbPng = Decoder::Png::Encode(rgb, 2, 1, 3);
    ASSERT_TRUE(rgbPng.has_value());
    decoded = Decoder::Png::Decode(*rgbPng);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->pixels, (std::vector<std::uint8_t>{1, 2, 3, 255, 4, 5, 6, 255}));
}

// Invariant: ParseHeader reports IHDR fields and the colour type of what the
// encoder wrote, without decoding pixels.
TEST(DecoderPng, ParseHeaderReportsEncoderOutput) {
    const auto rgb = MakePattern(5, 7, 3);
    const auto png = Decoder::Png::Encode(rgb, 5, 7, 3);
    ASSERT_TRUE(png.has_value());
    const auto header = Decoder::Png::ParseHeader(*png);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->width, 5u);
    EXPECT_EQ(header->height, 7u);
    EXPECT_EQ(header->bitDepth, 8);
    EXPECT_EQ(header->colorType, Decoder::Png::ColorType::Rgb);
    EXPECT_FALSE(header->interlaced);
    EXPECT_FALSE(header->hasTransparency);
}

// Invariant: ParseHeader rejects a bad signature, truncation, zero and
// over-range dimensions, and illegal depth/colour-type pairs.
TEST(DecoderPng, ParseHeaderRejectsMalformedInput) {
    const auto rgba = MakePattern(4, 4, 4);
    const auto good = Decoder::Png::Encode(rgba, 4, 4, 4);
    ASSERT_TRUE(good.has_value());
    ASSERT_TRUE(Decoder::Png::ParseHeader(*good).has_value());

    EXPECT_FALSE(Decoder::Png::ParseHeader({}));
    EXPECT_FALSE(Decoder::Png::ParseHeader({good->data(), 20}));  // truncated IHDR

    auto badSignature = *good;
    badSignature[1] = 'X';
    EXPECT_FALSE(Decoder::Png::ParseHeader(badSignature));

    auto zeroWidth = *good;
    PutBigEndian32(zeroWidth, kIhdrData, 0);
    EXPECT_FALSE(Decoder::Png::ParseHeader(zeroWidth));

    auto hugeHeight = *good;
    PutBigEndian32(hugeHeight, kIhdrData + 4, 0x80000000u);  // > 2^31 - 1
    EXPECT_FALSE(Decoder::Png::ParseHeader(hugeHeight));

    auto badPair = *good;
    badPair[kIhdrData + 8] = 4;   // bit depth 4 ...
    badPair[kIhdrData + 9] = 6;   // ... is illegal for RGBA
    EXPECT_FALSE(Decoder::Png::ParseHeader(badPair));

    auto badInterlace = *good;
    badInterlace[kIhdrData + 12] = 2;
    EXPECT_FALSE(Decoder::Png::ParseHeader(badInterlace));
}

// Invariant (overflow): a header that claims 2^31-1 x 2^31-1 pixels but has
// no pixel data parses fine (it is structurally valid) yet Decode must fail
// cleanly instead of trying to allocate ~16 EiB.
TEST(DecoderPng, DecodeOfForgedHugeDimensionsFailsCleanly) {
    const auto rgba = MakePattern(4, 4, 4);
    auto png = *Decoder::Png::Encode(rgba, 4, 4, 4);
    PutBigEndian32(png, kIhdrData, 0x7FFFFFFFu);
    PutBigEndian32(png, kIhdrData + 4, 0x7FFFFFFFu);
    const auto header = Decoder::Png::ParseHeader(png);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->width, 0x7FFFFFFFu);
    EXPECT_FALSE(Decoder::Png::Decode(png));
}

// Invariant: Decode returns nullopt for empty, garbage and truncated input.
TEST(DecoderPng, DecodeRejectsMalformedInput) {
    EXPECT_FALSE(Decoder::Png::Decode({}));
    const std::vector<std::uint8_t> garbage(64, 0x5A);
    EXPECT_FALSE(Decoder::Png::Decode(garbage));
    const auto rgba = MakePattern(16, 16, 4);
    const auto png = *Decoder::Png::Encode(rgba, 16, 16, 4);
    EXPECT_FALSE(Decoder::Png::Decode({png.data(), png.size() / 2}));
}

// Invariant: every invalid Encode argument is rejected with nullopt. The
// stride (width * channels) is an int inside stb, so a width whose stride
// exceeds INT_MAX must be refused even though the buffer is "large enough"
// in the caller's mind.
TEST(DecoderPng, EncodeRejectsInvalidArguments) {
    const auto rgba = MakePattern(4, 4, 4);
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 4, 4, 0));
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 4, 4, 5));
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 0, 4, 4));
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 4, 0, 4));
    EXPECT_FALSE(Decoder::Png::Encode({rgba.data(), rgba.size() - 1}, 4, 4, 4));
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 0x7FFFFFFFu, 1, 4));          // stride > INT_MAX
    EXPECT_FALSE(Decoder::Png::Encode(rgba, 0xFFFFFFFFu, 0xFFFFFFFFu, 4));  // size overflow
}
