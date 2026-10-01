// core/libs/tests/GuestPngDec.cpp
// GoogleTest suite for libScePngDec (core/libs/prx/libScePngDec/Export.cpp).
//
// Calls the exports as a guest would (System V ABI, raw pointers). PNG inputs
// are generated in-test with the shared encoder (plus a hand-inserted tRNS
// chunk and forged IHDR fields); no game data is used
// (.agents/rules/legal-boundary.md).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "Decoder/Png.hpp"
#include "GuestTestPages.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

extern "C" {
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI scePngDecQueryMemorySize(const PngDecCreateParam*) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI scePngDecCreate(const PngDecCreateParam*, void*, std::uint32_t, void**) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI scePngDecDelete(void*) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI scePngDecParseHeader(const PngDecParseParam*, PngDecImageInfo*) noexcept;
// Guest export under test (contract documented in the PRX's Export.cpp).
std::int32_t APS5_VABI scePngDecDecode(void*, const PngDecDecodeParam*, PngDecImageInfo*) noexcept;
}

namespace {

constexpr std::int32_t kInvalidAddr = static_cast<std::int32_t>(0x80690001);
constexpr std::int32_t kInvalidSize = static_cast<std::int32_t>(0x80690002);
constexpr std::int32_t kInvalidParam = static_cast<std::int32_t>(0x80690003);
constexpr std::int32_t kInvalidHandle = static_cast<std::int32_t>(0x80690004);
constexpr std::int32_t kInvalidWorkMemory = static_cast<std::int32_t>(0x80690005);
constexpr std::int32_t kInvalidData = static_cast<std::int32_t>(0x80690010);
constexpr std::int32_t kDecodeError = static_cast<std::int32_t>(0x80690012);

constexpr std::size_t kIhdrData = 16;  // signature(8) + length(4) + "IHDR"(4)

std::vector<std::uint8_t> EncodePng(const std::vector<std::uint8_t>& pixels, std::uint32_t w, std::uint32_t h, std::uint32_t channels) {
    auto png = Decoder::Png::Encode(pixels, w, h, channels);
    EXPECT_TRUE(png.has_value());
    return png.value_or(std::vector<std::uint8_t>{});
}

// Inserts a zero-filled tRNS chunk right after IHDR. ParseHeader/stb do not
// verify CRCs, so the bogus CRC is fine; the chunk only has to precede IDAT.
std::vector<std::uint8_t> WithTrns(std::vector<std::uint8_t> png) {
    const std::vector<std::uint8_t> chunk = {0, 0, 0, 6, 't', 'R', 'N', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    png.insert(png.begin() + 8 + 12 + 13, chunk.begin(), chunk.end());
    return png;
}

// Fixture: a live decoder handle in misaligned work memory.
class PngDecTest : public ::testing::Test {
protected:
    void SetUp() override {
        const PngDecCreateParam create{sizeof(PngDecCreateParam), 0, 4096};
        ASSERT_EQ(scePngDecCreate(&create, work_ + 1, 0x20, &handle_), 0);
    }
    void TearDown() override { scePngDecDelete(handle_); }

    std::int32_t Decode(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& out, std::uint16_t format = 0,
                        std::uint16_t alpha = 0x80, std::uint32_t pitch = 0) {
        PngDecDecodeParam p{};
        p.png_mem_addr = png.data();
        p.png_mem_size = static_cast<std::uint32_t>(png.size());
        p.image_mem_addr = out.data();
        p.image_mem_size = static_cast<std::uint32_t>(out.size());
        p.pixel_format = format;
        p.alpha_value = alpha;
        p.image_pitch = pitch;
        return scePngDecDecode(handle_, &p, &info_);
    }

    alignas(8) unsigned char work_[0x20 + 16]{};
    void* handle_ = nullptr;
    PngDecImageInfo info_{};
};

}  // namespace

// Invariant: QueryMemorySize validates max_image_width/attribute and otherwise
// reports the fixed 0x20-byte context size.
TEST(PngDecLifecycle, QueryMemorySize) {
    const PngDecCreateParam ok{sizeof(PngDecCreateParam), 0, 1920};
    EXPECT_EQ(scePngDecQueryMemorySize(&ok), 0x20);
    EXPECT_EQ(scePngDecQueryMemorySize(nullptr), kInvalidParam);
    const PngDecCreateParam badAttr{sizeof(PngDecCreateParam), 2, 1920};
    EXPECT_EQ(scePngDecQueryMemorySize(&badAttr), kInvalidParam);
    const PngDecCreateParam zeroWidth{sizeof(PngDecCreateParam), 0, 0};
    EXPECT_EQ(scePngDecQueryMemorySize(&zeroWidth), kInvalidSize);
    const PngDecCreateParam hugeWidth{sizeof(PngDecCreateParam), 0, 1000002};
    EXPECT_EQ(scePngDecQueryMemorySize(&hugeWidth), kInvalidSize);
    const PngDecCreateParam maxWidth{sizeof(PngDecCreateParam), 0, 1000001};
    EXPECT_EQ(scePngDecQueryMemorySize(&maxWidth), 0x20);
}

// Invariant: Create/Delete argument codes, 8-byte handle alignment inside
// misaligned work memory, and single-shot Delete.
TEST(PngDecLifecycle, CreateDeleteContract) {
    const PngDecCreateParam ok{sizeof(PngDecCreateParam), 0, 1920};
    alignas(8) static unsigned char memory[0x20 + 8];
    void* handle = nullptr;
    EXPECT_EQ(scePngDecCreate(nullptr, memory + 1, 0x20, &handle), kInvalidParam);
    EXPECT_EQ(scePngDecCreate(&ok, nullptr, 0x20, &handle), kInvalidAddr);
    EXPECT_EQ(scePngDecCreate(&ok, memory + 1, 0x20, nullptr), kInvalidAddr);
    EXPECT_EQ(scePngDecCreate(&ok, memory + 1, 0x1F, &handle), kInvalidWorkMemory);
    EXPECT_EQ(handle, nullptr);
    ASSERT_EQ(scePngDecCreate(&ok, memory + 1, 0x20, &handle), 0);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(handle) % 8, 0u);

    EXPECT_EQ(scePngDecDelete(nullptr), kInvalidHandle);
    EXPECT_EQ(scePngDecDelete(static_cast<unsigned char*>(handle) + 1), kInvalidHandle);
    EXPECT_EQ(scePngDecDelete(handle), 0);
    EXPECT_EQ(scePngDecDelete(handle), kInvalidHandle);
}

// Invariant: ParseHeader maps PNG colour types to SCE colour-space codes,
// reports depth, and sets the tRNS flag only when the chunk precedes IDAT.
TEST(PngDecParse, HeaderFields) {
    const auto rgba = EncodePng(std::vector<std::uint8_t>(3 * 2 * 4, 9), 3, 2, 4);
    PngDecImageInfo info{};
    PngDecParseParam param{rgba.data(), static_cast<std::uint32_t>(rgba.size()), 0};
    ASSERT_EQ(scePngDecParseHeader(&param, &info), 0);
    EXPECT_EQ(info.image_width, 3u);
    EXPECT_EQ(info.image_height, 2u);
    EXPECT_EQ(info.color_space, 19);
    EXPECT_EQ(info.bit_depth, 8);
    EXPECT_EQ(info.image_flag, 0u);

    const auto rgb = EncodePng(std::vector<std::uint8_t>(3 * 2 * 3, 9), 3, 2, 3);
    param = {rgb.data(), static_cast<std::uint32_t>(rgb.size()), 0};
    ASSERT_EQ(scePngDecParseHeader(&param, &info), 0);
    EXPECT_EQ(info.color_space, 3);
    const auto trns = WithTrns(rgb);
    param = {trns.data(), static_cast<std::uint32_t>(trns.size()), 0};
    ASSERT_EQ(scePngDecParseHeader(&param, &info), 0);
    EXPECT_EQ(info.image_flag, 2u);

    const auto gray = EncodePng(std::vector<std::uint8_t>(6, 9), 3, 2, 1);
    param = {gray.data(), static_cast<std::uint32_t>(gray.size()), 0};
    ASSERT_EQ(scePngDecParseHeader(&param, &info), 0);
    EXPECT_EQ(info.color_space, 2);
    const auto grayAlpha = EncodePng(std::vector<std::uint8_t>(12, 9), 3, 2, 2);
    param = {grayAlpha.data(), static_cast<std::uint32_t>(grayAlpha.size()), 0};
    ASSERT_EQ(scePngDecParseHeader(&param, &info), 0);
    EXPECT_EQ(info.color_space, 18);
}

// Invariant: ParseHeader argument and data errors (null param/pointers, zero
// size, non-PNG bytes, truncated data).
TEST(PngDecParse, Errors) {
    const auto png = EncodePng(std::vector<std::uint8_t>(16, 1), 2, 2, 4);
    PngDecImageInfo info{};
    EXPECT_EQ(scePngDecParseHeader(nullptr, &info), kInvalidParam);
    PngDecParseParam param{nullptr, 100, 0};
    EXPECT_EQ(scePngDecParseHeader(&param, &info), kInvalidAddr);
    param = {png.data(), static_cast<std::uint32_t>(png.size()), 0};
    EXPECT_EQ(scePngDecParseHeader(&param, nullptr), kInvalidAddr);
    param = {png.data(), 0, 0};
    EXPECT_EQ(scePngDecParseHeader(&param, &info), kInvalidSize);
    param = {png.data(), 10, 0};  // shorter than signature + IHDR
    EXPECT_EQ(scePngDecParseHeader(&param, &info), kInvalidData);
    std::vector<std::uint8_t> garbage(64, 0x33);
    param = {garbage.data(), 64, 0};
    EXPECT_EQ(scePngDecParseHeader(&param, &info), kInvalidData);
}

// Invariant: RGBA decode is lossless, reports the packed (w<<16|h) result and
// fills image_info.
TEST_F(PngDecTest, DecodeRgba) {
    std::vector<std::uint8_t> src(5 * 3 * 4);
    for (std::size_t i = 0; i < src.size(); ++i) src[i] = static_cast<std::uint8_t>(i * 5 + 1);
    const auto png = EncodePng(src, 5, 3, 4);
    std::vector<std::uint8_t> out(src.size());
    EXPECT_EQ(Decode(png, out), (5 << 16) | 3);
    EXPECT_EQ(out, src);
    EXPECT_EQ(info_.image_width, 5u);
    EXPECT_EQ(info_.color_space, 19);
}

// Invariant: B8G8R8A8 output swaps R and B; alpha_value fills alpha only for
// sources without any alpha channel or tRNS (RGB here).
TEST_F(PngDecTest, DecodeRgbBgraFillsAlpha) {
    const std::vector<std::uint8_t> src = {1, 2, 3, 4, 5, 6};
    const auto png = EncodePng(src, 2, 1, 3);
    std::vector<std::uint8_t> out(8);
    ASSERT_EQ(Decode(png, out, 1, 0x7F), (2 << 16) | 1);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{3, 2, 1, 0x7F, 6, 5, 4, 0x7F}));
}

// Invariant: a source that carries its own alpha keeps it instead of
// alpha_value.
TEST_F(PngDecTest, DecodeKeepsSourceAlpha) {
    const std::vector<std::uint8_t> src = {10, 50, 200, 60};  // gray+alpha
    const auto png = EncodePng(src, 2, 1, 2);
    std::vector<std::uint8_t> out(8);
    ASSERT_GT(Decode(png, out, 0, 0xEE), 0);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{10, 10, 10, 50, 200, 200, 200, 60}));
}

// Invariant: a caller pitch larger than the row is honoured and the padding
// bytes are left untouched; a pitch below the row size is INVALID_PARAM.
TEST_F(PngDecTest, PitchHandling) {
    const std::vector<std::uint8_t> src = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const auto png = EncodePng(src, 2, 2, 4);
    std::vector<std::uint8_t> out(2 * 16, 0xCC);
    ASSERT_GT(Decode(png, out, 0, 0, 16), 0);
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin(), out.begin() + 8), (std::vector<std::uint8_t>{1, 2, 3, 255, 4, 5, 6, 255}));
    EXPECT_EQ(out[8], 0xCC);  // padding untouched
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin() + 16, out.begin() + 24), (std::vector<std::uint8_t>{7, 8, 9, 255, 10, 11, 12, 255}));
    EXPECT_EQ(Decode(png, out, 0, 0, 4), kInvalidParam);
}

// Invariant (overflow): output bounds are computed in 64 bits before any
// decode. Forged header dimensions of 2^31-1 x 2^31-1 and a maximal pitch
// must yield INVALID_SIZE (output too small), never a wrapped small number
// and never a huge allocation.
TEST_F(PngDecTest, ForgedHugeDimensionsAreRejectedBeforeDecoding) {
    auto png = EncodePng(std::vector<std::uint8_t>(4 * 4 * 4, 7), 4, 4, 4);
    const auto put = [&](std::size_t off, std::uint32_t v) {
        png[off] = static_cast<std::uint8_t>(v >> 24); png[off + 1] = static_cast<std::uint8_t>(v >> 16);
        png[off + 2] = static_cast<std::uint8_t>(v >> 8); png[off + 3] = static_cast<std::uint8_t>(v);
    };
    put(kIhdrData, 0x7FFFFFFFu);
    put(kIhdrData + 4, 0x7FFFFFFFu);
    std::vector<std::uint8_t> out(64);
    EXPECT_EQ(Decode(png, out), kInvalidSize);
    // Row size (2^31-1)*4 exceeds any 32-bit pitch, so this is a PARAM error.
    EXPECT_EQ(Decode(png, out, 0, 0, 0xFFFFFFFFu), kInvalidParam);
    // Narrow but enormously tall: (height-1)*pitch ~ 2^63 must not wrap.
    put(kIhdrData, 4);
    EXPECT_EQ(Decode(png, out, 0, 0, 0xFFFFFFFFu), kInvalidSize);
}

// Invariant: argument validation order and codes for Decode.
TEST_F(PngDecTest, DecodeArgumentValidation) {
    const auto png = EncodePng(std::vector<std::uint8_t>(16, 1), 2, 2, 4);
    std::vector<std::uint8_t> out(16);
    PngDecDecodeParam p{};
    p.png_mem_addr = png.data();
    p.png_mem_size = static_cast<std::uint32_t>(png.size());
    p.image_mem_addr = out.data();
    p.image_mem_size = 16;

    EXPECT_EQ(scePngDecDecode(nullptr, &p, &info_), kInvalidHandle);
    EXPECT_EQ(scePngDecDecode(handle_, nullptr, &info_), kInvalidParam);
    PngDecDecodeParam bad = p; bad.png_mem_addr = nullptr;
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidAddr);
    bad = p; bad.image_mem_addr = nullptr;
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidAddr);
    bad = p; bad.png_mem_size = 0;
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidSize);
    bad = p; bad.image_mem_size = 0;
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidSize);
    bad = p; bad.image_mem_size = 15;  // one byte short of the 2x2 RGBA image
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidSize);
    bad = p; bad.pixel_format = 2;
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidParam);
    bad = p; bad.png_mem_size = 12;  // too short to be a PNG
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kInvalidData);
    // Valid header but truncated pixel data: header parses, decode fails.
    bad = p; bad.png_mem_size = static_cast<std::uint32_t>(png.size() - 20);
    EXPECT_EQ(scePngDecDecode(handle_, &bad, &info_), kDecodeError);
}

// Invariant: a 16-bit source with the 16-bit-output attribute is an
// unsupported state and aborts (Unsupported) rather than throwing.
TEST(PngDecUnsupported, SixteenBitOutputAborts) {
    const PngDecCreateParam create{sizeof(PngDecCreateParam), 1, 64};
    alignas(8) static unsigned char memory[0x20 + 8];
    void* handle = nullptr;
    ASSERT_EQ(scePngDecCreate(&create, memory, 0x20, &handle), 0);
    auto png = EncodePng(std::vector<std::uint8_t>(16, 1), 2, 2, 4);
    png[kIhdrData + 8] = 16;  // forge bit depth 16 (legal for RGBA)
    std::vector<std::uint8_t> out(16);
    PngDecDecodeParam p{};
    p.png_mem_addr = png.data();
    p.png_mem_size = static_cast<std::uint32_t>(png.size());
    p.image_mem_addr = out.data();
    p.image_mem_size = 16;
    PngDecImageInfo info{};
    EXPECT_DEATH(scePngDecDecode(handle, &p, &info), "");
}

// ---- Guest pointer range validation (GuestMemoryValidation) -----------------
// Each case hands the export a pointer whose range is unreadable/unwritable.
// Without the validation the host dereferences it and the process faults;
// with it the export returns an SCE code and touches nothing.

using GuestTest::GuestPages;
using GuestTest::PageAccess;

// Invariant: struct pointers that are unreadable behave like null ones
// (INVALID_PARAM for param structs, INVALID_HANDLE for handles).
TEST(PngDecGuestRanges, UnreadableStructPointers) {
    GuestPages none(1, PageAccess::None);
    ASSERT_NE(none.data(), nullptr);
    PngDecImageInfo info{};
    EXPECT_EQ(scePngDecQueryMemorySize(reinterpret_cast<const PngDecCreateParam*>(none.data())), kInvalidParam);
    EXPECT_EQ(scePngDecParseHeader(reinterpret_cast<const PngDecParseParam*>(none.data()), &info), kInvalidParam);
    EXPECT_EQ(scePngDecDelete(none.data()), kInvalidHandle);
    PngDecDecodeParam decode{};
    EXPECT_EQ(scePngDecDecode(none.data(), &decode, &info), kInvalidHandle);
}

// Invariant: Create refuses work memory the host cannot write and a handle
// slot it cannot write, before placing any header.
TEST(PngDecGuestRanges, CreateRejectsUnwritableMemory) {
    GuestPages none(1, PageAccess::None);
    GuestPages readOnly(1, PageAccess::ReadOnly);
    ASSERT_NE(none.data(), nullptr);
    ASSERT_NE(readOnly.data(), nullptr);
    const PngDecCreateParam ok{sizeof(PngDecCreateParam), 0, 1920};
    alignas(8) static unsigned char work[0x20 + 8];
    void* handle = nullptr;
    EXPECT_EQ(scePngDecCreate(&ok, readOnly.data(), 0x20, &handle), kInvalidWorkMemory);
    EXPECT_EQ(scePngDecCreate(&ok, none.data(), 0x20, &handle), kInvalidWorkMemory);
    EXPECT_EQ(scePngDecCreate(&ok, work, 0x20, reinterpret_cast<void**>(readOnly.data())), kInvalidAddr);
    EXPECT_EQ(handle, nullptr);
}

// Invariant: ParseHeader and Decode check the PNG bytes, image_info and the
// output raster extent (not the title's claimed image_mem_size).
TEST_F(PngDecTest, DecodeAndParseRejectUnusableBuffers) {
    const auto png = EncodePng(std::vector<std::uint8_t>(4 * 4 * 4, 7), 4, 4, 4);
    GuestPages none(1, PageAccess::None);
    GuestPages readOnly(1, PageAccess::ReadOnly);
    GuestPages rw(1, PageAccess::ReadWrite);
    ASSERT_NE(none.data(), nullptr);
    ASSERT_NE(readOnly.data(), nullptr);
    ASSERT_NE(rw.data(), nullptr);
    std::vector<std::uint8_t> out(4 * 4 * 4);

    // PNG data unreadable (ParseHeader and Decode).
    PngDecParseParam parse{none.data(), static_cast<std::uint32_t>(png.size()), 0};
    PngDecImageInfo info{};
    EXPECT_EQ(scePngDecParseHeader(&parse, &info), kInvalidAddr);
    PngDecDecodeParam p{};
    p.png_mem_addr = none.data();
    p.png_mem_size = static_cast<std::uint32_t>(png.size());
    p.image_mem_addr = out.data();
    p.image_mem_size = static_cast<std::uint32_t>(out.size());
    EXPECT_EQ(scePngDecDecode(handle_, &p, &info), kInvalidAddr);

    // image_info not writable.
    parse = {png.data(), static_cast<std::uint32_t>(png.size()), 0};
    EXPECT_EQ(scePngDecParseHeader(&parse, reinterpret_cast<PngDecImageInfo*>(readOnly.data())), kInvalidAddr);

    // Output raster read-only, and output raster running off the mapping end.
    p.png_mem_addr = png.data();
    p.image_mem_addr = readOnly.data();
    p.image_mem_size = static_cast<std::uint32_t>(out.size());
    EXPECT_EQ(scePngDecDecode(handle_, &p, &info), kInvalidAddr);
    p.image_mem_addr = rw.end() - 20;  // 4x4 RGBA needs 64 bytes, only 20 are mapped
    EXPECT_EQ(scePngDecDecode(handle_, &p, &info), kInvalidAddr);

    // A valid decode still works afterwards.
    p.image_mem_addr = out.data();
    EXPECT_GT(scePngDecDecode(handle_, &p, &info), 0);
}
