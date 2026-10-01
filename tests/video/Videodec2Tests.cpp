// tests/video/Videodec2Tests.cpp
// GoogleTest suite for the libSceVideodec2 guest ABI: struct layouts, every SCE_VIDEODEC2_ERROR_*
// code the library returns (values and check order follow KytyPS5 libVideoDec2.cpp), the NV12 output
// layout (256-byte pitch, chroma plane right after pitch * height luma rows), picture-info reporting
// and handle lifecycle. Pictures come from synthetic I_PCM / P_Skip streams (H264TestStream.hpp), so
// the decoded bytes are exactly checkable. Decode tests skip, loudly, when FFmpeg is not built.

#include "H264Decoder.hpp"
#include "H264TestStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace H264Test;

constexpr int kApiFail = static_cast<int>(0x811D0100u);
constexpr int kStructSize = static_cast<int>(0x811D0101u);
constexpr int kArgumentPointer = static_cast<int>(0x811D0102u);
constexpr int kDecoderInstance = static_cast<int>(0x811D0103u);
constexpr int kMemorySize = static_cast<int>(0x811D0104u);
constexpr int kMemoryPointer = static_cast<int>(0x811D0105u);
constexpr int kFrameBufferSize = static_cast<int>(0x811D0106u);
constexpr int kFrameBufferPointer = static_cast<int>(0x811D0107u);
constexpr int kAccessUnitSize = static_cast<int>(0x811D010Du);
constexpr int kAccessUnitPointer = static_cast<int>(0x811D010Eu);
constexpr int kOutputInfo = static_cast<int>(0x811D010Fu);
constexpr int kComputeQueue = static_cast<int>(0x811D0110u);
constexpr int kConfigInfo = static_cast<int>(0x811D0200u);
constexpr int kComputePipeId = static_cast<int>(0x811D0201u);
constexpr int kComputeQueueId = static_cast<int>(0x811D0202u);
constexpr int kResourceType = static_cast<int>(0x811D0203u);
constexpr int kCodecType = static_cast<int>(0x811D0204u);
constexpr int kInputQueueDepth = static_cast<int>(0x811D0206u);
constexpr int kDpbFrameCount = static_cast<int>(0x811D0209u);
constexpr int kFrameWidthHeight = static_cast<int>(0x811D020Au);
constexpr int kAccessUnit = static_cast<int>(0x811D0301u);
constexpr int kOversizeDecode = static_cast<int>(0x811D0302u);

// Guest layouts repeated on purpose so a layout change in the library is caught here.
struct ComputeMemoryInfo { std::uint64_t thisSize, cpuGpuMemorySize, cpuGpuMemory; };
struct ComputeConfigInfo { std::uint64_t thisSize; std::uint16_t pipe, queue; std::uint8_t check, r0; std::uint16_t r1; };
struct DecoderConfigInfo {
    std::uint64_t thisSize;
    std::uint32_t resourceType, codecType, profile, maxLevel;
    std::int32_t maxW, maxH, maxDpb;
    std::uint32_t queueDepth;
    std::uint64_t computeQueue, affinity;
    std::int32_t priority;
    std::uint8_t optimize, check, r0, r1;
    std::uint64_t extra;
};
struct DecoderMemoryInfo {
    std::uint64_t thisSize, cpuSize, cpu, gpuSize, gpu, cpuGpuSize, cpuGpu, maxFrameBuffer;
    std::uint32_t alignment, reserved;
};
struct InputData { std::uint64_t thisSize, auData, auSize, pts, dts, attached; };
struct FrameBuffer { std::uint64_t thisSize, buffer, size; std::uint8_t accepted, pad[7]; };
struct OutputInfo {
    std::uint64_t thisSize;
    std::uint8_t valid, errorFrame, pictureCount, discarded;
    std::uint32_t codecType, width, pitch, height;
    std::uint64_t frameBuffer, frameBufferSize;
    std::uint32_t frameFormat, pitchInBytes;
};
static_assert(sizeof(ComputeMemoryInfo) == 24 && sizeof(ComputeConfigInfo) == 16 && sizeof(DecoderConfigInfo) == 72);
static_assert(sizeof(DecoderMemoryInfo) == 72 && sizeof(InputData) == 48 && sizeof(FrameBuffer) == 32 && sizeof(OutputInfo) == 56);

extern "C" {
int APS5_VABI sceVideodec2QueryComputeMemoryInfo(ComputeMemoryInfo*) noexcept;
int APS5_VABI sceVideodec2AllocateComputeQueue(const ComputeConfigInfo*, const ComputeMemoryInfo*, std::uint64_t*) noexcept;
int APS5_VABI sceVideodec2ReleaseComputeQueue(std::uint64_t) noexcept;
int APS5_VABI sceVideodec2QueryDecoderMemoryInfo(const DecoderConfigInfo*, DecoderMemoryInfo*) noexcept;
int APS5_VABI sceVideodec2CreateDecoder(const DecoderConfigInfo*, const DecoderMemoryInfo*, std::uint64_t*) noexcept;
int APS5_VABI sceVideodec2DeleteDecoder(std::uint64_t) noexcept;
int APS5_VABI sceVideodec2Decode(std::uint64_t, const InputData*, FrameBuffer*, OutputInfo*) noexcept;
int APS5_VABI sceVideodec2Flush(std::uint64_t, FrameBuffer*, OutputInfo*) noexcept;
int APS5_VABI sceVideodec2Reset(std::uint64_t) noexcept;
int APS5_VABI sceVideodec2GetPictureInfo(const OutputInfo*, void*, void*) noexcept;
int APS5_VABI sceVideodec2GetAvcPictureInfo(const OutputInfo*, void*, void*) noexcept;
}

#define REQUIRE_HOST_DECODER()                                                         \
    do {                                                                               \
        if (!Videodec2::H264DecoderAvailable()) GTEST_SKIP() << "FFmpeg is not built"; \
    } while (0)

/** A valid config plus its memory info, and a scratch area for the 16 MiB-sized pointers. */
struct DecoderSetup {
    DecoderConfigInfo config{};
    DecoderMemoryInfo memory{};
    char scratch;  // Only its address is used (the library never dereferences these pointers).
    DecoderSetup() {
        config.thisSize = sizeof(config);
        config.resourceType = 1;
        config.codecType = 1;
        config.maxW = config.maxH = -1;
        config.maxDpb = -1;
        config.queueDepth = 1;
        config.computeQueue = reinterpret_cast<std::uintptr_t>(&scratch);
        memory.thisSize = sizeof(memory);
        memory.cpuSize = memory.gpuSize = memory.cpuGpuSize = memory.maxFrameBuffer = 16ull << 20;
        memory.cpu = memory.gpu = memory.cpuGpu = reinterpret_cast<std::uintptr_t>(&scratch);
        memory.alignment = 0x100;
    }
};

class Videodec2Test : public ::testing::Test {
protected:
    void TearDown() override {
        if (handle_ != 0) sceVideodec2DeleteDecoder(handle_);
    }
    /** Creates a decoder from `setup`; returns the SCE result. */
    int Create(const DecoderSetup& setup) { return sceVideodec2CreateDecoder(&setup.config, &setup.memory, &handle_); }

    /** Decodes one access unit into `fb` (sized `bufferBytes`), filling `out`. */
    int Decode(const std::vector<std::uint8_t>& au, std::vector<std::uint8_t>& fb, OutputInfo& out, std::uint64_t pts = 1, std::uint64_t attached = 0) {
        InputData in{sizeof(in), reinterpret_cast<std::uintptr_t>(au.data()), au.size(), pts, pts - 1, attached};
        FrameBuffer frame{sizeof(frame), reinterpret_cast<std::uintptr_t>(fb.data()), fb.size(), 0, {}};
        out = {};
        out.thisSize = sizeof(out);
        const int rc = sceVideodec2Decode(handle_, &in, &frame, &out);
        accepted_ = frame.accepted != 0;
        return rc;
    }

    std::uint64_t handle_ = 0;
    bool accepted_ = false;
};

// Query/allocate compute queue: struct size, pipe/queue id ranges, memory size and pointer checks,
// in the oracle's order; the queue handle is the caller's scratch pointer; release needs non-null.
TEST_F(Videodec2Test, ComputeQueueArgumentErrors) {
    ComputeMemoryInfo mem{sizeof(mem), 0, 0};
    EXPECT_EQ(sceVideodec2QueryComputeMemoryInfo(nullptr), kArgumentPointer);
    ComputeMemoryInfo bad{8, 0, 0};
    EXPECT_EQ(sceVideodec2QueryComputeMemoryInfo(&bad), kStructSize);
    ASSERT_EQ(sceVideodec2QueryComputeMemoryInfo(&mem), 0);
    EXPECT_EQ(mem.cpuGpuMemorySize, 16ull << 20);

    char scratch;
    mem.cpuGpuMemory = reinterpret_cast<std::uintptr_t>(&scratch);
    ComputeConfigInfo cfg{sizeof(cfg), 0, 0, 0, 0, 0};
    std::uint64_t queue = 0;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(nullptr, &mem, &queue), kArgumentPointer);
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&cfg, &mem, nullptr), kArgumentPointer);
    ComputeConfigInfo badSize = cfg;
    badSize.thisSize = 4;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&badSize, &mem, &queue), kStructSize);
    ComputeConfigInfo reserved = cfg;
    reserved.r1 = 1;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&reserved, &mem, &queue), kConfigInfo);
    ComputeConfigInfo pipe = cfg;
    pipe.pipe = 5;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&pipe, &mem, &queue), kComputePipeId);
    ComputeConfigInfo qid = cfg;
    qid.queue = 8;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&qid, &mem, &queue), kComputeQueueId);
    ComputeMemoryInfo small = mem;
    small.cpuGpuMemorySize = (16ull << 20) - 1;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&cfg, &small, &queue), kMemorySize);
    ComputeMemoryInfo noMem = mem;
    noMem.cpuGpuMemory = 0;
    EXPECT_EQ(sceVideodec2AllocateComputeQueue(&cfg, &noMem, &queue), kMemoryPointer);
    ASSERT_EQ(sceVideodec2AllocateComputeQueue(&cfg, &mem, &queue), 0);
    EXPECT_EQ(queue, mem.cpuGpuMemory);
    EXPECT_EQ(sceVideodec2ReleaseComputeQueue(queue), 0);
    EXPECT_EQ(sceVideodec2ReleaseComputeQueue(0), kComputeQueueId);
}

// QueryDecoderMemoryInfo and CreateDecoder validate the config in the oracle's order. Only AVC is
// supported: HEVC (974921) and every other codec type is CODEC_TYPE, never a decoder that returns
// black pictures.
TEST_F(Videodec2Test, DecoderConfigValidation) {
    DecoderSetup ok;
    DecoderMemoryInfo query{sizeof(query)};
    EXPECT_EQ(sceVideodec2QueryDecoderMemoryInfo(nullptr, &query), kArgumentPointer);
    EXPECT_EQ(sceVideodec2QueryDecoderMemoryInfo(&ok.config, nullptr), kArgumentPointer);
    ASSERT_EQ(sceVideodec2QueryDecoderMemoryInfo(&ok.config, &query), 0);
    EXPECT_EQ(query.cpuSize, 16ull << 20);
    EXPECT_EQ(query.maxFrameBuffer, 16ull << 20);
    EXPECT_EQ(query.alignment, 0x100u);

    auto bad = [&](auto mutate, int expected, const char* what) {
        SCOPED_TRACE(what);
        DecoderSetup s;
        mutate(s);
        DecoderMemoryInfo q{sizeof(q)};
        EXPECT_EQ(sceVideodec2QueryDecoderMemoryInfo(&s.config, &q), expected);
        std::uint64_t h = 0;
        EXPECT_EQ(sceVideodec2CreateDecoder(&s.config, &s.memory, &h), expected);
        EXPECT_EQ(h, 0u);
    };
    bad([](DecoderSetup& s) { s.config.thisSize = 8; }, kStructSize, "config size");
    bad([](DecoderSetup& s) { s.config.resourceType = 2; }, kResourceType, "resource type");
    bad([](DecoderSetup& s) { s.config.codecType = 974921; }, kCodecType, "HEVC");
    bad([](DecoderSetup& s) { s.config.codecType = 2; }, kCodecType, "other codec");
    bad([](DecoderSetup& s) { s.config.r0 = 1; }, kConfigInfo, "reserved");
    bad([](DecoderSetup& s) { s.config.queueDepth = 0; }, kInputQueueDepth, "queue depth");
    bad([](DecoderSetup& s) { s.config.maxDpb = 0; }, kDpbFrameCount, "dpb zero");
    bad([](DecoderSetup& s) { s.config.maxDpb = -2; }, kDpbFrameCount, "dpb < -1");
    bad([](DecoderSetup& s) { s.config.maxW = 0; }, kFrameWidthHeight, "width zero");
    bad([](DecoderSetup& s) { s.config.maxH = -2; }, kFrameWidthHeight, "height < -1");

    // Create-only checks.
    DecoderSetup s;
    std::uint64_t h = 0;
    EXPECT_EQ(sceVideodec2CreateDecoder(nullptr, &s.memory, &h), kArgumentPointer);
    EXPECT_EQ(sceVideodec2CreateDecoder(&s.config, &s.memory, nullptr), kArgumentPointer);
    DecoderSetup noQueue;
    noQueue.config.computeQueue = 0;
    EXPECT_EQ(sceVideodec2CreateDecoder(&noQueue.config, &noQueue.memory, &h), kComputeQueue);
    DecoderSetup memSize;
    memSize.memory.gpuSize = 1;
    EXPECT_EQ(sceVideodec2CreateDecoder(&memSize.config, &memSize.memory, &h), kMemorySize);
    DecoderSetup memSize2;
    memSize2.memory.maxFrameBuffer = 1;
    EXPECT_EQ(sceVideodec2CreateDecoder(&memSize2.config, &memSize2.memory, &h), kMemorySize);
    DecoderSetup memPtr;
    memPtr.memory.cpu = 0;
    EXPECT_EQ(sceVideodec2CreateDecoder(&memPtr.config, &memPtr.memory, &h), kMemoryPointer);
    DecoderSetup memStruct;
    memStruct.memory.thisSize = 8;
    EXPECT_EQ(sceVideodec2CreateDecoder(&memStruct.config, &memStruct.memory, &h), kStructSize);
}

// Without the host decoder, CreateDecoder fails with API_FAIL (documented), never returning a
// decoder that would produce invented pictures. With it, create/delete/delete-again behave.
TEST_F(Videodec2Test, CreateAndDeleteLifecycle) {
    DecoderSetup s;
    const int rc = Create(s);
    if (!Videodec2::H264DecoderAvailable()) {
        EXPECT_EQ(rc, kApiFail);
        EXPECT_EQ(handle_, 0u);
        return;
    }
    ASSERT_EQ(rc, 0);
    ASSERT_NE(handle_, 0u);
    EXPECT_EQ(sceVideodec2DeleteDecoder(handle_), 0);
    EXPECT_EQ(sceVideodec2DeleteDecoder(handle_), kDecoderInstance);
    EXPECT_EQ(sceVideodec2Reset(handle_), kDecoderInstance);
    EXPECT_EQ(sceVideodec2DeleteDecoder(0), kDecoderInstance);
    handle_ = 0;
}

// Decode argument validation in the oracle's order: instance first, then pointers, struct sizes
// (input, frame buffer, output - 48 and 56 bytes accepted), access unit size/pointer, frame buffer
// size/pointer.
TEST_F(Videodec2Test, DecodeAndFlushArgumentErrors) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 1))});
    std::vector<std::uint8_t> fb(1 << 16);
    InputData in{sizeof(in), reinterpret_cast<std::uintptr_t>(au.data()), au.size(), 1, 0, 0};
    FrameBuffer frame{sizeof(frame), reinterpret_cast<std::uintptr_t>(fb.data()), fb.size(), 0, {}};
    OutputInfo out{};
    out.thisSize = sizeof(out);

    EXPECT_EQ(sceVideodec2Decode(handle_ + 1, &in, &frame, &out), kDecoderInstance);
    EXPECT_EQ(sceVideodec2Decode(handle_, nullptr, &frame, &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, nullptr, &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &frame, nullptr), kArgumentPointer);
    InputData badIn = in;
    badIn.thisSize = 8;
    EXPECT_EQ(sceVideodec2Decode(handle_, &badIn, &frame, &out), kStructSize);
    FrameBuffer badFrame = frame;
    badFrame.thisSize = 8;
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &badFrame, &out), kStructSize);
    OutputInfo badOut = out;
    badOut.thisSize = 40;
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &frame, &badOut), kStructSize);
    InputData noSize = in;
    noSize.auSize = 0;
    EXPECT_EQ(sceVideodec2Decode(handle_, &noSize, &frame, &out), kAccessUnitSize);
    InputData noData = in;
    noData.auData = 0;
    EXPECT_EQ(sceVideodec2Decode(handle_, &noData, &frame, &out), kAccessUnitPointer);
    FrameBuffer noFbSize = frame;
    noFbSize.size = 0;
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &noFbSize, &out), kFrameBufferSize);
    FrameBuffer noFb = frame;
    noFb.buffer = 0;
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &noFb, &out), kFrameBufferPointer);

    // Flush shares the frame/output checks.
    EXPECT_EQ(sceVideodec2Flush(handle_ + 1, &frame, &out), kDecoderInstance);
    EXPECT_EQ(sceVideodec2Flush(handle_, nullptr, &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Flush(handle_, &badFrame, &out), kStructSize);
    EXPECT_EQ(sceVideodec2Flush(handle_, &noFbSize, &out), kFrameBufferSize);
    EXPECT_EQ(sceVideodec2Flush(handle_, &noFb, &out), kFrameBufferPointer);

    // A 48-byte (older SDK) output struct is accepted and its missing tail is left alone.
    OutputInfo old{};
    old.thisSize = 48;
    old.frameFormat = 0x5A5A5A5A;
    old.pitchInBytes = 0x5A5A5A5A;
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &frame, &old), 0);
    EXPECT_EQ(old.frameFormat, 0x5A5A5A5Au);
    EXPECT_EQ(old.pitchInBytes, 0x5A5A5A5Au);
}

// An IDR decodes into the guest frame buffer as NV12 with a 256-byte pitch, chroma right after the
// luma rows, and zeroed padding; the reply carries codec, size, pitch and the buffer address.
TEST_F(Videodec2Test, DecodeWritesNv12WithPitch256) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;
    const Planes planes = Pattern(g, 9);
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, planes)});
    std::vector<std::uint8_t> fb(256 * 48 + 64, 0xEE);  // Exactly pitch * (32 + 16) rows, plus a sentinel tail.
    const std::size_t exact = 256 * 48;
    OutputInfo out{};
    ASSERT_EQ(Decode(au, fb, out, 77, 0x1234), 0);
    EXPECT_TRUE(accepted_);
    ASSERT_EQ(out.valid, 1);
    EXPECT_EQ(out.errorFrame, 0);
    EXPECT_EQ(out.pictureCount, 1);
    EXPECT_EQ(out.codecType, 1u);
    EXPECT_EQ(out.width, 32u);
    EXPECT_EQ(out.height, 32u);
    EXPECT_EQ(out.pitch, 256u);
    EXPECT_EQ(out.pitchInBytes, 256u);
    EXPECT_EQ(out.frameFormat, 0u);
    EXPECT_EQ(out.frameBuffer, reinterpret_cast<std::uintptr_t>(fb.data()));
    for (unsigned r = 0; r < 32; ++r) {
        EXPECT_EQ(0, std::memcmp(&fb[r * 256], &planes.y[r * 32], 32)) << "luma row " << r;
        for (unsigned c = 32; c < 256; ++c) ASSERT_EQ(fb[r * 256 + c], 0) << "luma pad " << c << "," << r;
    }
    const std::size_t chroma = 256 * 32;
    for (unsigned r = 0; r < 16; ++r) {
        for (unsigned c = 0; c < 16; ++c) {
            ASSERT_EQ(fb[chroma + r * 256 + 2 * c], planes.cb[r * 16 + c]) << "cb " << c << "," << r;
            ASSERT_EQ(fb[chroma + r * 256 + 2 * c + 1], planes.cr[r * 16 + c]) << "cr " << c << "," << r;
        }
        for (unsigned c = 32; c < 256; ++c) ASSERT_EQ(fb[chroma + r * 256 + c], 0);
    }
    for (std::size_t i = exact; i < fb.size(); ++i) ASSERT_EQ(fb[i], 0xEE) << "wrote past the NV12 area at " << i;
}

// A frame buffer too small for the picture is FRAME_BUFFER_SIZE and nothing is written; the picture
// is kept, so retrying with a large enough buffer returns it (it is not lost).
TEST_F(Videodec2Test, SmallFrameBufferKeepsThePicture) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;
    const Planes planes = Pattern(g, 10);
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, planes)});
    std::vector<std::uint8_t> small(256 * 48 - 1, 0xEE);
    OutputInfo out{};
    EXPECT_EQ(Decode(au, small, out, 5), kFrameBufferSize);
    EXPECT_FALSE(accepted_);
    EXPECT_EQ(out.valid, 0);
    for (std::uint8_t b : small) ASSERT_EQ(b, 0xEE);

    std::vector<std::uint8_t> big(256 * 48, 0);
    const InputData none{sizeof(InputData), 0, 0, 0, 0, 0};
    (void)none;
    FrameBuffer frame{sizeof(frame), reinterpret_cast<std::uintptr_t>(big.data()), big.size(), 0, {}};
    OutputInfo again{};
    again.thisSize = sizeof(again);
    ASSERT_EQ(sceVideodec2Flush(handle_, &frame, &again), 0);
    ASSERT_EQ(again.valid, 1);
    EXPECT_EQ(0, std::memcmp(big.data(), planes.y.data(), 32));
}

// A picture larger than the configured maximum is OVERSIZE_DECODE and is dropped.
TEST_F(Videodec2Test, OversizePictureIsRejected) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    s.config.maxW = 16;
    s.config.maxH = 16;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;  // 32x32 > 16x16
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 11))});
    std::vector<std::uint8_t> fb(1 << 16);
    OutputInfo out{};
    EXPECT_EQ(Decode(au, fb, out), kOversizeDecode);
    EXPECT_EQ(out.valid, 0);
}

// Bytes that are not Annex B are rejected as ACCESS_UNIT (not silently ignored), and the decoder
// keeps working afterwards.
TEST_F(Videodec2Test, BadAccessUnitIsRejected) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const std::vector<std::uint8_t> avcc = {0, 0, 0, 5, 0x67, 1, 2, 3, 4};
    std::vector<std::uint8_t> fb(1 << 16);
    OutputInfo out{};
    EXPECT_EQ(Decode(avcc, fb, out), kAccessUnit);
    EXPECT_EQ(out.valid, 0);
    const Geometry g;
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 12))});
    EXPECT_EQ(Decode(au, fb, out), 0);
    EXPECT_EQ(out.valid, 1);
}

// GetPictureInfo reports what the decoder was told: pts/dts/attached, IDR flag, profile 66 / level
// 10 and the macroblock dimensions minus one. A 104-byte request (older SDK) is accepted, other
// sizes are STRUCT_SIZE, an unknown buffer is OUTPUT_INFO, and the optional second block is zeroed.
TEST_F(Videodec2Test, PictureInfoReportsStreamMetadata) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 13))});
    std::vector<std::uint8_t> fb(256 * 48);
    OutputInfo out{};
    ASSERT_EQ(Decode(au, fb, out, 4000, 0xFEED), 0);
    ASSERT_EQ(out.valid, 1);

    std::vector<std::uint8_t> info(120, 0xCC);
    std::uint64_t size = 120;
    std::memcpy(info.data(), &size, 8);
    std::vector<std::uint8_t> second(64, 0xCC);
    std::uint64_t secondSize = 64;
    std::memcpy(second.data(), &secondSize, 8);
    ASSERT_EQ(sceVideodec2GetPictureInfo(&out, info.data(), second.data()), 0);
    auto u64 = [&](std::size_t off) { std::uint64_t v; std::memcpy(&v, &info[off], 8); return v; };
    auto u32 = [&](std::size_t off) { std::uint32_t v; std::memcpy(&v, &info[off], 4); return v; };
    EXPECT_EQ(u64(0), 120u);
    EXPECT_EQ(info[8], 1);
    EXPECT_EQ(u64(16), 4000u);
    EXPECT_EQ(u64(24), 3999u);
    EXPECT_EQ(u64(32), 0xFEEDu);
    EXPECT_EQ(info[40], 1);   // idr_picture_flag
    EXPECT_EQ(info[41], 66);  // profile_idc
    EXPECT_EQ(info[42], 10);  // level_idc
    EXPECT_EQ(u32(44), 1u);   // pic_width_in_mbs_minus1
    EXPECT_EQ(u32(48), 1u);   // pic_height_in_map_units_minus1
    EXPECT_EQ(info[52], 1);   // frame_mbs_only_flag
    for (std::size_t i = 8; i < second.size(); ++i) ASSERT_EQ(second[i], 0) << "second block byte " << i;

    // Same data through the AVC-specific export.
    std::vector<std::uint8_t> viaAvc(120, 0);
    std::memcpy(viaAvc.data(), &size, 8);
    ASSERT_EQ(sceVideodec2GetAvcPictureInfo(&out, viaAvc.data(), nullptr), 0);
    EXPECT_EQ(0, std::memcmp(viaAvc.data() + 8, info.data() + 8, 112));

    std::vector<std::uint8_t> old(104, 0);
    std::uint64_t oldSize = 104;
    std::memcpy(old.data(), &oldSize, 8);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&out, old.data(), nullptr), 0);
    std::uint64_t wrong = 100;
    std::memcpy(info.data(), &wrong, 8);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&out, info.data(), nullptr), kStructSize);
    secondSize = 8;
    std::memcpy(second.data(), &secondSize, 8);
    std::memcpy(info.data(), &size, 8);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&out, info.data(), second.data()), kStructSize);

    EXPECT_EQ(sceVideodec2GetPictureInfo(nullptr, info.data(), nullptr), kArgumentPointer);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&out, nullptr, nullptr), kArgumentPointer);
    OutputInfo badSize = out;
    badSize.thisSize = 8;
    EXPECT_EQ(sceVideodec2GetPictureInfo(&badSize, info.data(), nullptr), kStructSize);
    OutputInfo noPicture = out;
    noPicture.valid = 0;
    EXPECT_EQ(sceVideodec2GetPictureInfo(&noPicture, info.data(), nullptr), kOutputInfo);
    OutputInfo unknown = out;
    unknown.frameBuffer += 1;
    EXPECT_EQ(sceVideodec2GetPictureInfo(&unknown, info.data(), nullptr), kOutputInfo);

    // Reset forgets the pictures of that decoder.
    ASSERT_EQ(sceVideodec2Reset(handle_), 0);
    std::memcpy(info.data(), &size, 8);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&out, info.data(), nullptr), kOutputInfo);
}

// Review regression: a cropped SPS (30x28 inside a 32x32 grid) must be reported with the crop
// offsets recovered from the grid padding, and a stream without VUI colour data must not claim a
// colour description (value 2 is "unspecified" too). Fails if the offsets stay zero.
TEST_F(Videodec2Test, PictureInfoReportsCropAndNoColourDescription) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    Geometry g;
    g.cropRight = 1;   // 2 luma samples
    g.cropBottom = 2;  // 4 luma samples
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 3))});
    std::vector<std::uint8_t> fb(256 * 48);
    OutputInfo out{};
    ASSERT_EQ(Decode(au, fb, out, 1, 1), 0);
    ASSERT_EQ(out.valid, 1);
    ASSERT_EQ(out.width, 30u);
    ASSERT_EQ(out.height, 28u);

    std::vector<std::uint8_t> info(120, 0);
    const std::uint64_t size = 120;
    std::memcpy(info.data(), &size, 8);
    ASSERT_EQ(sceVideodec2GetPictureInfo(&out, info.data(), nullptr), 0);
    auto u32 = [&](std::size_t off) { std::uint32_t v; std::memcpy(&v, &info[off], 4); return v; };
    EXPECT_EQ(info[53], 1);  // frame_cropping_flag
    EXPECT_EQ(u32(56), 0u);  // left
    EXPECT_EQ(u32(60), 1u);  // right
    EXPECT_EQ(u32(64), 0u);  // top
    EXPECT_EQ(u32(68), 2u);  // bottom
}

// Review regression: unreadable or unwritable guest pointers are ARGUMENT_POINTER, never a fault.
TEST_F(Videodec2Test, DecodeProbesGuestStructPointers) {
    REQUIRE_HOST_DECODER();
#ifndef _WIN32
    // Off Windows GuestRangeUsable only rejects null (bean portps5-8l0d), so address 0x10 would fault.
    GTEST_SKIP() << "GuestRangeUsable cannot detect unmapped pointers off Windows";
#endif
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    const Geometry g;
    const auto au = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 1))});
    std::vector<std::uint8_t> fb(1 << 16);
    InputData in{sizeof(in), reinterpret_cast<std::uintptr_t>(au.data()), au.size(), 1, 0, 0};
    FrameBuffer frame{sizeof(frame), reinterpret_cast<std::uintptr_t>(fb.data()), fb.size(), 0, {}};
    OutputInfo out{};
    out.thisSize = sizeof(out);
    auto* bad = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10));
    EXPECT_EQ(sceVideodec2Decode(handle_, static_cast<const InputData*>(bad), &frame, &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, static_cast<FrameBuffer*>(bad), &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Decode(handle_, &in, &frame, static_cast<OutputInfo*>(bad)), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Flush(handle_, static_cast<FrameBuffer*>(bad), &out), kArgumentPointer);
    EXPECT_EQ(sceVideodec2Flush(handle_, &frame, static_cast<OutputInfo*>(bad)), kArgumentPointer);
}

// Flush with nothing pending produces no picture and no error; after Flush the decoder accepts a
// new stream; the picture-info table is bounded (old entries are forgotten, not leaked).
TEST_F(Videodec2Test, FlushWithNothingPendingAndPictureTableIsBounded) {
    REQUIRE_HOST_DECODER();
    DecoderSetup s;
    ASSERT_EQ(Create(s), 0);
    std::vector<std::uint8_t> fb(256 * 48);
    FrameBuffer frame{sizeof(frame), reinterpret_cast<std::uintptr_t>(fb.data()), fb.size(), 1, {}};
    OutputInfo out{};
    out.thisSize = sizeof(out);
    ASSERT_EQ(sceVideodec2Flush(handle_, &frame, &out), 0);
    EXPECT_EQ(out.valid, 0);
    EXPECT_EQ(frame.accepted, 0);

    const Geometry g;
    const auto idr = Join({Sps(g), Pps(), IdrPcm(g, Pattern(g, 14))});
    // 200 distinct frame buffers: only the most recent ones stay queryable.
    std::vector<std::vector<std::uint8_t>> buffers(200, std::vector<std::uint8_t>(256 * 48));
    OutputInfo first{};
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        OutputInfo o{};
        ASSERT_EQ(Decode(idr, buffers[i], o, i + 1), 0);
        ASSERT_EQ(o.valid, 1) << i;
        if (i == 0) first = o;
    }
    std::vector<std::uint8_t> info(120, 0);
    std::uint64_t size = 120;
    std::memcpy(info.data(), &size, 8);
    EXPECT_EQ(sceVideodec2GetPictureInfo(&first, info.data(), nullptr), kOutputInfo);
}

}  // namespace
