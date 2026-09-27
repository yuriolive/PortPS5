// Unit tests for AudioOut2 contexts/ports, AudioOut v1 return codes, and the
// M1 underrun/latency telemetry (docs/spec/audio.md M1 row).
//
// No audio device and no game data: contexts run on the wall-clock model
// through the test hook, and PCM buffers are synthetic. Mastering and
// speaker-array creation abort through the logging abort path, so they are
// intentionally not called here.

// SDL renames main() to SDL_main() unless told otherwise; these tests own
// their main, so opt out before any SDL header is pulled in.
#define SDL_MAIN_HANDLED
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAudioOut/src/AudioOut2Internal.hpp"

#include <cmath>
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

bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

}  // namespace

extern "C" {
int APS5_VABI sceAudioOut2ContextAdvance(AudioOut2ContextHandle ctx) noexcept;
int APS5_VABI sceAudioOut2ContextCreate(const AudioOut2ContextParam* params, void* buffer, size_t buffer_size, AudioOut2ContextHandle* ctx) noexcept;
int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle ctx) noexcept;
int APS5_VABI sceAudioOut2ContextGetQueueLevel(AudioOut2ContextHandle ctx, uint32_t* queue_level, uint32_t* available_queue) noexcept;
int APS5_VABI sceAudioOut2ContextPush(AudioOut2ContextHandle ctx, uint32_t blocking) noexcept;
int APS5_VABI sceAudioOut2ContextQueryMemory(const AudioOut2ContextParam* params, size_t* memory_size) noexcept;
int APS5_VABI sceAudioOut2ContextResetParam(AudioOut2ContextParam* params) noexcept;
int APS5_VABI sceAudioOut2ContextSetAttributes(AudioOut2ContextHandle ctx, const AudioOut2Attribute* attributes, uint32_t num) noexcept;
int APS5_VABI sceAudioOut2PortCreate(AudioOut2ContextHandle ctx, const AudioOut2PortParam* params, AudioOut2PortHandle* port) noexcept;
int APS5_VABI sceAudioOut2PortDestroy(AudioOut2PortHandle port) noexcept;
int APS5_VABI sceAudioOut2PortGetState(AudioOut2PortHandle port, AudioOut2PortState* state) noexcept;
int APS5_VABI sceAudioOut2PortSetAttributes(AudioOut2PortHandle port, const AudioOut2Attribute* attributes, uint32_t num) noexcept;
int APS5_VABI sceAudioOut2Initialize(void) noexcept;
int APS5_VABI sceAudioOut2GetSystemState(AudioOut2SystemState* state) noexcept;
int APS5_VABI sceAudioOut2UserCreate(uint32_t user_id, AudioOut2UserHandle* handle) noexcept;
int APS5_VABI sceAudioOut2UserDestroy(AudioOut2UserHandle handle) noexcept;
int APS5_VABI sceAudioOut2GetSpeakerInfo(AudioOut2SpeakerInfo* info, uint32_t flags) noexcept;
int APS5_VABI sceAudioOutInit() noexcept;
int APS5_VABI sceAudioOutOpen(int userId, int type, int index, std::uint32_t len, std::uint32_t freq, std::uint32_t param) noexcept;
int APS5_VABI sceAudioOutClose(int handle) noexcept;
int APS5_VABI sceAudioOutOutput(int handle, const void* ptr) noexcept;
int APS5_VABI sceAudioOutOutputs(AudioOutOutputParam* param, std::uint32_t num) noexcept;
int APS5_VABI sceAudioOutSetVolume(int handle, std::uint32_t flag, int* vol) noexcept;
int APS5_VABI sceAudioOutGetPortState(int handle, AudioOutPortState* state) noexcept;
}

static void TestPureHelpers() {
    // Channel decoding from data_format bits 8..11.
    REQUIRE(AudioOut2DecodeChannels(0x100) == 1);
    REQUIRE(AudioOut2DecodeChannels(0x200) == 2);
    REQUIRE(AudioOut2DecodeChannels(0x880) == 8);
    REQUIRE(AudioOut2DecodeChannels(0x000) == 0);
    REQUIRE(AudioOut2DecodeChannels(0xF00) == 0);

    // Downmix matrix on one frame.
    {
        float out[2] = {0, 0};
        const float in[1] = {2.0f};
        const float vol[8] = {0.5f, 0.5f, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 1, vol, out);
        REQUIRE(Near(out[0], 1.0f) && Near(out[1], 1.0f));
    }
    {
        float out[2] = {0, 0};
        const float in[2] = {1.0f, 3.0f};
        const float vol[8] = {0.5f, 0.25f, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 2, vol, out);
        REQUIRE(Near(out[0], 0.5f) && Near(out[1], 0.75f));
    }
    {
        // 8-channel bed: centre folds at -3 dB into both sides, rears/sides
        // into the front, LFE is dropped.
        float out[2] = {0, 0};
        const float in[8] = {1, 2, 4, 100, 8, 10, 12, 14};
        const float vol[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        AudioOut2DownmixFrame(in, 8, vol, out);
        const float expectL = 1.0f + 4.0f * 0.7071f + (8.0f + 12.0f) * 0.7071f;
        const float expectR = 2.0f + 4.0f * 0.7071f + (10.0f + 14.0f) * 0.7071f;
        REQUIRE(std::fabs(out[0] - expectL) < 1e-3f);
        REQUIRE(std::fabs(out[1] - expectR) < 1e-3f);
    }

    // Latency and queue-level math without a device.
    REQUIRE(std::fabs(AudioOut2QueuedMs(384) - 1.0) < 1e-9);
    REQUIRE(AudioOut2QueueLevelForPending(0, 2048, 4) == 0);
    REQUIRE(AudioOut2QueueLevelForPending(100, 64, 4) == 2);
    REQUIRE(AudioOut2QueueLevelForPending(10000, 64, 4) == 4);
    REQUIRE(AudioOut2QueueLevelForPending(100, 0, 4) == 0);
}

static AudioOut2ContextHandle MakeWallClockContext(std::uint32_t depth, std::uint32_t grain) {
    AudioOut2ContextParam params{};
    REQUIRE(sceAudioOut2ContextResetParam(&params) == 0);
    REQUIRE(params.max_ports == 16 && params.queue_depth == 1 && params.num_grains == 256);
    params.queue_depth = depth;
    params.num_grains = grain;
    AudioOut2ContextHandle ctx = 0;
    REQUIRE(sceAudioOut2ContextCreate(&params, nullptr, 0, &ctx) == 0);
    REQUIRE(ctx != 0);
    AudioOut2ForceWallClockForTesting(ctx);
    return ctx;
}

static void TestContextLifecycle() {
    REQUIRE(sceAudioOut2ContextQueryMemory(nullptr, nullptr) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2ContextParam params{};
    size_t memory = 0;
    REQUIRE(sceAudioOut2ContextQueryMemory(&params, nullptr) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2ContextQueryMemory(&params, &memory) == 0);
    REQUIRE(memory == 0x10000);
    REQUIRE(sceAudioOut2ContextResetParam(nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2ContextCreate(nullptr, nullptr, 0, nullptr) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2ContextAdvance(0) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    REQUIRE(sceAudioOut2ContextDestroy(0) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    REQUIRE(sceAudioOut2ContextGetQueueLevel(0, nullptr, nullptr) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    REQUIRE(sceAudioOut2ContextPush(0, 0) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    REQUIRE(sceAudioOut2Initialize() == 0);
    REQUIRE(sceAudioOut2ContextAdvance(ctx) == 0);

    std::uint32_t level = 99;
    std::uint32_t avail = 99;
    REQUIRE(sceAudioOut2ContextGetQueueLevel(ctx, &level, &avail) == 0);
    REQUIRE(level == 0 && avail == 4);

    // Null attributes with a count are invalid; an empty set is a no-op.
    REQUIRE(sceAudioOut2ContextSetAttributes(ctx, nullptr, 1) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2ContextSetAttributes(ctx, nullptr, 0) == 0);
    REQUIRE(sceAudioOut2ContextSetAttributes(0, nullptr, 0) ==
            SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    // Fill the queue with non-blocking pushes; one more is rejected.
    for (int i = 0; i < 4; i++) REQUIRE(sceAudioOut2ContextPush(ctx, 0) == 0);
    REQUIRE(sceAudioOut2ContextPush(ctx, 0) == SCE_AUDIO_OUT2_ERROR_QUEUE_FULL);
    REQUIRE(sceAudioOut2ContextGetQueueLevel(ctx, &level, &avail) == 0);
    REQUIRE(level == 4 && avail == 0);

    // Wall-clock latency is the four queued grains (4 x 256 samples at 48 kHz).
    const double latency = AudioOut2LatencyMs(ctx);
    REQUIRE(latency > 15.0 && latency < 30.0);
    REQUIRE(AudioOut2LatencyMs(0) == 0.0);

    REQUIRE(sceAudioOut2ContextDestroy(ctx) == 0);
}

static AudioOut2PortHandle MakePort(AudioOut2ContextHandle ctx, std::uint32_t dataFormat) {
    AudioOut2PortParam params{};
    params.data_format = dataFormat;
    params.sampling_freq = 48000;
    AudioOut2PortHandle port = 0;
    REQUIRE(sceAudioOut2PortCreate(ctx, &params, &port) == 0);
    REQUIRE(port != 0);
    return port;
}

static void SetPortData(AudioOut2PortHandle port, const float* pcm) {
    AudioOut2Attribute attr{};
    attr.attribute_id = 0;
    attr.value = &pcm;
    attr.value_size = sizeof(pcm);
    REQUIRE(sceAudioOut2PortSetAttributes(port, &attr, 1) == 0);
}

static void TestPortsAndMix() {
    REQUIRE(sceAudioOut2PortCreate(0, nullptr, nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    AudioOut2PortParam params{};
    AudioOut2PortHandle port = 0;
    REQUIRE(sceAudioOut2PortCreate(ctx, nullptr, &port) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2PortCreate(ctx, &params, nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2PortDestroy(0) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    REQUIRE(sceAudioOut2PortGetState(0, nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    REQUIRE(sceAudioOut2PortSetAttributes(0, nullptr, 0) == SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);

    // Stereo port: the real mix path sums the grain set through attributes.
    AudioOut2PortHandle stereo = MakePort(ctx, 0x200);
    AudioOut2PortState state{};
    REQUIRE(sceAudioOut2PortGetState(stereo, nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    REQUIRE(sceAudioOut2PortGetState(stereo, &state) == 0);
    REQUIRE(state.output == 1 && state.num_channels == 2 && state.volume == 127);

    std::vector<float> pcm(256 * 2, 0.5f);
    SetPortData(stereo, pcm.data());
    const float gains[2] = {1.0f, 1.0f};
    AudioOut2Attribute vol{};
    vol.attribute_id = 1;
    vol.value = gains;
    vol.value_size = sizeof(gains);
    REQUIRE(sceAudioOut2PortSetAttributes(stereo, &vol, 1) == 0);
    // Unknown attribute ids are ignored, never an error.
    AudioOut2Attribute unknown{};
    unknown.attribute_id = 9;
    std::uint32_t marker = 0;
    unknown.value = &marker;
    unknown.value_size = sizeof(marker);
    REQUIRE(sceAudioOut2PortSetAttributes(stereo, &unknown, 1) == 0);

    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    std::vector<float> out(256 * 2, 0.0f);
    REQUIRE(AudioOut2MixPorts(*context, out.data(), 256) == 1);
    for (float sample : out) REQUIRE(Near(sample, 0.5f));

    // Mono port fans out to both sides.
    AudioOut2PortHandle mono = MakePort(ctx, 0x100);
    std::vector<float> monoPcm(256, 1.0f);
    SetPortData(mono, monoPcm.data());
    std::fill(out.begin(), out.end(), 0.0f);
    REQUIRE(AudioOut2MixPorts(*context, out.data(), 256) == 2);
    for (float sample : out) REQUIRE(Near(sample, 1.5f));

    REQUIRE(sceAudioOut2PortDestroy(mono) == 0);
    REQUIRE(sceAudioOut2PortDestroy(stereo) == 0);
    REQUIRE(sceAudioOut2ContextDestroy(ctx) == 0);
}

static void TestTelemetry() {
    AudioOut2ResetTelemetryForTesting();
    AudioOut2Telemetry empty = AudioOut2TelemetrySnapshot();
    REQUIRE(empty.underruns == 0 && empty.overrunDrops == 0);

    // Wall-clock pushes never touch the device queue, so neither counter moves.
    AudioOut2ContextHandle ctx = MakeWallClockContext(2, 256);
    REQUIRE(sceAudioOut2ContextPush(ctx, 0) == 0);
    REQUIRE(sceAudioOut2ContextPush(ctx, 0) == 0);
    AudioOut2Telemetry after = AudioOut2TelemetrySnapshot();
    REQUIRE(after.underruns == 0 && after.overrunDrops == 0);
    REQUIRE(sceAudioOut2ContextDestroy(ctx) == 0);
}

static void TestSystemUserSpeaker() {
    REQUIRE(sceAudioOut2GetSystemState(nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2SystemState sys{};
    REQUIRE(sceAudioOut2GetSystemState(&sys) == 0);
    REQUIRE(sys.loudness == 0.0f);

    REQUIRE(sceAudioOut2UserCreate(0, nullptr) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2UserHandle a = 0;
    AudioOut2UserHandle b = 0;
    REQUIRE(sceAudioOut2UserCreate(0, &a) == 0);
    REQUIRE(sceAudioOut2UserCreate(0, &b) == 0);
    REQUIRE(a != 0 && b != 0 && a != b);
    REQUIRE(sceAudioOut2UserDestroy(a) == 0);
    REQUIRE(sceAudioOut2UserDestroy(b) == 0);

    REQUIRE(sceAudioOut2GetSpeakerInfo(nullptr, 0) == SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT);
    AudioOut2SpeakerInfo info{};
    REQUIRE(sceAudioOut2GetSpeakerInfo(&info, 0) == 0);
    REQUIRE(info.type == 0 && info.available_bits == 0x3);
    REQUIRE(info.speaker_angle[0].azimuth == -30 && info.speaker_angle[1].azimuth == 30);
}

static void TestAudioOutV1() {
    REQUIRE(sceAudioOutInit() == 0);
    REQUIRE(sceAudioOutOpen(0, 999, 0, 256, 48000, 1) == -2144993270);
    REQUIRE(sceAudioOutOpen(0, 0, 0, 256, 48000, 0xFF) == -2144993276);
    REQUIRE(sceAudioOutClose(999) == -2144993277);
    REQUIRE(sceAudioOutOutput(999, nullptr) == -2144993277);
    REQUIRE(sceAudioOutOutputs(nullptr, 0) == -2144993276);
    REQUIRE(sceAudioOutSetVolume(999, 0, nullptr) == -2144993276);
    AudioOutPortState v1state{};
    REQUIRE(sceAudioOutGetPortState(999, nullptr) == -2144993276);
    REQUIRE(sceAudioOutGetPortState(999, &v1state) == -2144993277);

    // A null buffer waits for the queue to drain instead of throwing; a real
    // buffer queues one block. Both work with or without an audio device.
    const int handle = sceAudioOutOpen(0, 0, 0, 256, 48000, 1);
    REQUIRE(handle > 0);
    REQUIRE(sceAudioOutOutput(handle, nullptr) == 256);
    std::vector<std::int16_t> block(256 * 2, 1000);
    REQUIRE(sceAudioOutOutput(handle, block.data()) == 256);
    int vols[2] = {32768, 32768};
    REQUIRE(sceAudioOutSetVolume(handle, 0x3, vols) == 0);
    REQUIRE(sceAudioOutGetPortState(handle, &v1state) == 0);
    REQUIRE(v1state.channel == 2);
    AudioOutOutputParam multi{};
    multi.handle = handle;
    multi.ptr = block.data();
    REQUIRE(sceAudioOutOutputs(&multi, 1) == 256);
    REQUIRE(sceAudioOutClose(handle) == 0);
}

int main() {
    TestPureHelpers();
    TestContextLifecycle();
    TestPortsAndMix();
    TestTelemetry();
    TestSystemUserSpeaker();
    TestAudioOutV1();
    return 0;
}
