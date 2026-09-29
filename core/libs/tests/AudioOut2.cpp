// AudioOut2.cpp
// PortPS5 - AudioOut2 Context and AudioOut v1 GoogleTest Suite (Audio Subsystem M2)
//
// Purpose:
//   Validates AudioOut2 context/port lifecycle, channel decoding, pure helpers,
//   downmix matrix with LFE fold (-10 dB), telemetry aggregates, and AudioOut v1
//   return codes on synthetic buffers without audio hardware.
//
// Invariants Verified:
//   - Format decoding correctly extracts channels from data_format bits 8..11.
//   - Downmixing folds mono to stereo, maps stereo, and folds 8ch with LFE at -10 dB.
//   - Queue level and latency math conform to target specifications.
//   - Context and port lifecycle methods return correct POSIX/SCE error codes.

#define SDL_MAIN_HANDLED
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAudioOut/src/AudioOut2Internal.hpp"

extern "C" {
// Advances context timeline. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextAdvance(AudioOut2ContextHandle ctx) noexcept;

// Creates an AudioOut2 context. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextCreate(const AudioOut2ContextParam* params, void* buffer, size_t buffer_size, AudioOut2ContextHandle* ctx) noexcept;

// Destroys an AudioOut2 context. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle ctx) noexcept;

// Queries queue level and available slots. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextGetQueueLevel(AudioOut2ContextHandle ctx, uint32_t* queue_level, uint32_t* available_queue) noexcept;

// Pushes mixed grain to mixer queue. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextPush(AudioOut2ContextHandle ctx, uint32_t blocking) noexcept;

// Queries required memory size for context. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextQueryMemory(const AudioOut2ContextParam* params, size_t* memory_size) noexcept;

// Resets context parameter struct. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextResetParam(AudioOut2ContextParam* params) noexcept;

// Sets context rendering attributes. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextSetAttributes(AudioOut2ContextHandle ctx, const AudioOut2Attribute* attributes, uint32_t num) noexcept;

// Creates an AudioOut2 port. Returns 0 on success.
int APS5_VABI sceAudioOut2PortCreate(AudioOut2ContextHandle ctx, const AudioOut2PortParam* params, AudioOut2PortHandle* port) noexcept;

// Destroys an AudioOut2 port. Returns 0 on success.
int APS5_VABI sceAudioOut2PortDestroy(AudioOut2PortHandle port) noexcept;

// Queries port runtime state. Returns 0 on success.
int APS5_VABI sceAudioOut2PortGetState(AudioOut2PortHandle port, AudioOut2PortState* state) noexcept;

// Sets port PCM buffer and volume attributes. Returns 0 on success.
int APS5_VABI sceAudioOut2PortSetAttributes(AudioOut2PortHandle port, const AudioOut2Attribute* attributes, uint32_t num) noexcept;

// Initializes AudioOut2 subsystem. Returns 0 on success.
int APS5_VABI sceAudioOut2Initialize(void) noexcept;

// Queries AudioOut2 system state. Returns 0 on success.
int APS5_VABI sceAudioOut2GetSystemState(AudioOut2SystemState* state) noexcept;

// Creates an AudioOut2 user slot. Returns 0 on success.
int APS5_VABI sceAudioOut2UserCreate(uint32_t user_id, AudioOut2UserHandle* handle) noexcept;

// Destroys an AudioOut2 user slot. Returns 0 on success.
int APS5_VABI sceAudioOut2UserDestroy(AudioOut2UserHandle handle) noexcept;

// Queries speaker angles and layout. Returns 0 on success.
int APS5_VABI sceAudioOut2GetSpeakerInfo(AudioOut2SpeakerInfo* info, uint32_t flags) noexcept;

// Initializes AudioOut v1 subsystem. Returns 0 on success.
int APS5_VABI sceAudioOutInit() noexcept;

// Opens AudioOut v1 port. Returns port handle or error code.
int APS5_VABI sceAudioOutOpen(int userId, int type, int index, std::uint32_t len, std::uint32_t freq, std::uint32_t param) noexcept;

// Closes AudioOut v1 port. Returns 0 on success.
int APS5_VABI sceAudioOutClose(int handle) noexcept;

// Outputs audio on AudioOut v1 port. Returns sample count or error.
int APS5_VABI sceAudioOutOutput(int handle, const void* ptr) noexcept;

// Outputs audio simultaneously across multiple v1 ports. Returns sample count.
int APS5_VABI sceAudioOutOutputs(AudioOutOutputParam* param, std::uint32_t num) noexcept;

// Sets per-channel volume on AudioOut v1 port. Returns 0 on success.
int APS5_VABI sceAudioOutSetVolume(int handle, std::uint32_t flag, int* vol) noexcept;

// Queries port state on AudioOut v1 port. Returns 0 on success.
int APS5_VABI sceAudioOutGetPortState(int handle, AudioOutPortState* state) noexcept;
}

namespace {

AudioOut2ContextHandle MakeWallClockContext(std::uint32_t depth, std::uint32_t grain) {
    AudioOut2ContextParam params{};
    EXPECT_EQ(sceAudioOut2ContextResetParam(&params), 0);
    EXPECT_TRUE(params.max_ports == 16 && params.queue_depth == 1 && params.num_grains == 256);
    params.queue_depth = depth;
    params.num_grains = grain;
    AudioOut2ContextHandle ctx = 0;
    EXPECT_EQ(sceAudioOut2ContextCreate(&params, nullptr, 0, &ctx), 0);
    EXPECT_NE(ctx, 0u);
    AudioOut2ForceWallClockForTesting(ctx);
    return ctx;
}

AudioOut2PortHandle MakePort(AudioOut2ContextHandle ctx, std::uint32_t dataFormat) {
    AudioOut2PortParam params{};
    params.data_format = dataFormat;
    params.sampling_freq = 48000;
    AudioOut2PortHandle port = 0;
    EXPECT_EQ(sceAudioOut2PortCreate(ctx, &params, &port), 0);
    EXPECT_NE(port, 0u);
    return port;
}

void SetPortData(AudioOut2PortHandle port, const float* pcm) {
    AudioOut2Attribute attr{};
    attr.attribute_id = 0;
    attr.value = &pcm;
    attr.value_size = sizeof(pcm);
    EXPECT_EQ(sceAudioOut2PortSetAttributes(port, &attr, 1), 0);
}

}  // namespace

// Verifies pure helper math for channel decoding, downmixing with LFE fold, and latency estimation.
TEST(AudioOut2Tests, PureHelpers) {
    EXPECT_EQ(AudioOut2DecodeChannels(0x100), 1u);
    EXPECT_EQ(AudioOut2DecodeChannels(0x200), 2u);
    EXPECT_EQ(AudioOut2DecodeChannels(0x880), 8u);
    EXPECT_EQ(AudioOut2DecodeChannels(0x000), 0u);
    EXPECT_EQ(AudioOut2DecodeChannels(0xF00), 0u);

    // Mono downmix fans out equally
    {
        float out[2] = {0, 0};
        const float in[1] = {2.0f};
        const float vol[8] = {0.5f, 0.5f, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 1, vol, out);
        EXPECT_NEAR(out[0], 1.0f, 1e-5f);
        EXPECT_NEAR(out[1], 1.0f, 1e-5f);
    }

    // Stereo downmix preserves channel separation
    {
        float out[2] = {0, 0};
        const float in[2] = {1.0f, 3.0f};
        const float vol[8] = {0.5f, 0.25f, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 2, vol, out);
        EXPECT_NEAR(out[0], 0.5f, 1e-5f);
        EXPECT_NEAR(out[1], 0.75f, 1e-5f);
    }

    // 8-channel bed downmix: centre folds at -3 dB, rears/sides at -3 dB, and LFE folds at -10 dB
    {
        float out[2] = {0, 0};
        const float in[8] = {1.0f, 2.0f, 4.0f, 100.0f, 8.0f, 10.0f, 12.0f, 14.0f};
        const float vol[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 8, vol, out);

        const float lfeFold = 100.0f * AUDIO_OUT2_LFE_GAIN;
        const float expectL = 1.0f + 4.0f * AUDIO_OUT2_DOWNMIX_GAIN + lfeFold + (8.0f + 12.0f) * AUDIO_OUT2_DOWNMIX_GAIN;
        const float expectR = 2.0f + 4.0f * AUDIO_OUT2_DOWNMIX_GAIN + lfeFold + (10.0f + 14.0f) * AUDIO_OUT2_DOWNMIX_GAIN;

        EXPECT_NEAR(out[0], expectL, 1e-3f);
        EXPECT_NEAR(out[1], expectR, 1e-3f);
    }

    // Latency and queue level math
    EXPECT_NEAR(AudioOut2QueuedMs(384), 1.0, 1e-9);
    EXPECT_EQ(AudioOut2QueueLevelForPending(0, 2048, 4), 0u);
    EXPECT_EQ(AudioOut2QueueLevelForPending(100, 64, 4), 2u);
    EXPECT_EQ(AudioOut2QueueLevelForPending(10000, 64, 4), 4u);
    EXPECT_EQ(AudioOut2QueueLevelForPending(100, 0, 4), 0u);
}

// Verifies context creation, memory query, parameter reset, queue level queries, and destruction.
TEST(AudioOut2Tests, ContextLifecycle) {
    EXPECT_EQ(sceAudioOut2ContextQueryMemory(nullptr, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2ContextParam params{};
    size_t memory = 0;
    EXPECT_EQ(sceAudioOut2ContextQueryMemory(&params, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2ContextQueryMemory(&params, &memory), 0);
    EXPECT_EQ(memory, 0x10000u);
    EXPECT_EQ(sceAudioOut2ContextResetParam(nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2ContextCreate(nullptr, nullptr, 0, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2ContextAdvance(0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2ContextDestroy(0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(0, nullptr, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2ContextPush(0, 0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    EXPECT_EQ(sceAudioOut2Initialize(), 0);
    EXPECT_EQ(sceAudioOut2ContextAdvance(ctx), 0);

    std::uint32_t level = 99;
    std::uint32_t avail = 99;
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(ctx, &level, &avail), 0);
    EXPECT_EQ(level, 0u);
    EXPECT_EQ(avail, 4u);

    EXPECT_EQ(sceAudioOut2ContextSetAttributes(ctx, nullptr, 1), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2ContextSetAttributes(ctx, nullptr, 0), 0);
    EXPECT_EQ(sceAudioOut2ContextSetAttributes(0, nullptr, 0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), 0);
    }
    EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), SCE_AUDIO_OUT2_ERROR_QUEUE_FULL);
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(ctx, &level, &avail), 0);
    EXPECT_EQ(level, 4u);
    EXPECT_EQ(avail, 0u);

    const double latency = AudioOut2LatencyMs(ctx);
    EXPECT_GE(latency, 0.0);
    EXPECT_EQ(AudioOut2LatencyMs(0), 0.0);

    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies port creation, attribute configuration, channel fan-out, and multi-port summing.
TEST(AudioOut2Tests, PortsAndMix) {
    EXPECT_EQ(sceAudioOut2PortCreate(0, nullptr, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    AudioOut2PortParam params{};
    AudioOut2PortHandle port = 0;
    EXPECT_EQ(sceAudioOut2PortCreate(ctx, nullptr, &port), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2PortCreate(ctx, &params, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2PortDestroy(0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2PortGetState(0, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2PortSetAttributes(0, nullptr, 0), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    AudioOut2PortHandle stereo = MakePort(ctx, 0x200);
    AudioOut2PortState state{};
    EXPECT_EQ(sceAudioOut2PortGetState(stereo, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(sceAudioOut2PortGetState(stereo, &state), 0);
    EXPECT_EQ(state.output, 1u);
    EXPECT_EQ(state.num_channels, 2u);
    EXPECT_EQ(state.volume, 127);

    std::vector<float> pcm(256 * 2, 0.5f);
    SetPortData(stereo, pcm.data());
    const float gains[2] = {1.0f, 1.0f};
    AudioOut2Attribute vol{};
    vol.attribute_id = 1;
    vol.value = gains;
    vol.value_size = sizeof(gains);
    EXPECT_EQ(sceAudioOut2PortSetAttributes(stereo, &vol, 1), 0);

    AudioOut2Attribute unknown{};
    unknown.attribute_id = 9;
    std::uint32_t marker = 0;
    unknown.value = &marker;
    unknown.value_size = sizeof(marker);
    EXPECT_EQ(sceAudioOut2PortSetAttributes(stereo, &unknown, 1), 0);

    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    std::vector<float> out(256 * 2, 0.0f);
    EXPECT_EQ(AudioOut2MixPorts(*context, out.data(), 256), 1u);
    for (float sample : out) {
        EXPECT_NEAR(sample, 0.5f, 1e-5f);
    }

    AudioOut2PortHandle mono = MakePort(ctx, 0x100);
    std::vector<float> monoPcm(256, 1.0f);
    SetPortData(mono, monoPcm.data());
    std::fill(out.begin(), out.end(), 0.0f);
    EXPECT_EQ(AudioOut2MixPorts(*context, out.data(), 256), 2u);
    for (float sample : out) {
        EXPECT_NEAR(sample, 1.5f, 1e-5f);
    }

    EXPECT_EQ(sceAudioOut2PortDestroy(mono), 0);
    EXPECT_EQ(sceAudioOut2PortDestroy(stereo), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies telemetry snapshot stability and reset hooks for process-wide metrics.
TEST(AudioOut2Tests, Telemetry) {
    AudioOut2ResetTelemetryForTesting();
    AudioOut2Telemetry empty = AudioOut2TelemetrySnapshot();
    EXPECT_EQ(empty.underruns, 0u);
    EXPECT_EQ(empty.overrunDrops, 0u);

    AudioOut2ContextHandle ctx = MakeWallClockContext(2, 256);
    EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), 0);
    EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), 0);
    AudioOut2Telemetry after = AudioOut2TelemetrySnapshot();
    EXPECT_EQ(after.underruns, 0u);
    EXPECT_EQ(after.overrunDrops, 0u);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies system state queries, user handles, and speaker configuration angles.
TEST(AudioOut2Tests, SystemUserSpeaker) {
    EXPECT_EQ(sceAudioOut2GetSystemState(nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2SystemState sys{};
    EXPECT_EQ(sceAudioOut2GetSystemState(&sys), 0);
    EXPECT_FLOAT_EQ(sys.loudness, 0.0f);

    EXPECT_EQ(sceAudioOut2UserCreate(0, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2UserHandle a = 0;
    AudioOut2UserHandle b = 0;
    EXPECT_EQ(sceAudioOut2UserCreate(0, &a), 0);
    EXPECT_EQ(sceAudioOut2UserCreate(0, &b), 0);
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);
    EXPECT_NE(a, b);
    EXPECT_EQ(sceAudioOut2UserDestroy(a), 0);
    EXPECT_EQ(sceAudioOut2UserDestroy(b), 0);

    EXPECT_EQ(sceAudioOut2GetSpeakerInfo(nullptr, 0), SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2SpeakerInfo info{};
    EXPECT_EQ(sceAudioOut2GetSpeakerInfo(&info, 0), 0);
    EXPECT_EQ(info.type, 0u);
    EXPECT_EQ(info.available_bits, 0x3u);
    EXPECT_EQ(info.speaker_angle[0].azimuth, -30);
    EXPECT_EQ(info.speaker_angle[1].azimuth, 30);
}

// Verifies AudioOut v1 port initialization, open, volume adjustment, and audio pushing.
TEST(AudioOut2Tests, AudioOutV1) {
    EXPECT_EQ(sceAudioOutInit(), 0);
    EXPECT_EQ(sceAudioOutOpen(0, 999, 0, 256, 48000, 1), -2144993270);
    EXPECT_EQ(sceAudioOutOpen(0, 0, 0, 256, 48000, 0xFF), -2144993276);
    EXPECT_EQ(sceAudioOutClose(999), -2144993277);
    EXPECT_EQ(sceAudioOutOutput(999, nullptr), -2144993277);
    EXPECT_EQ(sceAudioOutOutputs(nullptr, 0), -2144993276);
    EXPECT_EQ(sceAudioOutSetVolume(999, 0, nullptr), -2144993276);
    AudioOutPortState v1state{};
    EXPECT_EQ(sceAudioOutGetPortState(999, nullptr), -2144993276);
    EXPECT_EQ(sceAudioOutGetPortState(999, &v1state), -2144993277);

    const int handle = sceAudioOutOpen(0, 0, 0, 256, 48000, 1);
    EXPECT_GT(handle, 0);
    EXPECT_EQ(sceAudioOutOutput(handle, nullptr), 256);
    std::vector<std::int16_t> block(256 * 2, 1000);
    EXPECT_EQ(sceAudioOutOutput(handle, block.data()), 256);
    int vols[2] = {32768, 32768};
    EXPECT_EQ(sceAudioOutSetVolume(handle, 0x3, vols), 0);
    EXPECT_EQ(sceAudioOutGetPortState(handle, &v1state), 0);
    EXPECT_EQ(v1state.channel, 2u);
    AudioOutOutputParam multi{};
    multi.handle = handle;
    multi.ptr = block.data();
    EXPECT_EQ(sceAudioOutOutputs(&multi, 1), 256);
    EXPECT_EQ(sceAudioOutClose(handle), 0);
}
