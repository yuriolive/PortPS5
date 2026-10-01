// tests/video/H264DecoderTests.cpp
// GoogleTest suite for Videodec2::H264Decoder, the FFmpeg wrapper behind libSceVideodec2. All input
// is synthetic: Baseline-profile Annex B streams built in H264TestStream.hpp from I_PCM macroblocks
// (lossless, so decoded planes must equal the generated planes exactly) and P_Skip frames. The suite
// also asserts the linked FFmpeg reports the plain LGPL-2.1+ licence that the GPL-2.0-only
// distribution depends on. Tests skip, loudly, when the host decoder is not built; the ci preset
// requires FFmpeg so the skips cannot hide there.

#include "H264Decoder.hpp"
#include "H264TestStream.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace H264Test;
using Videodec2::AccessUnitInfo;
using Videodec2::DecodeStatus;
using Videodec2::H264Decoder;
using Videodec2::Picture;

#define REQUIRE_HOST_DECODER()                                                           \
    do {                                                                                 \
        if (!Videodec2::H264DecoderAvailable()) GTEST_SKIP() << "FFmpeg is not built";   \
    } while (0)

/** Collects every picture from Decode (after each unit) and from Drain at the end. */
std::vector<Picture> DecodeAll(H264Decoder& decoder, const std::vector<std::vector<std::uint8_t>>& units, const std::vector<AccessUnitInfo>& infos) {
    std::vector<Picture> pictures;
    for (std::size_t i = 0; i < units.size(); ++i) {
        EXPECT_EQ(decoder.Decode(units[i].data(), units[i].size(), infos[i]), DecodeStatus::Ok) << "unit " << i;
        Picture p;
        while (decoder.PopPicture(p)) pictures.push_back(p);
    }
    decoder.Drain();
    Picture p;
    while (decoder.PopPicture(p)) pictures.push_back(p);
    return pictures;
}

/** Expects `picture` to hold exactly the visible part of `planes` in NV12 form. */
void ExpectMatches(const Picture& picture, const Planes& planes, const Geometry& g) {
    ASSERT_EQ(picture.width, g.Width());
    ASSERT_EQ(picture.height, g.Height());
    ASSERT_EQ(picture.luma.size(), static_cast<std::size_t>(picture.width) * picture.height);
    for (unsigned r = 0; r < picture.height; ++r)
        for (unsigned c = 0; c < picture.width; ++c)
            ASSERT_EQ(picture.luma[r * picture.width + c], planes.y[r * planes.width + c]) << "luma " << c << "," << r;
    const unsigned chromaRows = (picture.height + 1) / 2;
    const unsigned chromaWidth = (picture.width + 1) / 2;
    ASSERT_EQ(picture.chroma.size(), static_cast<std::size_t>(chromaWidth) * 2 * chromaRows);
    for (unsigned r = 0; r < chromaRows; ++r)
        for (unsigned c = 0; c < chromaWidth; ++c) {
            ASSERT_EQ(picture.chroma[r * chromaWidth * 2 + 2 * c], planes.cb[r * (planes.width / 2) + c]) << "cb " << c << "," << r;
            ASSERT_EQ(picture.chroma[r * chromaWidth * 2 + 2 * c + 1], planes.cr[r * (planes.width / 2) + c]) << "cr " << c << "," << r;
        }
}

// The linked FFmpeg must be a plain LGPL-2.1+ build. A GPL (--enable-gpl), LGPLv3 (--enable-version3)
// or non-free build changes the licence of the whole binary and is incompatible with GPL-2.0-only.
TEST(H264DecoderTest, LinkedFfmpegIsPlainLgpl21OrLater) {
    REQUIRE_HOST_DECODER();
    EXPECT_STREQ(Videodec2::H264DecoderLicense(), "LGPL version 2.1 or later");
}

// An IDR access unit of I_PCM macroblocks decodes to exactly the generated planes, with the caller's
// metadata, keyframe flag and Baseline profile / level 1.0 reported.
TEST(H264DecoderTest, IdrPcmDecodesBitExactly) {
    REQUIRE_HOST_DECODER();
    const Geometry g;
    const Planes planes = Pattern(g, 1);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, planes)});
    const auto pictures = DecodeAll(*decoder, {au}, {AccessUnitInfo{1000, 900, 0xABCD}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], planes, g);
    EXPECT_TRUE(pictures[0].keyFrame);
    EXPECT_FALSE(pictures[0].corrupt);
    EXPECT_EQ(pictures[0].info.pts, 1000u);
    EXPECT_EQ(pictures[0].info.dts, 900u);
    EXPECT_EQ(pictures[0].info.attached, 0xABCDu);
    EXPECT_EQ(pictures[0].profile, 66u);
    EXPECT_EQ(pictures[0].level, 10u);
}

// P_Skip frames repeat the previous picture; each output carries the metadata of its own access
// unit (pts/dts/attached travel through the decoder), in order, and only the first is a keyframe.
TEST(H264DecoderTest, SkipFramesRepeatPictureAndCarryMetadata) {
    REQUIRE_HOST_DECODER();
    const Geometry g;
    const Planes planes = Pattern(g, 2);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    const std::vector<std::vector<std::uint8_t>> units = {Join({Sps(g), Pps(), IdrPcm(g, planes)}), PSkip(g, 1), PSkip(g, 2), PSkip(g, 3)};
    const std::vector<AccessUnitInfo> infos = {{10, 10, 1}, {20, 20, 2}, {30, 30, 3}, {40, 40, 4}};
    const auto pictures = DecodeAll(*decoder, units, infos);
    ASSERT_EQ(pictures.size(), 4u);
    for (std::size_t i = 0; i < pictures.size(); ++i) {
        SCOPED_TRACE(i);
        ExpectMatches(pictures[i], planes, g);
        EXPECT_EQ(pictures[i].info.pts, infos[i].pts);
        EXPECT_EQ(pictures[i].info.attached, infos[i].attached);
        EXPECT_EQ(pictures[i].keyFrame, i == 0);
    }
}

// Cropping is applied by the decoder: a 32x32 grid cropped to 30x28 yields 30x28 pictures whose
// planes are the top-left part of the generated ones.
TEST(H264DecoderTest, CroppedPictureHasVisibleSize) {
    REQUIRE_HOST_DECODER();
    Geometry g;
    g.cropRight = 1;   // 2 luma samples
    g.cropBottom = 2;  // 4 luma rows
    ASSERT_EQ(g.Width(), 30u);
    ASSERT_EQ(g.Height(), 28u);
    const Planes planes = Pattern(g, 3);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    const auto pictures = DecodeAll(*decoder, {Join({Sps(g), Pps(), IdrPcm(g, planes)})}, {AccessUnitInfo{}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], planes, g);
}

// Bytes that are not an Annex B access unit are rejected, never guessed at: null, empty, a
// length-prefixed (AVCC) unit and a unit that does not start with a start code.
TEST(H264DecoderTest, RejectsNonAnnexBInput) {
    REQUIRE_HOST_DECODER();
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    const std::uint8_t avcc[] = {0, 0, 0, 5, 0x67, 1, 2, 3, 4};
    const std::uint8_t noStart[] = {0x67, 0x42, 0x00, 0x0a, 0x00};
    EXPECT_EQ(decoder->Decode(nullptr, 4, {}), DecodeStatus::BadAccessUnit);
    EXPECT_EQ(decoder->Decode(avcc, 0, {}), DecodeStatus::BadAccessUnit);
    EXPECT_EQ(decoder->Decode(avcc, sizeof(avcc), {}), DecodeStatus::BadAccessUnit);
    EXPECT_EQ(decoder->Decode(noStart, sizeof(noStart), {}), DecodeStatus::BadAccessUnit);
    Picture p;
    EXPECT_FALSE(decoder->PopPicture(p));
}

// Garbage after a valid start code must not crash or fabricate a picture: whatever status it
// gets, no picture is produced, and the decoder still decodes a real stream afterwards.
TEST(H264DecoderTest, GarbageAnnexBYieldsNoPictureAndDecoderRecovers) {
    REQUIRE_HOST_DECODER();
    const Geometry g;
    const Planes planes = Pattern(g, 4);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    std::vector<std::uint8_t> garbage = {0, 0, 0, 1, 0x65};
    for (int i = 0; i < 64; ++i) garbage.push_back(static_cast<std::uint8_t>(0x5A ^ (i * 37)));
    (void)decoder->Decode(garbage.data(), garbage.size(), {});
    Picture p;
    EXPECT_FALSE(decoder->PopPicture(p));
    const auto pictures = DecodeAll(*decoder, {Join({Sps(g), Pps(), IdrPcm(g, planes)})}, {AccessUnitInfo{}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], planes, g);
}

// After Drain (end of stream) the decoder accepts a new stream without being recreated: a second
// IDR decodes to its own, different planes (the end-of-stream state is cleared before new input).
TEST(H264DecoderTest, DecodesAgainAfterDrain) {
    REQUIRE_HOST_DECODER();
    const Geometry g;
    const Planes first = Pattern(g, 5);
    const Planes second = Pattern(g, 6);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    auto pictures = DecodeAll(*decoder, {Join({Sps(g), Pps(), IdrPcm(g, first)})}, {AccessUnitInfo{1, 1, 1}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], first, g);
    pictures = DecodeAll(*decoder, {Join({Sps(g), Pps(), IdrPcm(g, second)})}, {AccessUnitInfo{2, 2, 2}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], second, g);
    EXPECT_EQ(pictures[0].info.pts, 2u);
}

// Reset drops queued pictures: after it nothing is ready, and a fresh IDR decodes normally.
TEST(H264DecoderTest, ResetDropsQueuedPictures) {
    REQUIRE_HOST_DECODER();
    const Geometry g;
    const Planes planes = Pattern(g, 7);
    auto decoder = H264Decoder::Create();
    ASSERT_TRUE(decoder);
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, planes)});
    ASSERT_EQ(decoder->Decode(au.data(), au.size(), {}), DecodeStatus::Ok);
    decoder->Drain();  // Make any held picture ready, then discard it.
    decoder->Reset();
    Picture p;
    EXPECT_FALSE(decoder->PopPicture(p));
    const auto pictures = DecodeAll(*decoder, {au}, {AccessUnitInfo{}});
    ASSERT_EQ(pictures.size(), 1u);
    ExpectMatches(pictures[0], planes, g);
}

// The stream builder itself is the test oracle, so check it against known-good bytes: the SPS for a
// 2x2 macroblock Baseline stream. Failing here means the generator, not the decoder, is wrong.
TEST(H264StreamBuilderTest, SpsMatchesHandEncodedBytes) {
    const auto sps = Sps(Geometry{});
    // Start code, NAL header 0x67, then profile 66, constraints 0, level 10 and the Exp-Golomb fields
    // 1 1 1 1 010 0 010 010 1 0 0 0 + stop bit, encoded independently of the builder (see test notes).
    const std::vector<std::uint8_t> expected = {0, 0, 0, 1, 0x67, 0x42, 0x00, 0x0a, 0xF4, 0x4A, 0x20};
    EXPECT_EQ(sps, expected) << "got " << sps.size() << " bytes";
}

}  // namespace
