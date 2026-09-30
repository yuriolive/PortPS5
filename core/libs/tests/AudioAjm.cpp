// AudioAjm.cpp
// PortPS5 - Audio Job Manager (AJM) GoogleTest Suite (Audio Subsystem M2)
//
// Purpose:
//   Validates the AJM batch plumbing, ATRAC9 instance lifecycle, channel masks,
//   RIFF header skipping, unknown codec handling, and death test verification
//   for malformed batch layouts.
//
// Invariants Verified:
//   - Channel masks match PS5 SDK speaker bitmasks.
//   - RIFF header offsets skip container metadata to raw ATRAC9 payloads.
//   - Batch construction validates capacity and updates buffer offsets.
//   - Malformed batches trigger immediate Unsupported() aborts via death tests.

#define SDL_MAIN_HANDLED
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAjm.native/src/AjmInternal.hpp"

extern "C" {
// Initializes AJM subsystem. Returns 0 on success.
int APS5_VABI sceAjmInitialize(int64_t reserved, uint32_t* context) noexcept;

// Finalizes AJM context. Returns 0 on success.
int APS5_VABI sceAjmFinalize(uint32_t context) noexcept;

// Creates a decoder instance. Returns 0 on success.
int APS5_VABI sceAjmInstanceCreate(uint32_t context, uint32_t codec, uint64_t flags, uint32_t* instance) noexcept;

// Destroys an instance. Returns 0 on success.
int APS5_VABI sceAjmInstanceDestroy(uint32_t context, uint32_t instance) noexcept;

// Parses ATRAC9 configuration header. Returns 0 on success.
int APS5_VABI sceAjmDecAt9ParseConfigData(const void* config_data, AjmDecAt9ConfigDataInfo* config_info) noexcept;

// Initializes an AJM batch descriptor. Returns 0 on success.
int APS5_VABI sceAjmBatchInitialize(void* buffer, size_t size, AjmBatchInfo* info) noexcept;

// Appends an initialize job to a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchJobInitialize(AjmBatchInfo* info, uint32_t instance, const void* codec_parameters, size_t codec_parameters_size, void* result) noexcept;

// Appends a clear-context job to a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchJobClearContext(AjmBatchInfo* info, uint32_t instance, void* result) noexcept;

// Appends a gapless decode job to a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchJobSetGaplessDecode(AjmBatchInfo* info, uint32_t instance, const void* gapless_decode, int reset, void* result) noexcept;

// Appends a split run decode job to a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchJobRunSplit(AjmBatchInfo* info, uint32_t instance, uint64_t flags, const AjmBuffer* input_buffers, size_t input_buffers_num, const AjmBuffer* output_buffers, size_t output_buffers_num, void* sideband_output, size_t sideband_output_size) noexcept;

// Appends a statistics query job to a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchJobGetStatistics(AjmBatchInfo* info, float interval, void* result) noexcept;

// Starts execution of an AJM batch. Returns 0 on success.
int APS5_VABI sceAjmBatchStart(uint32_t context, const AjmBatchInfo* info, int priority, AjmBatchError* error, uint32_t* batch) noexcept;

// Waits for batch completion. Returns 0 on success.
int APS5_VABI sceAjmBatchWait(uint32_t context, uint32_t batch, uint32_t timeout, AjmBatchError* error) noexcept;

// Dumps error information for a batch. Returns 0 on success.
int APS5_VABI sceAjmBatchErrorDump(const AjmBatchInfo* info, AjmBatchError* error) noexcept;
}

// Verifies speaker bitmasks, RIFF container header parsing, and batch append invariants.
TEST(AudioAjmTests, PureHelpers) {
    EXPECT_EQ(AjmChannelMask(1), 0x4u);
    EXPECT_EQ(AjmChannelMask(2), 0x3u);
    EXPECT_EQ(AjmChannelMask(4), 0x33u);
    EXPECT_EQ(AjmChannelMask(6), 0x3Fu);
    EXPECT_EQ(AjmChannelMask(8), 0x63Fu);
    EXPECT_EQ(AjmChannelMask(3), 0u);
    EXPECT_EQ(AjmChannelMask(0), 0u);

    const std::uint8_t bare[] = {0x01, 0x02, 0x03};
    EXPECT_EQ(AjmRiffDataOffset(bare, sizeof(bare)), 0u);
    EXPECT_EQ(AjmRiffDataOffset(nullptr, 0), 0u);
    const std::uint8_t riff[] = {
        'R', 'I', 'F', 'F', 0x24, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 0x10, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        'd', 'a', 't', 'a', 0x04, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD,
    };
    EXPECT_EQ(AjmRiffDataOffset(riff, sizeof(riff)), 44u);
    EXPECT_EQ(AjmRiffDataOffset(riff, 11), 0u);

    AjmJobHeader header = AjmMakeHeader(AjmJobKind::Run, 7, nullptr, 0);
    EXPECT_EQ(header.kind, AjmJobKind::Run);
    EXPECT_EQ(header.instance, 7u);
    EXPECT_EQ(AjmAppend(nullptr, header, nullptr, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    AjmBatchInfo nullBuf{};
    nullBuf.p_buffer = nullptr;
    nullBuf.size = 64;
    EXPECT_EQ(AjmAppend(&nullBuf, header, nullptr, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint8_t tiny[8] = {};
    AjmBatchInfo small{};
    small.p_buffer = tiny;
    small.size = sizeof(tiny);
    EXPECT_EQ(AjmAppend(&small, header, nullptr, nullptr), SCE_AJM_ERROR_OUT_OF_RESOURCES);
    std::uint8_t storage[1024] = {};
    AjmBatchInfo info{};
    info.p_buffer = storage;
    info.size = sizeof(storage);
    EXPECT_EQ(AjmAppend(&info, header, nullptr, nullptr), 0);
    EXPECT_EQ(info.offset, sizeof(AjmJobHeader));
}

// Verifies context and decoder instance creation, ATRAC9 configuration parsing, and destruction.
TEST(AudioAjmTests, Lifecycle) {
    EXPECT_EQ(sceAjmInitialize(0, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint32_t context = 0;
    EXPECT_EQ(sceAjmInitialize(0, &context), 0);
    EXPECT_NE(context, 0u);

    EXPECT_EQ(sceAjmInstanceCreate(context, 1, 0, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint32_t at9 = 0;
    EXPECT_EQ(sceAjmInstanceCreate(context, AJM_CODEC_AT9, 0, &at9), 0);
    EXPECT_NE(at9, 0u);

    std::uint32_t other = 0;
    EXPECT_EQ(sceAjmInstanceCreate(context, 7, 0, &other), 0);
    EXPECT_NE(other, 0u);
    EXPECT_NE(other, at9);

    EXPECT_EQ(sceAjmInstanceDestroy(context, 0xFFFFFFFFu), SCE_AJM_ERROR_INVALID_INSTANCE);
    EXPECT_EQ(sceAjmInstanceDestroy(context, at9), 0);
    EXPECT_EQ(sceAjmInstanceDestroy(context, other), 0);
    EXPECT_EQ(sceAjmFinalize(context), 0);

    EXPECT_EQ(sceAjmDecAt9ParseConfigData(nullptr, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    AjmDecAt9ConfigDataInfo parsed{};
    EXPECT_EQ(sceAjmDecAt9ParseConfigData(nullptr, &parsed), SCE_AJM_ERROR_INVALID_PARAMETER);
}

// Verifies synthetic command batch execution, job result sidebands, and batch completion.
TEST(AudioAjmTests, SyntheticBatch) {
    std::uint32_t context = 0;
    ASSERT_EQ(sceAjmInitialize(0, &context), 0);
    std::uint32_t at9 = 0;
    ASSERT_EQ(sceAjmInstanceCreate(context, AJM_CODEC_AT9, 0, &at9), 0);

    EXPECT_EQ(sceAjmBatchInitialize(nullptr, 0, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    std::uint8_t storage[4096] = {};
    AjmBatchInfo info{};
    EXPECT_EQ(sceAjmBatchInitialize(storage, sizeof(storage), nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(sceAjmBatchInitialize(storage, sizeof(storage), &info), 0);

    std::int32_t initResult = -1;
    EXPECT_EQ(sceAjmBatchJobInitialize(&info, at9, nullptr, 0, &initResult), 0);
    std::uint8_t fakeInput[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    std::uint8_t pcm[4096] = {};
    AjmBuffer in{ fakeInput, sizeof(fakeInput) };
    AjmBuffer out{ pcm, sizeof(pcm) };
    std::int32_t runResult[2] = {-1, -1};
    EXPECT_EQ(sceAjmBatchJobRunSplit(&info, at9, 0, &in, 1, &out, 1, runResult, sizeof(runResult)), 0);
    std::uint8_t stats[24] = {};
    EXPECT_EQ(sceAjmBatchJobGetStatistics(&info, 0.0f, stats), 0);

    AjmBatchError error{};
    std::uint32_t batch = 0;
    EXPECT_EQ(sceAjmBatchStart(0, nullptr, 0, nullptr, nullptr), SCE_AJM_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(sceAjmBatchStart(context, &info, 0, &error, &batch), 0);
    EXPECT_NE(batch, 0u);
    EXPECT_EQ(initResult, AJM_RESULT_INVALID_PARAMETER);
    EXPECT_EQ(runResult[0], AJM_RESULT_NOT_INITIALIZED);

    for (std::uint8_t byte : stats) {
        EXPECT_EQ(byte, 0u);
    }

    EXPECT_EQ(sceAjmBatchWait(context, batch, 0, &error), 0);
    EXPECT_EQ(sceAjmBatchWait(context, batch, 1000, nullptr), 0);
    EXPECT_EQ(sceAjmBatchErrorDump(&info, &error), 0);
    EXPECT_EQ(error.error_code, 0);

    std::uint8_t storage2[1024] = {};
    AjmBatchInfo info2{};
    EXPECT_EQ(sceAjmBatchInitialize(storage2, sizeof(storage2), &info2), 0);
    std::int32_t clearResult = -1;
    EXPECT_EQ(sceAjmBatchJobClearContext(&info2, 0xFFFFFFFFu, &clearResult), 0);
    std::uint8_t gapless[8] = {};
    std::int32_t gaplessResult = -1;
    EXPECT_EQ(sceAjmBatchJobSetGaplessDecode(&info2, 0xFFFFFFFFu, gapless, 1, &gaplessResult), 0);
    EXPECT_EQ(sceAjmBatchStart(context, &info2, 0, nullptr, &batch), 0);
    EXPECT_EQ(clearResult, AJM_RESULT_INVALID_PARAMETER);
    EXPECT_EQ(gaplessResult, AJM_RESULT_INVALID_PARAMETER);

    EXPECT_EQ(sceAjmInstanceDestroy(context, at9), 0);
    EXPECT_EQ(sceAjmFinalize(context), 0);
}

// Verifies unknown codec handling returns codec error without producing fabricated PCM.
TEST(AudioAjmTests, UnknownCodec) {
    std::uint32_t context = 0;
    ASSERT_EQ(sceAjmInitialize(0, &context), 0);
    std::uint32_t instance = 0;
    ASSERT_EQ(sceAjmInstanceCreate(context, 7, 0, &instance), 0);

    std::uint8_t storage[1024] = {};
    AjmBatchInfo info{};
    EXPECT_EQ(sceAjmBatchInitialize(storage, sizeof(storage), &info), 0);
    std::int32_t initResult = -1;
    EXPECT_EQ(sceAjmBatchJobInitialize(&info, instance, nullptr, 0, &initResult), 0);
    std::uint8_t fakeInput[8] = {};
    std::uint8_t pcm[256] = {};
    AjmBuffer in{ fakeInput, sizeof(fakeInput) };
    AjmBuffer out{ pcm, sizeof(pcm) };

    std::int32_t runResult[2] = {-1, -1};
    EXPECT_EQ(sceAjmBatchJobRunSplit(&info, instance, 0, &in, 1, &out, 1, &runResult, sizeof(runResult)), 0);
    std::uint32_t batch = 0;
    EXPECT_EQ(sceAjmBatchStart(context, &info, 0, nullptr, &batch), 0);
    EXPECT_EQ(initResult, 0);
    EXPECT_EQ(runResult[0], AJM_RESULT_CODEC_ERROR);
    for (std::uint8_t byte : pcm) {
        EXPECT_EQ(byte, 0u);
    }

    EXPECT_EQ(sceAjmInstanceDestroy(context, instance), 0);
    EXPECT_EQ(sceAjmFinalize(context), 0);
}

// Verifies that a malformed batch layout aborts via Unsupported() rather than corrupting memory.
TEST(AudioAjmTests, MalformedBatchAborts) {
    std::uint32_t context = 0;
    ASSERT_EQ(sceAjmInitialize(0, &context), 0);

    // Case 1: Truncated header in batch
    std::uint8_t truncatedBuf[sizeof(AjmJobHeader) - 2] = {};
    AjmBatchInfo truncatedInfo{};
    truncatedInfo.p_buffer = truncatedBuf;
    truncatedInfo.size = sizeof(truncatedBuf);
    truncatedInfo.offset = sizeof(truncatedBuf);
    std::uint32_t batch = 0;

    EXPECT_DEATH(
        { sceAjmBatchStart(context, &truncatedInfo, 0, nullptr, &batch); },
        "sceAjmBatchStart: truncated job header"
    );

    // Case 2: Corrupted job bytes field exceeding remaining buffer
    std::uint8_t corruptBuf[128] = {};
    AjmJobHeader corruptJob = AjmMakeHeader(AjmJobKind::Run, 1, nullptr, 0);
    corruptJob.bytes = 5000;
    std::memcpy(corruptBuf, &corruptJob, sizeof(corruptJob));
    AjmBatchInfo corruptInfo{};
    corruptInfo.p_buffer = corruptBuf;
    corruptInfo.size = sizeof(corruptBuf);
    corruptInfo.offset = sizeof(corruptBuf);

    EXPECT_DEATH(
        { sceAjmBatchStart(context, &corruptInfo, 0, nullptr, &batch); },
        "sceAjmBatchStart: invalid job bytes"
    );

    EXPECT_EQ(sceAjmFinalize(context), 0);
}

// Verifies concurrent decoder-instance creation hands out unique live ids.
// Ported from SharpEMU Audio/AjmExportsTests.ConcurrentInstanceCreates_ProduceUniqueLiveIds
// (GPL-2.0-or-later, used under GPL-2.0 terms; registry-shape and generation
// specifics from the original have no equivalent here and are not ported).
TEST(AudioAjmTests, ConcurrentInstanceCreates) {
    std::uint32_t context = 0;
    ASSERT_EQ(sceAjmInitialize(0, &context), 0);
    constexpr int kThreads = 8;
    constexpr int kPerThread = 16;
    std::mutex idsLock;
    std::set<std::uint32_t> ids;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kPerThread; i++) {
                std::uint32_t instance = 0;
                EXPECT_EQ(sceAjmInstanceCreate(context, AJM_CODEC_AT9, 0, &instance), 0);
                EXPECT_NE(instance, 0u);
                std::lock_guard lock(idsLock);
                ids.insert(instance);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(ids.size(), static_cast<std::size_t>(kThreads * kPerThread));
    for (auto instance : ids) {
        EXPECT_EQ(sceAjmInstanceDestroy(context, instance), 0);
    }
    EXPECT_EQ(sceAjmFinalize(context), 0);
}
