// Unit tests for the AJM batch plumbing and ATRAC9 instance handling
// (docs/spec/audio.md M1 row). Synthetic batches only, no game data: the
// assertions cover job parsing, return codes, and sideband layouts. Decoding
// a project-generated stream stays local-only until an encoder licence
// allows it.

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAjm.native/src/AjmInternal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void Require(bool value) {
    if (!value) std::abort();
}
#define REQUIRE(cond) Require(cond)

}  // namespace

extern "C" {
int APS5_VABI sceAjmInitialize(int64_t reserved, uint32_t* context) noexcept;
int APS5_VABI sceAjmFinalize(uint32_t context) noexcept;
int APS5_VABI sceAjmInstanceCreate(uint32_t context, uint32_t codec, uint64_t flags, uint32_t* instance) noexcept;
int APS5_VABI sceAjmInstanceDestroy(uint32_t context, uint32_t instance) noexcept;
int APS5_VABI sceAjmDecAt9ParseConfigData(const void* config_data, AjmDecAt9ConfigDataInfo* config_info) noexcept;
int APS5_VABI sceAjmBatchInitialize(void* buffer, size_t size, AjmBatchInfo* info) noexcept;
int APS5_VABI sceAjmBatchJobInitialize(AjmBatchInfo* info, uint32_t instance, const void* codec_parameters, size_t codec_parameters_size, void* result) noexcept;
int APS5_VABI sceAjmBatchJobClearContext(AjmBatchInfo* info, uint32_t instance, void* result) noexcept;
int APS5_VABI sceAjmBatchJobSetGaplessDecode(AjmBatchInfo* info, uint32_t instance, const void* gapless_decode, int reset, void* result) noexcept;
int APS5_VABI sceAjmBatchJobRunSplit(AjmBatchInfo* info, uint32_t instance, uint64_t flags, const AjmBuffer* input_buffers, size_t input_buffers_num, const AjmBuffer* output_buffers, size_t output_buffers_num, void* sideband_output, size_t sideband_output_size) noexcept;
int APS5_VABI sceAjmBatchJobGetStatistics(AjmBatchInfo* info, float interval, void* result) noexcept;
int APS5_VABI sceAjmBatchStart(uint32_t context, const AjmBatchInfo* info, int priority, AjmBatchError* error, uint32_t* batch) noexcept;
int APS5_VABI sceAjmBatchWait(uint32_t context, uint32_t batch, uint32_t timeout, AjmBatchError* error) noexcept;
int APS5_VABI sceAjmBatchErrorDump(const AjmBatchInfo* info, AjmBatchError* error) noexcept;
}

static void TestPureHelpers() {
    // Speaker masks for each decoded channel count.
    REQUIRE(AjmChannelMask(1) == 0x4);
    REQUIRE(AjmChannelMask(2) == 0x3);
    REQUIRE(AjmChannelMask(4) == 0x33);
    REQUIRE(AjmChannelMask(6) == 0x3F);
    REQUIRE(AjmChannelMask(8) == 0x63F);
    REQUIRE(AjmChannelMask(3) == 0);
    REQUIRE(AjmChannelMask(0) == 0);

    // RIFF/WAVE header skipping up to the data payload.
    const std::uint8_t bare[] = {0x01, 0x02, 0x03};
    REQUIRE(AjmRiffDataOffset(bare, sizeof(bare)) == 0);
    REQUIRE(AjmRiffDataOffset(nullptr, 0) == 0);
    const std::uint8_t riff[] = {
        'R', 'I', 'F', 'F', 0x24, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 0x10, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        'd', 'a', 't', 'a', 0x04, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD,
    };
    REQUIRE(AjmRiffDataOffset(riff, sizeof(riff)) == 44);
    // Truncated headers are not headers.
    REQUIRE(AjmRiffDataOffset(riff, 11) == 0);

    // Batch append: null storage and overflow report errors, the happy path
    // advances the used-bytes field.
    AjmJobHeader header = AjmMakeHeader(AjmJobKind::Run, 7, nullptr, 0);
    REQUIRE(header.kind == AjmJobKind::Run && header.instance == 7);
    REQUIRE(AjmAppend(nullptr, header, nullptr, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    AjmBatchInfo nullBuf{};
    nullBuf.p_buffer = nullptr;
    nullBuf.size = 64;
    REQUIRE(AjmAppend(&nullBuf, header, nullptr, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint8_t tiny[8] = {};
    AjmBatchInfo small{};
    small.p_buffer = tiny;
    small.size = sizeof(tiny);
    REQUIRE(AjmAppend(&small, header, nullptr, nullptr) == SCE_AJM_ERROR_OUT_OF_RESOURCES);
    std::uint8_t storage[1024] = {};
    AjmBatchInfo info{};
    info.p_buffer = storage;
    info.size = sizeof(storage);
    REQUIRE(AjmAppend(&info, header, nullptr, nullptr) == 0);
    REQUIRE(info.offset == sizeof(AjmJobHeader));
}

static void TestLifecycle() {
    REQUIRE(sceAjmInitialize(0, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint32_t context = 0;
    REQUIRE(sceAjmInitialize(0, &context) == 0);
    REQUIRE(context != 0);

    REQUIRE(sceAjmInstanceCreate(context, 1, 0, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint32_t at9 = 0;
    REQUIRE(sceAjmInstanceCreate(context, AJM_CODEC_AT9, 0, &at9) == 0);
    REQUIRE(at9 != 0);
    // Unknown codecs still create; their jobs fail per job below.
    std::uint32_t other = 0;
    REQUIRE(sceAjmInstanceCreate(context, 7, 0, &other) == 0);
    REQUIRE(other != 0 && other != at9);

    REQUIRE(sceAjmInstanceDestroy(context, 0xFFFFFFFFu) == SCE_AJM_ERROR_INVALID_INSTANCE);
    REQUIRE(sceAjmInstanceDestroy(context, at9) == 0);
    REQUIRE(sceAjmInstanceDestroy(context, other) == 0);
    REQUIRE(sceAjmFinalize(context) == 0);

    REQUIRE(sceAjmDecAt9ParseConfigData(nullptr, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    AjmDecAt9ConfigDataInfo parsed{};
    REQUIRE(sceAjmDecAt9ParseConfigData(nullptr, &parsed) == SCE_AJM_ERROR_INVALID_PARAMETER);
}

static void TestSyntheticBatch() {
    std::uint32_t context = 0;
    REQUIRE(sceAjmInitialize(0, &context) == 0);
    std::uint32_t at9 = 0;
    REQUIRE(sceAjmInstanceCreate(context, AJM_CODEC_AT9, 0, &at9) == 0);

    REQUIRE(sceAjmBatchInitialize(nullptr, 0, nullptr) == SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint8_t storage[4096] = {};
    AjmBatchInfo info{};
    REQUIRE(sceAjmBatchInitialize(storage, sizeof(storage), nullptr) ==
            SCE_AJM_ERROR_INVALID_PARAMETER);
    REQUIRE(sceAjmBatchInitialize(storage, sizeof(storage), &info) == 0);

    // An initialize job with no parameters cannot configure the decoder, so
    // the following run reports "not initialized" instead of decoding.
    std::int32_t initResult = -1;
    REQUIRE(sceAjmBatchJobInitialize(&info, at9, nullptr, 0, &initResult) == 0);
    std::uint8_t fakeInput[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    std::uint8_t pcm[4096] = {};
    AjmBuffer in{ fakeInput, sizeof(fakeInput) };
    AjmBuffer out{ pcm, sizeof(pcm) };
    std::int32_t runResult[2] = {-1, -1};
    REQUIRE(sceAjmBatchJobRunSplit(&info, at9, 0, &in, 1, &out, 1, runResult, sizeof(runResult)) == 0);
    std::uint8_t stats[24] = {};
    REQUIRE(sceAjmBatchJobGetStatistics(&info, 0.0f, stats) == 0);

    AjmBatchError error{};
    std::uint32_t batch = 0;
    REQUIRE(sceAjmBatchStart(0, nullptr, 0, nullptr, nullptr) ==
            SCE_AJM_ERROR_INVALID_PARAMETER);
    REQUIRE(sceAjmBatchStart(context, &info, 0, &error, &batch) == 0);
    REQUIRE(batch != 0);
    REQUIRE(initResult == AJM_RESULT_INVALID_PARAMETER);
    REQUIRE(runResult[0] == AJM_RESULT_NOT_INITIALIZED);
    // The statistics job zeroes its sideband.
    for (std::uint8_t byte : stats) REQUIRE(byte == 0);

    // BatchWait only reports completion (decode is synchronous), so any
    // timeout succeeds; ErrorDump zeroes its struct.
    REQUIRE(sceAjmBatchWait(context, batch, 0, &error) == 0);
    REQUIRE(sceAjmBatchWait(context, batch, 1000, nullptr) == 0);
    REQUIRE(sceAjmBatchErrorDump(&info, &error) == 0);
    REQUIRE(error.error_code == 0);

    // Clear-context and gapless jobs parse on an unknown instance too: they
    // report per-job parameter errors without touching the instance map.
    std::uint8_t storage2[1024] = {};
    AjmBatchInfo info2{};
    REQUIRE(sceAjmBatchInitialize(storage2, sizeof(storage2), &info2) == 0);
    std::int32_t clearResult = -1;
    REQUIRE(sceAjmBatchJobClearContext(&info2, 0xFFFFFFFFu, &clearResult) == 0);
    std::uint8_t gapless[8] = {};
    std::int32_t gaplessResult = -1;
    REQUIRE(sceAjmBatchJobSetGaplessDecode(&info2, 0xFFFFFFFFu, gapless, 1, &gaplessResult) == 0);
    REQUIRE(sceAjmBatchStart(context, &info2, 0, nullptr, &batch) == 0);
    REQUIRE(clearResult == AJM_RESULT_INVALID_PARAMETER);
    REQUIRE(gaplessResult == AJM_RESULT_INVALID_PARAMETER);

    REQUIRE(sceAjmInstanceDestroy(context, at9) == 0);
    REQUIRE(sceAjmFinalize(context) == 0);
}

static void TestUnknownCodec() {
    std::uint32_t context = 0;
    REQUIRE(sceAjmInitialize(0, &context) == 0);
    std::uint32_t instance = 0;
    REQUIRE(sceAjmInstanceCreate(context, 7, 0, &instance) == 0);

    std::uint8_t storage[1024] = {};
    AjmBatchInfo info{};
    REQUIRE(sceAjmBatchInitialize(storage, sizeof(storage), &info) == 0);
    std::int32_t initResult = -1;
    REQUIRE(sceAjmBatchJobInitialize(&info, instance, nullptr, 0, &initResult) == 0);
    std::uint8_t fakeInput[8] = {};
    std::uint8_t pcm[256] = {};
    AjmBuffer in{ fakeInput, sizeof(fakeInput) };
    AjmBuffer out{ pcm, sizeof(pcm) };
    // The sideband must hold the full result record (8 bytes); a smaller
    // buffer is left untouched by design.
    std::int32_t runResult[2] = {-1, -1};
    REQUIRE(sceAjmBatchJobRunSplit(&info, instance, 0, &in, 1, &out, 1, &runResult, sizeof(runResult)) == 0);
    std::uint32_t batch = 0;
    REQUIRE(sceAjmBatchStart(context, &info, 0, nullptr, &batch) == 0);
    REQUIRE(initResult == 0);
    // No decoder exists for the codec: the job reports a codec error and the
    // output stays untouched instead of silent zeros.
    REQUIRE(runResult[0] == AJM_RESULT_CODEC_ERROR);
    for (std::uint8_t byte : pcm) REQUIRE(byte == 0);

    REQUIRE(sceAjmInstanceDestroy(context, instance) == 0);
    REQUIRE(sceAjmFinalize(context) == 0);
}

int main() {
    TestPureHelpers();
    TestLifecycle();
    TestSyntheticBatch();
    TestUnknownCodec();
    return 0;
}
