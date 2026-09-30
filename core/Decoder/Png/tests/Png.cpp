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

// ---- Hand-built PNG construction (valid CRCs, stored-deflate IDAT) ----------
// Reaches decoder paths the stb-based encoder cannot produce (16-bit,
// interlaced, palette + tRNS, CRC-valid forged headers). Layouts follow the
// public PNG specification; the corner-case classes mirror Willem van Schaik's
// PngSuite (basi*, tbb*, xc*, xcs*, xhd*, xdt*), whose licence permits free
// redistribution, but no PngSuite file is copied into the repo.

std::uint32_t Crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void AppendBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}

void AppendChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    AppendBE32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    AppendBE32(out, Crc32(body.data(), body.size()));
}

// zlib stream holding a single stored (uncompressed) deflate block.
std::vector<std::uint8_t> StoredZlib(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z = {0x78, 0x01, 0x01};
    const auto n = static_cast<std::uint16_t>(raw.size());
    z.push_back(n & 0xFF);
    z.push_back(n >> 8);
    z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
    z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    AppendBE32(z, b << 16 | a);
    return z;
}

struct PngSpec {
    std::uint32_t width = 1, height = 1;
    std::uint8_t bitDepth = 8, colorType = 0, interlace = 0;
    std::vector<std::uint8_t> raw;  // filtered scanlines (filter byte included)
    std::vector<std::uint8_t> plte, trns;
    bool addIend = true;
};

std::vector<std::uint8_t> BuildPng(const PngSpec& spec) {
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    AppendBE32(ihdr, spec.width);
    AppendBE32(ihdr, spec.height);
    ihdr.insert(ihdr.end(), {spec.bitDepth, spec.colorType, 0, 0, spec.interlace});
    AppendChunk(out, "IHDR", ihdr);
    if (!spec.plte.empty()) AppendChunk(out, "PLTE", spec.plte);
    if (!spec.trns.empty()) AppendChunk(out, "tRNS", spec.trns);
    AppendChunk(out, "IDAT", StoredZlib(spec.raw));
    if (spec.addIend) AppendChunk(out, "IEND", {});
    return out;
}

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

// Invariant: a bad CRC in a critical chunk (IHDR, IDAT) is rejected even
// though stb itself would accept it (PngSuite xhd*/xcs*), while the same file
// with valid CRCs decodes.
TEST(DecoderPng, CriticalChunkCrcErrorsAreRejected) {
    PngSpec spec;
    spec.raw = {0, 0x55};
    const auto good = BuildPng(spec);
    ASSERT_TRUE(Decoder::Png::Decode(good).has_value());

    auto badIhdrCrc = good;
    badIhdrCrc[8 + 8 + 13] ^= 0xFF;  // first CRC byte of IHDR
    EXPECT_FALSE(Decoder::Png::Decode(badIhdrCrc));

    auto badIdatData = good;
    badIdatData[8 + 25 + 8 + 4] ^= 0x01;  // inside the IDAT payload
    EXPECT_FALSE(Decoder::Png::Decode(badIdatData));
}

// Invariant: a bad CRC in an ancillary chunk is ignored (the PNG spec lets
// decoders do so), so the image still decodes.
TEST(DecoderPng, AncillaryChunkCrcErrorIsIgnored) {
    PngSpec spec;
    spec.raw = {0, 0x55};
    auto png = BuildPng(spec);
    std::vector<std::uint8_t> text;
    AppendChunk(text, "tEXt", {'k', 0, 'v'});
    text.back() ^= 0xFF;  // corrupt the tEXt CRC
    png.insert(png.begin() + 8 + 25, text.begin(), text.end());
    EXPECT_TRUE(Decoder::Png::Decode(png).has_value());
}

// Invariant: a stream without IEND (PngSuite xdt*/truncation class) and a
// chunk whose length field points past the end are rejected without reading
// out of bounds.
TEST(DecoderPng, MissingIendAndOversizedChunkLengthAreRejected) {
    PngSpec spec;
    spec.raw = {0, 0x55};
    spec.addIend = false;
    EXPECT_FALSE(Decoder::Png::Decode(BuildPng(spec)));

    spec.addIend = true;
    auto forged = BuildPng(spec);
    for (int i = 0; i < 4; ++i) forged[8 + 25 + i] = 0xFF;  // IDAT length = 0xFFFFFFFF
    EXPECT_FALSE(Decoder::Png::Decode(forged));
}

// Invariant (alloc bomb): a structurally perfect file (valid CRCs) claiming
// 2^31-1 x 2^31-1 pixels is refused by stb's size cap without allocating;
// ParseHeader still reports the claimed size.
TEST(DecoderPng, CrcValidHugeDimensionsAreRejectedWithoutAllocation) {
    PngSpec spec;
    spec.width = 0x7FFFFFFF;
    spec.height = 0x7FFFFFFF;
    spec.raw = {0, 0};
    const auto png = BuildPng(spec);
    const auto header = Decoder::Png::ParseHeader(png);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->width, 0x7FFFFFFFu);
    EXPECT_FALSE(Decoder::Png::Decode(png));
}

// Invariant: an illegal colour type (PngSuite xc1/xc9) fails ParseHeader and
// Decode.
TEST(DecoderPng, IllegalColorTypeIsRejected) {
    PngSpec spec;
    spec.colorType = 1;
    spec.raw = {0, 0};
    const auto png = BuildPng(spec);
    EXPECT_FALSE(Decoder::Png::ParseHeader(png));
    EXPECT_FALSE(Decoder::Png::Decode(png));
    spec.colorType = 9;
    EXPECT_FALSE(Decoder::Png::Decode(BuildPng(spec)));
}

// Invariant (documented behaviour): 16-bit gray and Adam7 interlacing both
// decode; 16-bit samples reduce to their high byte, alpha becomes 255, and the
// header reports depth 16 plus the interlace flag.
TEST(DecoderPng, SixteenBitInterlacedGrayDecodesToHighByte) {
    PngSpec spec;
    spec.bitDepth = 16;
    spec.interlace = 1;
    spec.raw = {0, 0xAB, 0xCD};  // 1x1 Adam7: a single pass-1 scanline
    const auto png = BuildPng(spec);
    const auto header = Decoder::Png::ParseHeader(png);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->bitDepth, 16);
    EXPECT_TRUE(header->interlaced);
    const auto image = Decoder::Png::Decode(png);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->pixels, (std::vector<std::uint8_t>{0xAB, 0xAB, 0xAB, 0xFF}));
}

// Invariant (documented behaviour): a 1-bit palette image with a short tRNS
// table expands through PLTE; entries beyond the tRNS table are opaque, and
// the header flags the tRNS chunk (PngSuite tbb*/basn3p01 class).
TEST(DecoderPng, PaletteWithShortTrnsExpandsToRgba) {
    PngSpec spec;
    spec.width = 2;
    spec.colorType = 3;
    spec.bitDepth = 1;
    spec.raw = {0, 0x40};  // pixels: index 0, index 1
    spec.plte = {255, 0, 0, 0, 0, 255};
    spec.trns = {128};
    const auto png = BuildPng(spec);
    const auto header = Decoder::Png::ParseHeader(png);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->colorType, Decoder::Png::ColorType::Palette);
    EXPECT_TRUE(header->hasTransparency);
    const auto image = Decoder::Png::Decode(png);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->pixels, (std::vector<std::uint8_t>{255, 0, 0, 128, 0, 0, 255, 255}));
}
