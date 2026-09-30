// core/Decoder/Jpeg/tests/Jpeg.cpp
// GoogleTest suite for the shared JPEG codec (Decoder/Jpeg.hpp).
//
// All images are synthesised here (gradients); no game data or recorded
// frames are used (.agents/rules/legal-boundary.md). JPEG is lossy, so round
// trips are checked against an average-error bound, not bit equality.

#include "Decoder/Jpeg.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

// Smooth RGB gradient: compresses well and round-trips with a low error.
std::vector<std::uint8_t> MakeGradient(std::uint32_t width, std::uint32_t height, std::uint32_t channels) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * channels);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * channels];
            pixel[0] = static_cast<std::uint8_t>(x * 255 / (width - 1));
            if (channels == 3) {
                pixel[1] = static_cast<std::uint8_t>(y * 255 / (height - 1));
                pixel[2] = 128;
            }
        }
    }
    return pixels;
}

int AverageError(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual) {
    long total = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) total += std::abs(int{expected[i]} - int{actual[i]});
    return static_cast<int>(total / static_cast<long>(expected.size()));
}

}  // namespace

// Invariant: an RGB encode produces a JFIF stream (SOI ... EOI) that decodes
// back to the same geometry, 3 channels and a small average error at q=90.
TEST(DecoderJpeg, RgbRoundTrip) {
    const auto rgb = MakeGradient(32, 24, 3);
    const auto jpeg = Decoder::Jpeg::Encode(rgb, 32, 24, 3, 90);
    ASSERT_TRUE(jpeg.has_value());
    ASSERT_GT(jpeg->size(), 4u);
    EXPECT_EQ((*jpeg)[0], 0xFF);
    EXPECT_EQ((*jpeg)[1], 0xD8);
    EXPECT_EQ((*jpeg)[jpeg->size() - 2], 0xFF);
    EXPECT_EQ(jpeg->back(), 0xD9);

    const auto decoded = Decoder::Jpeg::Decode(*jpeg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 32u);
    EXPECT_EQ(decoded->height, 24u);
    EXPECT_EQ(decoded->channels, 3u);
    ASSERT_EQ(decoded->pixels.size(), rgb.size());
    EXPECT_LE(AverageError(rgb, decoded->pixels), 6);
}

// Invariant: single-channel input is accepted. stb's JPEG writer always emits
// a 3-component YCbCr stream (there is no 1-component mode), so the decoded
// image has 3 channels with R == G == B and values close to the source gray.
TEST(DecoderJpeg, GrayscaleInputRoundTripsAsNeutralRgb) {
    const auto gray = MakeGradient(40, 16, 1);
    const auto jpeg = Decoder::Jpeg::Encode(gray, 40, 16, 1, 90);
    ASSERT_TRUE(jpeg.has_value());
    const auto decoded = Decoder::Jpeg::Decode(*jpeg);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->channels, 3u);
    ASSERT_EQ(decoded->pixels.size(), gray.size() * 3);
    long total = 0;
    for (std::size_t i = 0; i < gray.size(); ++i) {
        EXPECT_NEAR(decoded->pixels[i * 3], decoded->pixels[i * 3 + 1], 2);
        EXPECT_NEAR(decoded->pixels[i * 3], decoded->pixels[i * 3 + 2], 2);
        total += std::abs(int{gray[i]} - int{decoded->pixels[i * 3]});
    }
    EXPECT_LE(total / static_cast<long>(gray.size()), 6);
}

// Invariant: lower quality never produces a larger stream than higher quality
// for the same content (monotone size/quality trade-off).
TEST(DecoderJpeg, LowerQualityIsNotLarger) {
    const auto rgb = MakeGradient(64, 64, 3);
    const auto high = Decoder::Jpeg::Encode(rgb, 64, 64, 3, 100);
    const auto low = Decoder::Jpeg::Encode(rgb, 64, 64, 3, 10);
    ASSERT_TRUE(high.has_value());
    ASSERT_TRUE(low.has_value());
    EXPECT_LE(low->size(), high->size());
}

// Invariant: every invalid Encode argument is rejected with nullopt (never a
// throw or a crash), including the 16-bit dimension cap and a short buffer.
TEST(DecoderJpeg, EncodeRejectsInvalidArguments) {
    const auto rgb = MakeGradient(8, 8, 3);
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 0, 8, 3, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 0, 3, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 0x10000, 8, 3, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 0x10000, 3, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 8, 2, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 8, 4, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 8, 3, 0));
    EXPECT_FALSE(Decoder::Jpeg::Encode(rgb, 8, 8, 3, 101));
    EXPECT_FALSE(Decoder::Jpeg::Encode({rgb.data(), rgb.size() - 1}, 8, 8, 3, 80));
}

// Invariant (overflow): maximal legal dimensions with a tiny buffer must be
// rejected by the 64-bit size check before stb ever reads the buffer.
TEST(DecoderJpeg, EncodeHugeDimensionsWithTinyBufferIsRejected) {
    const std::vector<std::uint8_t> tiny(16);
    EXPECT_FALSE(Decoder::Jpeg::Encode(tiny, 0xFFFF, 0xFFFF, 3, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(tiny, 0xFFFF, 0xFFFF, 1, 80));
    EXPECT_FALSE(Decoder::Jpeg::Encode(tiny, 0xFFFFFFFFu, 0xFFFFFFFFu, 3, 80));
}

// Invariant: Decode returns nullopt (not UB) for empty, garbage and truncated
// input.
TEST(DecoderJpeg, DecodeRejectsMalformedInput) {
    EXPECT_FALSE(Decoder::Jpeg::Decode({}));
    const std::vector<std::uint8_t> garbage(64, 0xAB);
    EXPECT_FALSE(Decoder::Jpeg::Decode(garbage));

    const auto rgb = MakeGradient(32, 32, 3);
    const auto jpeg = Decoder::Jpeg::Encode(rgb, 32, 32, 3, 90);
    ASSERT_TRUE(jpeg.has_value());
    // Only the SOI marker survives: no frame header, so nothing to decode.
    EXPECT_FALSE(Decoder::Jpeg::Decode({jpeg->data(), 2}));
}
