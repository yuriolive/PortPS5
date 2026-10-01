// core/libs/tests/GuestJpegEnc.cpp
// GoogleTest suite for libSceJpegEnc (core/libs/prx/libSceJpegEnc/Export.cpp).
//
// Calls the exports exactly as a guest would (System V ABI, raw pointers) and
// checks SCE return codes, handle lifecycle, encode output, overflow edges
// and the abort path for unsupported modes. Images are synthesised here; no
// game data is used (.agents/rules/legal-boundary.md).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Decoder/Jpeg.hpp"
#include "GuestTestPages.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI sceJpegEncQueryMemorySize(const JpegEncCreateParam*) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI sceJpegEncCreate(const JpegEncCreateParam*, void*, std::uint32_t, void**) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI sceJpegEncDelete(void*) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI sceJpegEncEncode(void*, const JpegEncEncodeParam*, JpegEncOutputInfo*) noexcept;
}

namespace {

constexpr std::int32_t kInvalidAddr = static_cast<std::int32_t>(0x80650101);
constexpr std::int32_t kInvalidSize = static_cast<std::int32_t>(0x80650102);
constexpr std::int32_t kInvalidParam = static_cast<std::int32_t>(0x80650103);
constexpr std::int32_t kInvalidHandle = static_cast<std::int32_t>(0x80650104);

// Fixture: a live encoder handle plus a 16x16 RGBA gradient and output buffer.
class JpegEncTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                unsigned char* px = &image_[(y * 16 + x) * 4];
                px[0] = static_cast<unsigned char>(x * 16);
                px[1] = static_cast<unsigned char>(y * 16);
                px[2] = 100;
                px[3] = 255;
            }
        }
        const JpegEncCreateParam create{sizeof(JpegEncCreateParam), 0};
        ASSERT_EQ(sceJpegEncCreate(&create, work_ + 1, 0x800, &handle_), 0);
    }

    void TearDown() override { sceJpegEncDelete(handle_); }

    JpegEncEncodeParam ValidParam() {
        JpegEncEncodeParam p{};
        p.image = image_;
        p.jpeg = jpeg_;
        p.image_size = sizeof(image_);
        p.jpeg_size = sizeof(jpeg_);
        p.image_width = 16;
        p.image_height = 16;
        p.image_pitch = 16 * 4;
        p.pixel_format = 0;  // R8G8B8A8
        p.encode_mode = 0;
        p.color_space = 1;   // YCC
        p.sampling_type = 2; // 4:2:0
        p.compression_ratio = 80;
        p.restart_interval = 0;
        return p;
    }

    // Returns the encode result for ValidParam() modified by `change`.
    template <typename F>
    std::int32_t EncodeWith(F change) {
        JpegEncEncodeParam p = ValidParam();
        change(p);
        return sceJpegEncEncode(handle_, &p, &info_);
    }

    alignas(4) unsigned char image_[16 * 16 * 4]{};
    unsigned char jpeg_[8192]{};
    alignas(32) unsigned char work_[0x800 + 64]{};
    void* handle_ = nullptr;
    JpegEncOutputInfo info_{};
};

}  // namespace

// Invariant: QueryMemorySize validates its param and otherwise reports the
// fixed 0x800-byte work size.
TEST(JpegEncLifecycle, QueryMemorySize) {
    const JpegEncCreateParam ok{sizeof(JpegEncCreateParam), 0};
    EXPECT_EQ(sceJpegEncQueryMemorySize(&ok), 0x800);
    EXPECT_EQ(sceJpegEncQueryMemorySize(nullptr), kInvalidAddr);
    const JpegEncCreateParam badSize{sizeof(JpegEncCreateParam) - 1, 0};
    EXPECT_EQ(sceJpegEncQueryMemorySize(&badSize), kInvalidSize);
    const JpegEncCreateParam badAttr{sizeof(JpegEncCreateParam), 1};
    EXPECT_EQ(sceJpegEncQueryMemorySize(&badAttr), kInvalidParam);
}

// Invariant: Create rejects each bad argument with its own code and leaves
// *handle untouched; a good Create aligns the handle to 32 bytes inside the
// supplied (deliberately misaligned) work memory; Delete is single-shot and
// rejects null, misaligned and forged handles.
TEST(JpegEncLifecycle, CreateDeleteContract) {
    const JpegEncCreateParam ok{sizeof(JpegEncCreateParam), 0};
    const JpegEncCreateParam badSize{sizeof(JpegEncCreateParam) - 1, 0};
    const JpegEncCreateParam badAttr{sizeof(JpegEncCreateParam), 1};
    alignas(32) static unsigned char memory[0x800 + 32];
    unsigned char* unaligned = memory + 1;
    void* handle = nullptr;

    EXPECT_EQ(sceJpegEncCreate(nullptr, unaligned, 0x800, &handle), kInvalidAddr);
    EXPECT_EQ(sceJpegEncCreate(&badSize, unaligned, 0x800, &handle), kInvalidSize);
    EXPECT_EQ(sceJpegEncCreate(&badAttr, unaligned, 0x800, &handle), kInvalidParam);
    EXPECT_EQ(sceJpegEncCreate(&ok, nullptr, 0x800, &handle), kInvalidAddr);
    EXPECT_EQ(sceJpegEncCreate(&ok, unaligned, 0x7FF, &handle), kInvalidSize);
    EXPECT_EQ(sceJpegEncCreate(&ok, unaligned, 0x800, nullptr), kInvalidAddr);
    EXPECT_EQ(handle, nullptr);

    ASSERT_EQ(sceJpegEncCreate(&ok, unaligned, 0x800, &handle), 0);
    const auto address = reinterpret_cast<std::uintptr_t>(handle);
    EXPECT_EQ(address % 32, 0u);
    EXPECT_GE(address, reinterpret_cast<std::uintptr_t>(unaligned));
    EXPECT_LT(address, reinterpret_cast<std::uintptr_t>(unaligned) + 32);

    EXPECT_EQ(sceJpegEncDelete(nullptr), kInvalidHandle);
    EXPECT_EQ(sceJpegEncDelete(static_cast<unsigned char*>(handle) + 1), kInvalidHandle);
    alignas(32) static unsigned char garbage[64] = {};  // aligned but untagged
    EXPECT_EQ(sceJpegEncDelete(garbage), kInvalidHandle);
    EXPECT_EQ(sceJpegEncDelete(handle), 0);
    EXPECT_EQ(sceJpegEncDelete(handle), kInvalidHandle);
}

// Invariant: Encode on a deleted, null or misaligned handle fails with
// INVALID_HANDLE before any other argument is examined.
TEST_F(JpegEncTest, EncodeRejectsBadHandles) {
    const JpegEncEncodeParam valid = ValidParam();
    EXPECT_EQ(sceJpegEncEncode(nullptr, &valid, &info_), kInvalidHandle);
    EXPECT_EQ(sceJpegEncEncode(static_cast<unsigned char*>(handle_) + 4, &valid, &info_), kInvalidHandle);
    ASSERT_EQ(sceJpegEncDelete(handle_), 0);
    EXPECT_EQ(sceJpegEncEncode(handle_, &valid, &info_), kInvalidHandle);
}

// Invariant: every malformed encode parameter maps to the documented error
// class (ADDR for pointers/alignment, SIZE for zero sizes, PARAM otherwise).
TEST_F(JpegEncTest, EncodeArgumentValidation) {
    EXPECT_EQ(sceJpegEncEncode(handle_, nullptr, &info_), kInvalidAddr);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image = nullptr; }), kInvalidAddr);
    EXPECT_EQ(EncodeWith([this](JpegEncEncodeParam& p) { p.image = image_ + 1; }), kInvalidAddr);  // unaligned RGBA
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.jpeg = nullptr; }), kInvalidAddr);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_size = 0; }), kInvalidSize);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.jpeg_size = 0; }), kInvalidSize);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_width = 0; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_height = 0; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_width = 0x10000; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_height = 0x10000; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_pitch = 0; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_pitch = 66; }), kInvalidParam);  // not 4-aligned
    EXPECT_EQ(EncodeWith([this](JpegEncEncodeParam& p) { p.image_size = sizeof(image_) - 1; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.encode_mode = 2; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.color_space = 0; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.sampling_type = 3; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.restart_interval = 0x10000; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.pixel_format = 2; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_width = 17; }), kInvalidParam);  // pitch too small
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.sampling_type = 0; }), kInvalidParam);  // RGBA needs subsampling
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.pixel_format = 11; }), kInvalidParam);  // Y8 needs gray colour space
}

// Invariant (overflow): height * pitch is evaluated in 64 bits. Maximal legal
// height with the maximal legal pitch (0xFFFF * 0xFFFFFFC ~ 2^44) must be
// rejected as PARAM rather than wrapping to a small value that would pass the
// image_size check and let the encoder read far outside the buffer.
TEST_F(JpegEncTest, EncodeRejectsHugeDimensionsWithoutOverflow) {
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_height = 0xFFFF; p.image_pitch = 0xFFFFFFC; }), kInvalidParam);
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) {
        p.image_height = 0xFFFF; p.image_width = 0xFFFF; p.image_pitch = 0xFFFF * 4; p.image_size = 0xFFFFFFFFu;
    }), kInvalidParam);  // 0xFFFF * 0x3FFFC > MAX_IMAGE_SIZE even though image_size claims room
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image_pitch = 0xFFFFFFF0u; }), kInvalidParam);  // > MAX_IMAGE_PITCH
}

// Invariant: a valid RGBA encode yields a JFIF stream whose size is reported
// in output_info and that decodes back to a 16x16 image close to the source.
TEST_F(JpegEncTest, EncodeRgbaRoundTrip) {
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.compression_ratio = 0; p.sampling_type = 2; }), 0);
    ASSERT_GT(info_.size, 4u);
    EXPECT_EQ(info_.height, 16u);
    EXPECT_EQ(jpeg_[0], 0xFF);
    EXPECT_EQ(jpeg_[1], 0xD8);
    EXPECT_EQ(jpeg_[info_.size - 2], 0xFF);
    EXPECT_EQ(jpeg_[info_.size - 1], 0xD9);
    const auto decoded = Decoder::Jpeg::Decode({jpeg_, info_.size});
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 16u);
    EXPECT_EQ(decoded->height, 16u);
    EXPECT_EQ(decoded->channels, 3u);
    long total = 0;
    for (int i = 0; i < 16 * 16; ++i) {
        for (int c = 0; c < 3; ++c) total += std::abs(int{image_[i * 4 + c]} - int{decoded->pixels[i * 3 + c]});
    }
    EXPECT_LE(total / (16 * 16 * 3), 12);
}

// Invariant: B8G8R8A8 input is channel-swapped, so encoding the same pixels
// as BGRA from a red/blue-swapped buffer decodes to (almost) the same RGB.
TEST_F(JpegEncTest, BgraIsChannelSwapped) {
    alignas(4) static unsigned char bgra[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i) {
        bgra[i * 4 + 0] = image_[i * 4 + 2];
        bgra[i * 4 + 1] = image_[i * 4 + 1];
        bgra[i * 4 + 2] = image_[i * 4 + 0];
        bgra[i * 4 + 3] = 255;
    }
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.compression_ratio = 0; }), 0);
    const auto rgbDecoded = Decoder::Jpeg::Decode({jpeg_, info_.size});
    ASSERT_TRUE(rgbDecoded.has_value());
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image = bgra; p.pixel_format = 1; p.compression_ratio = 0; }), 0);
    const auto bgraDecoded = Decoder::Jpeg::Decode({jpeg_, info_.size});
    ASSERT_TRUE(bgraDecoded.has_value());
    EXPECT_EQ(rgbDecoded->pixels, bgraDecoded->pixels);
}

// Invariant: grayscale Y8 input (byte-aligned address, full sampling, gray
// colour space) encodes to a gray-valued JPEG; the image pointer need not
// be 4-aligned for this format.
TEST_F(JpegEncTest, GrayscaleY8) {
    alignas(4) static unsigned char gray[16 * 16 + 1];
    for (int i = 0; i < 16 * 16; ++i) gray[1 + i] = static_cast<unsigned char>(i);
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam& p) {
        p.image = gray + 1; p.image_size = 16 * 16; p.image_pitch = 16;
        p.pixel_format = 11; p.color_space = 2; p.sampling_type = 0; p.compression_ratio = 0;
    }), 0);
    const auto decoded = Decoder::Jpeg::Decode({jpeg_, info_.size});
    ASSERT_TRUE(decoded.has_value());
    // The shared (stb) encoder always writes a 3-component YCbCr stream, so a
    // grayscale image decodes as RGB with R == G == B (docs/spec/image-codecs.md).
    ASSERT_EQ(decoded->channels, 3u);
    for (std::size_t i = 0; i + 2 < decoded->pixels.size(); i += 3) {
        EXPECT_NEAR(decoded->pixels[i], decoded->pixels[i + 1], 2);
        EXPECT_NEAR(decoded->pixels[i], decoded->pixels[i + 2], 2);
    }
}

// Invariant: a too-small output buffer yields INVALID_SIZE and writes nothing
// beyond jpeg_size (a canary byte after the declared end stays intact).
TEST_F(JpegEncTest, OutputBufferTooSmall) {
    jpeg_[64] = 0x5A;
    EXPECT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.jpeg_size = 64; }), kInvalidSize);
    EXPECT_EQ(jpeg_[64], 0x5A);
}

// Invariant: pitch padding is skipped, not encoded (row stride > row bytes).
TEST_F(JpegEncTest, PaddedPitchMatchesTightEncode) {
    constexpr std::uint32_t pitch = 16 * 4 + 16;
    alignas(4) static unsigned char padded[16 * pitch];
    std::memset(padded, 0xEE, sizeof(padded));
    for (int y = 0; y < 16; ++y) std::memcpy(padded + y * pitch, image_ + y * 64, 64);
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam&) {}), 0);
    const std::vector<unsigned char> tight(jpeg_, jpeg_ + info_.size);
    ASSERT_EQ(EncodeWith([](JpegEncEncodeParam& p) { p.image = padded; p.image_size = sizeof(padded); p.image_pitch = pitch; }), 0);
    EXPECT_EQ(tight, std::vector<unsigned char>(jpeg_, jpeg_ + info_.size));
}

// Invariant: modes the library cannot honour abort through Unsupported()
// instead of throwing or silently emitting a wrong stream.
TEST_F(JpegEncTest, UnsupportedModesAbort) {
    EXPECT_DEATH(EncodeWith([](JpegEncEncodeParam& p) { p.encode_mode = 1; }), "");
    EXPECT_DEATH(EncodeWith([](JpegEncEncodeParam& p) { p.restart_interval = 4; }), "");
}

// ---- Guest pointer range validation (GuestMemoryValidation) -----------------
// Each case hands the export a pointer whose range is unreadable/unwritable.
// Without the validation the host dereferences it and the process faults;
// with it the export returns the SCE code and touches nothing.

using GuestTest::GuestPages;
using GuestTest::PageAccess;

// Invariant: Create/QueryMemorySize never dereference an unreadable param
// struct, and Create never writes unwritable work memory or handle slot.
TEST(JpegEncGuestRanges, CreateRejectsUnusableGuestPointers) {
    GuestPages none(1, PageAccess::None);
    GuestPages readOnly(1, PageAccess::ReadOnly);
    ASSERT_NE(none.data(), nullptr);
    ASSERT_NE(readOnly.data(), nullptr);
    const JpegEncCreateParam ok{sizeof(JpegEncCreateParam), 0};
    alignas(32) static unsigned char work[0x800 + 32];
    void* handle = nullptr;

    EXPECT_EQ(sceJpegEncQueryMemorySize(reinterpret_cast<const JpegEncCreateParam*>(none.data())), kInvalidAddr);
    EXPECT_EQ(sceJpegEncCreate(reinterpret_cast<const JpegEncCreateParam*>(none.data()), work, 0x800, &handle), kInvalidAddr);
    EXPECT_EQ(sceJpegEncCreate(&ok, none.data(), 0x800, &handle), kInvalidAddr);      // work memory unreadable
    EXPECT_EQ(sceJpegEncCreate(&ok, readOnly.data(), 0x800, &handle), kInvalidAddr);  // work memory not writable
    EXPECT_EQ(sceJpegEncCreate(&ok, work, 0x800, reinterpret_cast<void**>(readOnly.data())), kInvalidAddr);  // handle slot not writable
    EXPECT_EQ(handle, nullptr);
    // Work memory that runs off the mapping end is rejected before any write. The pages are
    // read/write on purpose: the only reason to refuse is the 0x800-byte extent reaching the
    // guard page, so this fails if the check covers fewer bytes than MEMORY_SIZE.
    GuestPages rw(1, PageAccess::ReadWrite);
    ASSERT_NE(rw.data(), nullptr);
    EXPECT_EQ(sceJpegEncCreate(&ok, rw.end() - 0x100, 0x800, &handle), kInvalidAddr);
    EXPECT_EQ(handle, nullptr);
}

// Invariant: a forged handle pointing at unreadable memory is INVALID_HANDLE
// for both Encode and Delete, never a host fault while reading the live tag.
TEST(JpegEncGuestRanges, ForgedHandleInUnreadableMemory) {
    GuestPages none(1, PageAccess::None);
    ASSERT_NE(none.data(), nullptr);
    JpegEncOutputInfo info{};
    JpegEncEncodeParam param{};
    EXPECT_EQ(sceJpegEncDelete(none.data()), kInvalidHandle);
    EXPECT_EQ(sceJpegEncEncode(none.data(), &param, &info), kInvalidHandle);
}

// Invariant: Encode checks every guest range it touches: the param struct, the
// pixel buffer extent (height-1 pitches + last row), output_info and the bytes
// of JPEG it will write. image_size/jpeg_size are only the title's claims.
TEST_F(JpegEncTest, EncodeRejectsUnusableGuestBuffers) {
    GuestPages none(1, PageAccess::None);
    GuestPages readOnly(1, PageAccess::ReadOnly);
    GuestPages rw(1, PageAccess::ReadWrite);
    ASSERT_NE(none.data(), nullptr);
    ASSERT_NE(readOnly.data(), nullptr);
    ASSERT_NE(rw.data(), nullptr);

    // Param struct itself unreadable.
    EXPECT_EQ(sceJpegEncEncode(handle_, reinterpret_cast<const JpegEncEncodeParam*>(none.data()), &info_), kInvalidAddr);

    // Pixel buffer unreadable.
    EXPECT_EQ(EncodeWith([&](JpegEncEncodeParam& p) { p.image = none.data(); }), kInvalidAddr);

    // Pixel buffer whose claimed size (image_size is 1024 bytes of RGBA) runs past the mapping.
    EXPECT_EQ(EncodeWith([&](JpegEncEncodeParam& p) { p.image = rw.end() - 100; }), kInvalidAddr);

    // Output buffer not writable (read-only) and output_info not writable.
    EXPECT_EQ(EncodeWith([&](JpegEncEncodeParam& p) { p.jpeg = readOnly.data(); p.jpeg_size = 4096; }), kInvalidAddr);
    JpegEncEncodeParam p = ValidParam();
    EXPECT_EQ(sceJpegEncEncode(handle_, &p, reinterpret_cast<JpegEncOutputInfo*>(readOnly.data())), kInvalidAddr);

    // A fully valid call still succeeds afterwards (nothing was left half-written).
    EXPECT_EQ(sceJpegEncEncode(handle_, &p, &info_), 0);
    EXPECT_GT(info_.size, 0u);
}

// Invariant: only the produced JPEG bytes must be writable, not the claimed
// jpeg_size, so a title that over-declares capacity but maps enough still works.
TEST_F(JpegEncTest, EncodeOnlyNeedsProducedBytesWritable) {
    GuestPages rw(1, PageAccess::ReadWrite);
    ASSERT_NE(rw.data(), nullptr);
    JpegEncEncodeParam p = ValidParam();
    p.jpeg = rw.end() - 4096;  // 4 KiB writable, jpeg_size claims much more
    p.jpeg_size = 1 << 20;
    EXPECT_EQ(sceJpegEncEncode(handle_, &p, &info_), 0);
    EXPECT_LE(info_.size, 4096u);
}
