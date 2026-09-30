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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <future>
#include <thread>

#include "AudioMixerTestAccess.hpp"

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


/**
 * RAII fixture for v1 mixer tests: brings up a dummy-driver mixer with frozen
 * wall-clock retirement, tracks opened handles, and on every exit path (including
 * early ASSERT_* returns) closes them, shuts the mixer down and unfreezes the
 * clock so a failing test cannot leak sources or pause state into later tests.
 */
class V1MixerGuard {
public:
    V1MixerGuard() {
        auto& mixer = AudioMixer::Get();
        mixer.Shutdown();
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        initialized = mixer.Initialize();
        mixer.ForceWallClockForTesting();
        mixer.PauseWallClockForTesting(true);
    }
    ~V1MixerGuard() {
        for (int handle : handles) sceAudioOutClose(handle);
        auto& mixer = AudioMixer::Get();
        mixer.Shutdown();
        mixer.PauseWallClockForTesting(false);
    }
    V1MixerGuard(const V1MixerGuard&) = delete;
    V1MixerGuard& operator=(const V1MixerGuard&) = delete;

    /** Opens a v1 port and tracks it for cleanup. @return handle (<=0 on failure). */
    int Open(std::uint32_t len, std::uint32_t param) {
        const int handle = sceAudioOutOpen(0, 0, 0, len, 48000, param);
        if (handle > 0) handles.push_back(handle);
        return handle;
    }
    /** Closes a tracked handle now and stops tracking it. @return close result. */
    int Close(int handle) {
        handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
        return sceAudioOutClose(handle);
    }

    bool initialized = false;
    std::vector<int> handles;
};

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

// Verifies one Outputs batch submits every port in the batch: both ports'
// grains reach the mixer and sum in the callback. Ported from SharpEMU
// Audio/AudioOutExportsTests.SubmitsEveryPortInTheBatch (GPL-2.0-or-later,
// used under GPL-2.0 terms; guest-memory fault isolation from the original
// has no equivalent here and is not ported).
TEST(AudioOut2Tests, V1BatchSubmitsEveryPort) {
    V1MixerGuard guard;
    auto& mixer = AudioMixer::Get();
    ASSERT_TRUE(guard.initialized);
    const int first = guard.Open(256, 4);
    const int second = guard.Open(256, 4);
    ASSERT_GT(first, 0);
    ASSERT_GT(second, 0);
    ASSERT_NE(first, second);
    std::vector<float> pcmA(256 * 2, 0.25f);
    std::vector<float> pcmB(256 * 2, 0.25f);
    AudioOutOutputParam params[2]{};
    params[0].handle = first;
    params[0].ptr = pcmA.data();
    params[1].handle = second;
    params[1].ptr = pcmB.data();
    EXPECT_EQ(sceAudioOutOutputs(params, 2), 256);
    std::vector<float> out(256 * 2, 0.0f);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (float sample : out) {
        // Both grains submitted: 0.25 + 0.25 sums to exactly 0.5.
        EXPECT_FLOAT_EQ(sample, 0.5f);
    }
}

// Verifies S16 stereo guest PCM converts to mixer float with exact half-scale
// mapping, full-scale headroom without hard clipping, and volume scaling.
// Ported from SharpEMU Audio/AudioPcmConversionTests (GPL-2.0-or-later, used
// under GPL-2.0 terms; direction adapted: our pipeline converts S16 guest PCM
// up to F32 for the mixer). NaN sanitization from the original is not ported:
// it would need new mixer behavior, which is out of scope.
TEST(AudioOut2Tests, V1S16StereoConversionAndVolume) {
    V1MixerGuard guard;
    auto& mixer = AudioMixer::Get();
    ASSERT_TRUE(guard.initialized);
    const int handle = guard.Open(256, 1);
    ASSERT_GT(handle, 0);
    // Half scale maps exactly: 16384 / 32768 = 0.5.
    std::vector<std::int16_t> block(256 * 2, 0);
    for (std::uint32_t i = 0; i < 256; i++) {
        block[i * 2 + 0] = 16384;
        block[i * 2 + 1] = -16384;
    }
    EXPECT_EQ(sceAudioOutOutput(handle, block.data()), 256);
    std::vector<float> out(256 * 2, 0.0f);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (std::uint32_t i = 0; i < 256; i++) {
        EXPECT_FLOAT_EQ(out[i * 2 + 0], 0.5f);
        EXPECT_FLOAT_EQ(out[i * 2 + 1], -0.5f);
    }
    // Full scale stays inside (-1, 1) with headroom instead of hard clipping.
    std::fill(block.begin(), block.end(), INT16_MAX);
    EXPECT_EQ(sceAudioOutOutput(handle, block.data()), 256);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (float sample : out) {
        EXPECT_GT(sample, 0.9f);
        EXPECT_LT(sample, 1.0f);
    }
    std::fill(block.begin(), block.end(), INT16_MIN);
    EXPECT_EQ(sceAudioOutOutput(handle, block.data()), 256);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (float sample : out) {
        EXPECT_GT(sample, -1.0f);
        EXPECT_LT(sample, -0.9f);
    }
    // Half volume halves the converted amplitude: 0.5 * 0.5 = 0.25.
    int vols[2] = {16384, 16384};
    EXPECT_EQ(sceAudioOutSetVolume(handle, 0x3, vols), 0);
    std::fill(block.begin(), block.end(), 0);
    for (std::uint32_t i = 0; i < 256; i++) {
        block[i * 2 + 0] = 16384;
        block[i * 2 + 1] = 16384;
    }
    EXPECT_EQ(sceAudioOutOutput(handle, block.data()), 256);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.25f);
    }
}

// The consolidated host device owns queue timing even though the obsolete
// per-context device field remains zero. Missing sources still use the model.
TEST(AudioOut2Tests, QueueLevelUsesMixerSourceWithHostDevice) {
    auto& mixer = AudioMixer::Get();
    mixer.Shutdown();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    ASSERT_TRUE(mixer.Initialize());
    AudioOut2ContextParam params{};
    params.num_grains = 256;
    params.queue_depth = 8;
    AudioOut2ContextHandle ctx{};
    ASSERT_EQ(sceAudioOut2ContextCreate(&params, nullptr, 0, &ctx), 0);
    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    ASSERT_NE(context->source, nullptr);
    context->source->SetPaused(true);
    // Wait out selection that preceded the pause before placing the test grain.
    while (!AudioMixerTestAccess::Acquire(*context->source)) std::this_thread::yield();
    std::vector<AudioFrame> frames(AUDIO_MIXER_TARGET_CUSHION_FRAMES + 512);
    const bool pushed = context->source->PushStereo48k(frames.data(), frames.size());
    // Re-pause before releasing: the push unpaused the source, and the live
    // dummy callback would otherwise drain the grain before the asserts below.
    context->source->SetPaused(true);
    AudioMixerTestAccess::Release(*context->source);
    ASSERT_TRUE(pushed);
    EXPECT_EQ(context->device, 0u);
    std::uint32_t level{}, available{};
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(ctx, &level, &available), 0);
    EXPECT_EQ(level, 2u);
    EXPECT_EQ(available, 6u);

    auto* source = context->source;
    context->source = nullptr;
    context->device = 1; // Must not substitute for the missing mixer source.
    context->queued = 3;
    context->playHead = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(ctx, &level, &available), 0);
    EXPECT_EQ(level, 3u);
    EXPECT_EQ(available, 5u);
    context->device = 0;
    context->source = source;
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
    mixer.Shutdown();
}

// A blocked single or batch output must allow table operations on other ports.
// Closing/reopening the handle must retain the old source until output returns.
TEST(AudioOut2Tests, V1PacingReleasesTableAndPinsSourceAcrossClose) {
    auto& mixer = AudioMixer::Get();
    for (bool batch : {false, true}) {
        mixer.Shutdown();
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        ASSERT_TRUE(mixer.Initialize());
        mixer.ForceWallClockForTesting();
        // Freeze retirement so queued grains persist until explicit drains;
        // without this, real-time fallback pumps would consume them mid-test.
        mixer.PauseWallClockForTesting(true);
        const int handle = sceAudioOutOpen(0, 0, 0, 4096, 48000, 4);
        ASSERT_GT(handle, 0);
        auto& oldSource = AudioMixerTestAccess::FirstSource(mixer);
        std::vector<float> pcm(4096 * 2, 0.25f);
        ASSERT_EQ(sceAudioOutOutput(handle, pcm.data()), 4096);
        std::promise<void> started;
        auto output = std::async(std::launch::async, [&] {
            AudioOutOutputParam param{};
            param.handle = handle;
            param.ptr = pcm.data();
            started.set_value();
            return batch ? sceAudioOutOutputs(&param, 1) : sceAudioOutOutput(handle, pcm.data());
        });
        started.get_future().wait();
        EXPECT_EQ(output.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
        auto reopen = std::async(std::launch::async, [&] {
            EXPECT_EQ(sceAudioOutClose(handle), 0);
            return sceAudioOutOpen(0, 0, 0, 64, 48000, 4);
        });
        EXPECT_EQ(reopen.wait_for(std::chrono::milliseconds(60)), std::future_status::ready);
        const int replacement = reopen.get();
        EXPECT_EQ(replacement, handle);
        EXPECT_TRUE(oldSource.IsActive());
        std::vector<float> newPcm(64 * 2, 0.5f);
        EXPECT_EQ(sceAudioOutOutput(replacement, newPcm.data()), 64);
        // Retire just the old source; the replacement's queued samples must survive.
        EXPECT_TRUE(AudioMixerTestAccess::Acquire(oldSource));
        std::vector<AudioFrame> discard(4096);
        AudioMixerTestAccess::Pop(oldSource, discard.data(), discard.size());
        AudioMixerTestAccess::Release(oldSource);
        EXPECT_EQ(output.get(), 4096);
        EXPECT_FALSE(oldSource.IsActive());
        std::vector<float> mixed(64 * 2);
        AudioMixerTestAccess::Process(mixer, mixed.data(), 64);
        for (float sample : mixed) EXPECT_FLOAT_EQ(sample, 0.5f);
        EXPECT_EQ(sceAudioOutClose(replacement), 0);
        mixer.Shutdown();
        mixer.PauseWallClockForTesting(false);
    }
}

// Two producer APIs on one source must serialize the wait and push together.
// Once one full grain is queued, the second must remain paced until consumption.
TEST(AudioOut2Tests, V1SingleAndBatchProducersSharePacingLock) {
    auto& mixer = AudioMixer::Get();
    mixer.Shutdown();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    ASSERT_TRUE(mixer.Initialize());
    mixer.ForceWallClockForTesting();
    // Freeze retirement so queued grains persist until explicit drains;
    // without this, real-time fallback pumps would consume them mid-test.
    mixer.PauseWallClockForTesting(true);
    const int handle = sceAudioOutOpen(0, 0, 0, 4096, 48000, 4);
    ASSERT_GT(handle, 0);
    std::vector<float> pcm(4096 * 2, 0.25f);
    ASSERT_EQ(sceAudioOutOutput(handle, pcm.data()), 4096);
    auto single = std::async(std::launch::async, [&] { return sceAudioOutOutput(handle, pcm.data()); });
    AudioOutOutputParam param{};
    param.handle = handle;
    param.ptr = pcm.data();
    auto batch = std::async(std::launch::async, [&] { return sceAudioOutOutputs(&param, 1); });
    EXPECT_EQ(single.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    EXPECT_EQ(batch.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    mixer.SimulateCallback(4096);
    auto& source = AudioMixerTestAccess::FirstSource(mixer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    while (source.GetQueuedFrames() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(source.GetQueuedFrames(), 4096u);
    // One producer has completed; the other cannot finish without a second drain.
    const auto singleReady = single.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready;
    const auto batchReady = batch.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready;
    EXPECT_NE(singleReady, batchReady);
    mixer.SimulateCallback(4096);
    EXPECT_EQ(single.get(), 4096);
    EXPECT_EQ(batch.get(), 4096);
    EXPECT_EQ(source.GetQueuedFrames(), 4096u);
    EXPECT_EQ(sceAudioOutClose(handle), 0);
    mixer.Shutdown();
    mixer.PauseWallClockForTesting(false);
}

// Regression for CodeRabbit finding: AudioOut2 Render used to SoftLimit each
// context before the mixer callback limited the sum again, compressing peaks
// twice. A guest grain at 1.1 must reach the output limited exactly once.
TEST(AudioOut2Tests, ContextPeakLimitedOnlyOnce) {
    V1MixerGuard guard;
    auto& mixer = AudioMixer::Get();
    ASSERT_TRUE(guard.initialized);
    AudioOut2ContextParam cp{};
    cp.num_grains = 256;
    cp.queue_depth = 4;
    AudioOut2ContextHandle ctx{};
    ASSERT_EQ(sceAudioOut2ContextCreate(&cp, nullptr, 0, &ctx), 0);
    AudioOut2PortHandle port = MakePort(ctx, 0x200);
    std::vector<float> pcm(256 * 2, 1.1f);
    SetPortData(port, pcm.data());
    ASSERT_EQ(sceAudioOut2ContextPush(ctx, 0), 0);
    std::vector<float> out(256 * 2, 0.0f);
    AudioMixerTestAccess::Process(mixer, out.data(), 256);
    for (float sample : out) EXPECT_FLOAT_EQ(sample, SoftLimit(1.1f));
    EXPECT_EQ(sceAudioOut2PortDestroy(port), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Regression for CodeRabbit finding: a blocking v1 push that times out against
// a stuck (never-draining) ring must be recorded as a stall, not only dropped.
// Wall-clock retirement is frozen, so the ring never drains: the second 4096-frame
// grain cannot fit under the 100 ms ceiling and must exhaust the 200 ms bound.
TEST(AudioOut2Tests, V1PushTimeoutRecordsStall) {
    V1MixerGuard guard;
    auto& mixer = AudioMixer::Get();
    ASSERT_TRUE(guard.initialized);
    mixer.ResetTelemetryForTesting();
    const int handle = guard.Open(4096, 4);
    ASSERT_GT(handle, 0);
    std::vector<float> pcm(4096 * 2, 0.1f);
    EXPECT_EQ(sceAudioOutOutput(handle, pcm.data()), 4096);
    EXPECT_EQ(mixer.GetStalls(), 0u);
    EXPECT_EQ(sceAudioOutOutput(handle, pcm.data()), 4096);
    EXPECT_EQ(mixer.GetStalls(), 1u);
}
