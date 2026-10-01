// AudioOut2Internal.hpp
// PortPS5 - Internal AudioOut2 Context and Port Definitions (Audio Subsystem M2)
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libSceAudioOut. Defines structures, channel decoding,
//   and downmixing helpers shared across AudioOut2 implementation modules.
//
// Threading & Lifecycle:
//   - Header-only utilities and context structures.
//   - Downmixing routines are thread-safe and non-allocating.

#ifndef CORE_LIBS_PRX_LIBSCEAUDIOOUT_SRC_AUDIOOUT2INTERNAL_HPP
#define CORE_LIBS_PRX_LIBSCEAUDIOOUT_SRC_AUDIOOUT2INTERNAL_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "SDL.h"
#include "SceTypes.hpp"
#include "AudioMixer.hpp"

// Shared by the AudioOut2 context and port files (ported from AnyPS5 PR #5 as
// general mechanisms; no per-title branches).
//
// Tracing uses debug.trace = ["audio"] (docs/spec/configuration.md). It names
// the context/port parameters, the first attribute sets per port (then
// sampled), the first pushes and queue polls, and a once-per-second summary
// with the push rate and the SDL queue level.
bool AudioOut2TraceEnabled();
// Seconds since the first AudioOut2 call, for the trace.
double AudioOut2TraceSeconds();
bool AudioOut2IsValidContext(AudioOut2ContextHandle ctx);

#define AUDIOOUT2_TRACE(...) \
    do { \
        if (AudioOut2TraceEnabled()) std::fprintf(stderr, "[audioout2] " __VA_ARGS__); \
    } while (0)

static constexpr int SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT = static_cast<int>(0x80260502);
static constexpr int SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE = static_cast<int>(0x80260503);
static constexpr int SCE_AUDIO_OUT2_ERROR_OUT_OF_MEMORY = static_cast<int>(0x80260505);
// Not recovered from a title or SDK header: follows the 0x802605xx pattern of the codes above. Returned
// by a non-blocking push when the modelled hardware queue is full.
static constexpr int SCE_AUDIO_OUT2_ERROR_QUEUE_FULL = static_cast<int>(0x80260507);

static constexpr std::uint32_t AUDIO_OUT2_SAMPLE_RATE = 48000;
// The grain a push carries, in samples per channel: a context's num_grains (256 samples = 5.33 ms).
static constexpr std::uint32_t AUDIO_OUT2_DEFAULT_GRAIN = 256;
static constexpr std::uint32_t AUDIO_OUT2_OUTPUT_CHANNELS = 2;
static constexpr std::uint32_t AUDIO_OUT2_PORT_CHANNELS_MAX = 8;

// data_format bits 8..11 carry the channel count: 0x100 is mono, 0x200 stereo,
// 0x880 an 8-channel bed. The low byte's meaning is not known.
static constexpr std::uint32_t AUDIO_OUT2_FORMAT_CHANNELS_SHIFT = 8;
static constexpr std::uint32_t AUDIO_OUT2_FORMAT_CHANNELS_MASK = 0xFu;
// data_format bits 0..6 carry the sample type; bit 7 selects the "standard" 8-channel order, which
// sums to the same stereo fold (KytyPS5 libAudio2.cpp decodes the same fields).
static constexpr std::uint32_t AUDIO_OUT2_FORMAT_TYPE_MASK = 0x7Fu;

// 8-channel order FL FR C LFE RL RR SL SR (a swapped rear/side pair order sums
// the same): the rear and side pairs fold into the front at -3 dB and the
// centre into both sides. The LFE channel folds into the front pair at -10 dB
// (docs/spec/audio.md Target design M2).
static constexpr float AUDIO_OUT2_DOWNMIX_GAIN = 0.70710678f;
static constexpr float AUDIO_OUT2_LFE_GAIN = 0.31622777f;

static constexpr std::size_t AUDIO_OUT2_OUTPUT_FRAME_BYTES = AUDIO_OUT2_OUTPUT_CHANNELS * sizeof(float);
static constexpr std::uint32_t AUDIO_OUT2_OUTPUT_BYTES_PER_MS =
    AUDIO_OUT2_SAMPLE_RATE * AUDIO_OUT2_OUTPUT_FRAME_BYTES / 1000;

// Decodes the port channel count from data_format. Returns 0 when the format
// is not understood; the caller then leaves the port unrendered instead of
// fabricating PCM.
inline std::uint32_t AudioOut2DecodeChannels(std::uint32_t dataFormat) {
    const std::uint32_t channels =
        (dataFormat >> AUDIO_OUT2_FORMAT_CHANNELS_SHIFT) & AUDIO_OUT2_FORMAT_CHANNELS_MASK;
    return (channels == 1 || channels == 2 || channels == 8) ? channels : 0;
}

// Sample encodings a port can carry in its PCM buffer.
enum class AudioOut2SampleType : std::uint8_t { Float, Int16, Unknown };

/**
 * @brief Decodes the sample type from data_format bits 0..6.
 * @param dataFormat The guest SceAudioOut2PortParam data_format word.
 * @return Float for 0, Int16 for 1, Unknown for anything else; the caller then leaves the port
 *         unrendered (and logs it) instead of guessing.
 */
inline AudioOut2SampleType AudioOut2DecodeSampleType(std::uint32_t dataFormat) {
    switch (dataFormat & AUDIO_OUT2_FORMAT_TYPE_MASK) {
        case 0: return AudioOut2SampleType::Float;
        case 1: return AudioOut2SampleType::Int16;
        default: return AudioOut2SampleType::Unknown;
    }
}

/**
 * @brief Loads one frame of an interleaved guest PCM buffer as floats.
 * @param data Guest buffer; the caller guarantees it is readable for (frame + 1) * channels samples
 *        (the port contract is one grain per push).
 * @param type Float (copied unchanged) or Int16 (divided by 32768, so -32768 maps to exactly -1.0).
 *        Unknown must not be passed; such ports are never rendered.
 * @param frame Zero-based frame index.
 * @param channels Samples per frame, 1..AUDIO_OUT2_PORT_CHANNELS_MAX.
 * @param out Receives `channels` floats; must have room for that many.
 */
inline void AudioOut2LoadFrame(const void* data, AudioOut2SampleType type, std::size_t frame,
                               std::uint32_t channels, float* out) {
    const std::size_t first = frame * channels;
    if (type == AudioOut2SampleType::Int16) {
        const auto* samples = static_cast<const std::int16_t*>(data);
        for (std::uint32_t c = 0; c < channels; c++) out[c] = static_cast<float>(samples[first + c]) / 32768.0f;
    } else {
        // memcpy: the guest buffer is only 4-byte aligned by contract, and may not be float-aligned
        // for hostile input.
        std::memcpy(out, static_cast<const float*>(data) + first, channels * sizeof(float));
    }
}

// Downmixes one source frame onto the stereo pair, adding to out[0..1].
// in holds `channels` floats with per-channel gains in volume. Mono fans out,
// 2-7 channels mix the front pair directly, and 8 channels fold centre,
// rears and sides per the order above, with LFE folded at -10 dB.
inline void AudioOut2DownmixFrame(const float* in, std::uint32_t channels,
                                  const float* volume, float* out) {
    float left = 0.0f;
    float right = 0.0f;
    if (channels == 1) {
        left = right = in[0] * volume[0];
    } else if (channels < 8) {
        left = in[0] * volume[0];
        right = in[1] * volume[1];
    } else {
        const float centre = in[2] * volume[2] * AUDIO_OUT2_DOWNMIX_GAIN;
        const float lfe = in[3] * volume[3] * AUDIO_OUT2_LFE_GAIN;
        left = in[0] * volume[0] + centre + lfe +
               (in[4] * volume[4] + in[6] * volume[6]) * AUDIO_OUT2_DOWNMIX_GAIN;
        right = in[1] * volume[1] + centre + lfe +
                (in[5] * volume[5] + in[7] * volume[7]) * AUDIO_OUT2_DOWNMIX_GAIN;
    }
    out[0] += left;
    out[1] += right;
}

// Converts queued device bytes to milliseconds of output latency. Pure so the
// telemetry stays testable without an audio device.
inline double AudioOut2QueuedMs(std::uint32_t queuedBytes) {
    return static_cast<double>(queuedBytes) / static_cast<double>(AUDIO_OUT2_OUTPUT_BYTES_PER_MS);
}

// Maps pending (beyond-cushion) bytes onto the queue level the title sees:
// whole grains, capped at the queue depth. Pure for the same reason.
inline std::uint32_t AudioOut2QueueLevelForPending(std::uint32_t pendingBytes,
                                                   std::uint32_t grainBytes,
                                                   std::uint32_t queueDepth) {
    if (grainBytes == 0) return 0;
    return std::min(queueDepth, (pendingBytes + grainBytes - 1) / grainBytes);
}

struct AudioOut2Context;

struct AudioOut2Port {
    bool used = false;
    AudioOut2Context* context = nullptr;
    std::uint16_t type = 0;
    std::uint32_t dataFormat = 0;
    std::uint32_t samplingFreq = 0;
    std::uint32_t flags = 0;
    // Channel count decoded from dataFormat; 0 when the format is not understood (the port is then
    // not rendered), which includes an unknown sample type.
    std::uint32_t channels = 0;
    AudioOut2SampleType sampleType = AudioOut2SampleType::Float;
    // The PCM buffer (one grain, interleaved, float or signed 16-bit per sampleType) the title last
    // handed over through the data attribute. It is guest memory the title rewrites every tick, so
    // it is read when the context mixes, not when it is set.
    const void* data = nullptr;
    float volume[AUDIO_OUT2_PORT_CHANNELS_MAX] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::uint64_t dataSets = 0;
    std::uint64_t attributeTraces = 0;
};

struct AudioOut2Context {
    std::mutex lock;
    std::atomic<int> inFlight{0};
    std::uint32_t grain = AUDIO_OUT2_DEFAULT_GRAIN;
    std::uint32_t queueDepth = 1;
    // Fallback hardware queue model for a context without an SDL device: pushes that have not
    // finished playing, and the time the last of them finishes; playback is real time, so the queue
    // drains as the wall clock advances. With a device the SDL queue is the hardware queue.
    std::uint32_t queued = 0;
    std::chrono::steady_clock::time_point playHead;
    SDL_AudioDeviceID device = 0;
    AudioSource* source = nullptr;
    // Stereo float mix of the ports for one push.
    std::vector<float> mix;
    // Trace counters.
    std::uint64_t pushes = 0;
    std::uint64_t blockingPushes = 0;
    std::uint64_t fullRejects = 0;
    std::uint64_t advances = 0;
    std::uint64_t queueLevelPolls = 0;
    // M1 telemetry (docs/spec/audio.md Target design, results JSON
    // audio_underruns/audio_overrun_drops): underruns counts pushes that found
    // the device queue empty (the device starved and played the silence
    // cushion); overrunDrops counts grains dropped because the queue ran more
    // than 250 ms ahead. Both are per-context and mirrored into the process
    // aggregates read by the telemetry snapshot below.
    std::uint64_t underruns = 0;
    std::uint64_t overrunDrops = 0;
    std::uint64_t summaryPushes = 0;
    std::uint64_t summaryAdvances = 0;
    std::uint64_t summaryPolls = 0;
    // Largest sample magnitude of the grains mixed since the last summary: the mixed signal's level.
    float summaryPeak = 0.0f;
    std::chrono::steady_clock::time_point summaryStart;
};

// Process-wide M1 telemetry aggregates (mirrors of the per-context counters
// above, for the results JSON without walking live contexts).
struct AudioOut2Telemetry {
    std::uint64_t underruns = 0;
    std::uint64_t overrunDrops = 0;
};

// Reads the process aggregates. Lock-free; exact per push, eventually
// consistent across threads.
AudioOut2Telemetry AudioOut2TelemetrySnapshot();
// Output latency in milliseconds for one context: the device queue fill, or
// the wall-clock model's queued grains when there is no device. The video
// spec derives the FMV A/V offset from it.
double AudioOut2LatencyMs(AudioOut2ContextHandle ctx);
// Test hooks (non-export): drop the device so tests run deterministically on
// the wall-clock model, and reset the process aggregates between cases.
void AudioOut2ForceWallClockForTesting(AudioOut2ContextHandle ctx);
void AudioOut2ResetTelemetryForTesting();

// Mixes every port of the context that carries PCM data into out (stereo float, frames frames), summing
// onto the zeroed buffer. Returns the number of ports mixed.
std::uint32_t AudioOut2MixPorts(const AudioOut2Context& context, float* out, std::uint32_t frames);
// Forgets the ports of a context being destroyed.
void AudioOut2ReleasePorts(const AudioOut2Context& context);

#endif
