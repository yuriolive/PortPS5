// AudioMixer.hpp
// PortPS5 - Single Host Audio Mixer (Audio Subsystem M2)
//
// Purpose:
//   Single process-wide host audio mixer for PortPS5. Consolidates all AudioOut
//   v1 ports and AudioOut2 contexts onto one SDL audio device (48 kHz stereo F32
//   WASAPI shared mode) using callback-driven lock-free ring buffers.
//
// Threading & Lifecycle:
//   - Initialized on first audio use, persistent across process execution.
//   - SDL audio callback runs on a dedicated OS audio thread.
//   - SPSC lock-free ring buffers buffer frames between guest threads and the
//     callback thread; the callback thread never takes locks or allocates memory.
//   - Pacing uses Windows Futex primitives (WaitOnAddress / WakeByAddressAll).

#ifndef CORE_LIBS_PRX_LIBSCEAUDIOOUT_SRC_AUDIOMIXER_HPP
#define CORE_LIBS_PRX_LIBSCEAUDIOOUT_SRC_AUDIOMIXER_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "SDL.h"
#include "prx/libc/include/general/VabiMacros.hpp"

// Forward declaration of trace helpers
bool AudioOut2TraceEnabled();
double AudioOut2TraceSeconds();

// Standard mixer constants (M2 spec)
constexpr std::uint32_t AUDIO_MIXER_SAMPLE_RATE = 48000;
constexpr std::uint32_t AUDIO_MIXER_CHANNELS = 2;
constexpr std::size_t AUDIO_MIXER_FRAME_BYTES = AUDIO_MIXER_CHANNELS * sizeof(float);

// Ring buffer sizing:
// Target cushion: 40 ms (1920 frames at 48 kHz)
// Maximum ceiling: 100 ms (4800 frames at 48 kHz); grains beyond ceiling are dropped.
// Capacity: 8192 frames (power of 2, 64 KiB storage per source ring).
constexpr std::uint32_t AUDIO_MIXER_TARGET_CUSHION_MS = 40;
constexpr std::uint32_t AUDIO_MIXER_TARGET_CUSHION_FRAMES = (AUDIO_MIXER_SAMPLE_RATE * AUDIO_MIXER_TARGET_CUSHION_MS) / 1000;
constexpr std::uint32_t AUDIO_MIXER_CEILING_MS = 100;
constexpr std::uint32_t AUDIO_MIXER_CEILING_FRAMES = (AUDIO_MIXER_SAMPLE_RATE * AUDIO_MIXER_CEILING_MS) / 1000;
constexpr std::size_t AUDIO_MIXER_RING_CAPACITY = 8192;
constexpr std::size_t AUDIO_MIXER_RING_MASK = AUDIO_MIXER_RING_CAPACITY - 1;

// Downmix coefficients
// -3 dB = 10^(-3/20) = 0.70710678f (1 / sqrt(2))
// -10 dB = 10^(-10/20) = 0.31622777f (LFE fold per M2 spec)
constexpr float AUDIO_MIXER_DOWNMIX_3DB = 0.70710678f;
constexpr float AUDIO_MIXER_DOWNMIX_10DB = 0.31622777f;

/**
 * @brief Interleaved 32-bit floating-point stereo audio frame.
 */
struct AudioFrame {
    float left = 0.0f;
    float right = 0.0f;
};

/**
 * @brief Soft limiter applying smooth hyperbolic tangent compression above threshold.
 *
 * Linearly transparent for amplitudes |x| <= 0.8. Above 0.8, smoothly saturates
 * toward +/- 1.0 with a continuous derivative at the transition point.
 *
 * @param x Input audio sample.
 * @return Soft-limited sample strictly within [-1.0, 1.0].
 */
inline float SoftLimit(float x) noexcept {
    constexpr float kThreshold = 0.8f;
    constexpr float kMargin = 1.0f - kThreshold; // 0.2f
    const float absX = std::abs(x);
    if (absX <= kThreshold) {
        return x;
    }
    const float excess = absX - kThreshold;
    const float compressed = kThreshold + kMargin * std::tanh(excess / kMargin);
    return std::copysign(std::min(compressed, 1.0f), x);
}

/**
 * @brief Downmixes an 8-channel bed frame to stereo with LFE folded at -10 dB.
 *
 * 8-channel layout:
 *   [0]: Front Left (FL)
 *   [1]: Front Right (FR)
 *   [2]: Centre (C) -> folded at -3 dB into both Left and Right
 *   [3]: Low Frequency Effects (LFE) -> folded at -10 dB into both Left and Right
 *   [4]: Rear Left (RL) -> folded at -3 dB into Left
 *   [5]: Rear Right (RR) -> folded at -3 dB into Right
 *   [6]: Side Left (SL) -> folded at -3 dB into Left
 *   [7]: Side Right (SR) -> folded at -3 dB into Right
 *
 * @param in Pointer to 8 input float samples.
 * @param volume Optional per-channel volume multipliers (normalized to 1.0 = unity).
 * @param outL Reference receiving the mixed Left channel sample.
 * @param outR Reference receiving the mixed Right channel sample.
 */
inline void AudioMixerDownmix8Ch(const float* in, const float* volume, float& outL, float& outR) noexcept {
    const float fl  = in[0] * (volume ? volume[0] : 1.0f);
    const float fr  = in[1] * (volume ? volume[1] : 1.0f);
    const float c   = in[2] * (volume ? volume[2] : 1.0f) * AUDIO_MIXER_DOWNMIX_3DB;
    const float lfe = in[3] * (volume ? volume[3] : 1.0f) * AUDIO_MIXER_DOWNMIX_10DB;
    const float rl  = in[4] * (volume ? volume[4] : 1.0f) * AUDIO_MIXER_DOWNMIX_3DB;
    const float rr  = in[5] * (volume ? volume[5] : 1.0f) * AUDIO_MIXER_DOWNMIX_3DB;
    const float sl  = in[6] * (volume ? volume[6] : 1.0f) * AUDIO_MIXER_DOWNMIX_3DB;
    const float sr  = in[7] * (volume ? volume[7] : 1.0f) * AUDIO_MIXER_DOWNMIX_3DB;

    outL = fl + c + lfe + rl + sl;
    outR = fr + c + lfe + rr + sr;
}

/**
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) ring buffer for stereo audio frames.
 */
class AudioRingBuffer {
public:
    AudioRingBuffer() = default;

    /**
     * @brief Number of frames currently queued and ready to be read.
     */
    std::uint32_t QueuedFrames() const noexcept {
        const std::uint32_t w = m_writePos.load(std::memory_order_acquire);
        const std::uint32_t r = m_readPos.load(std::memory_order_relaxed);
        return w - r;
    }

    /**
     * @brief Pushes stereo frames into the ring buffer.
     *
     * Drops the grain and returns false if adding count frames would exceed the
     * 100 ms ring ceiling (4800 frames).
     */
    bool Push(const AudioFrame* frames, std::uint32_t count) noexcept;

    /**
     * @brief Pops up to count frames from the ring buffer into out.
     *
     * @return Number of frames actually read.
     */
    std::uint32_t Pop(AudioFrame* out, std::uint32_t count) noexcept;

    /**
     * @brief Clears all queued frames.
     */
    void Clear() noexcept;

    /**
     * @brief Waits until queued frames drop to or below targetFrames.
     */
    void WaitUntilQueuedAtMost(std::uint32_t targetFrames, std::uint32_t timeoutMs) noexcept;

    /**
     * @brief Drains all queued frames up to timeoutMs.
     */
    void Drain(std::uint32_t timeoutMs) noexcept;

private:
    std::atomic<std::uint32_t> m_writePos{0};
    std::atomic<std::uint32_t> m_readPos{0};
    std::array<AudioFrame, AUDIO_MIXER_RING_CAPACITY> m_buffer{};
};

/**
 * @brief An active audio stream source attached to the host mixer.
 */
class AudioSource {
public:
    AudioSource();
    ~AudioSource();

    /**
     * @brief Initializes a source after retiring any selected consumer.
     * @param sampleRate Input rate in Hz (zero selects 48 kHz).
     * @param channels Input channel count (zero selects stereo).
     * Lifecycle calls and producers must be externally serialized.
     */
    void Init(std::uint32_t sampleRate, std::uint32_t channels);
    /**
     * @brief Closes consumer admission and waits before clearing queued frames.
     * Callers must first quiesce producers and serialize lifecycle calls.
     */
    void Reset();

    bool IsActive() const noexcept { return (m_state.load(std::memory_order_acquire) & kActive) != 0; }
    bool IsPaused() const noexcept { return m_paused.load(std::memory_order_relaxed); }
    void SetPaused(bool paused) noexcept { m_paused.store(paused, std::memory_order_release); }

    std::uint32_t GetQueuedFrames() const noexcept { return m_ring.QueuedFrames(); }
    std::uint32_t GetSampleRate() const noexcept { return m_sampleRate; }
    std::uint32_t GetChannels() const noexcept { return m_channels; }

    bool PushStereo48k(const AudioFrame* frames, std::uint32_t count);
    bool PushAndResample(const AudioFrame* frames, std::uint32_t inCount);

    void WaitUntilQueuedAtMost(std::uint32_t targetFrames, std::uint32_t timeoutMs) noexcept {
        m_ring.WaitUntilQueuedAtMost(targetFrames, timeoutMs);
    }
    void Drain(std::uint32_t timeoutMs) noexcept {
        m_ring.Drain(timeoutMs);
    }

private:
    friend class AudioMixer;
    friend struct AudioMixerTestAccess;

    // Selection and retirement share one atomic word: Reset closes admission
    // before waiting for the selected consumer to acknowledge its final Pop.
    static constexpr std::uint32_t kActive = 1;
    static constexpr std::uint32_t kConsuming = 2;
    bool TryAcquireConsumer() noexcept;
    void ReleaseConsumer() noexcept;
    std::uint32_t Pop(AudioFrame* out, std::uint32_t count) noexcept { return m_ring.Pop(out, count); }
    std::atomic<std::uint32_t> m_state{0};
    std::atomic<bool> m_paused{false};
    std::uint32_t m_sampleRate = AUDIO_MIXER_SAMPLE_RATE;
    std::uint32_t m_channels = AUDIO_MIXER_CHANNELS;
    AudioRingBuffer m_ring;

    SDL_AudioStream* m_resampler = nullptr;
    std::mutex m_resamplerLock;
};

/**
 * @brief Process-wide single host audio mixer.
 */
class AudioMixer {
public:
    static AudioMixer& Get();

    bool Initialize();
    void Shutdown();

    AudioSource* RegisterSource(std::uint32_t sampleRate, std::uint32_t channels);
    void UnregisterSource(AudioSource* source);

    std::uint64_t GetFramesConsumed() const noexcept;
    double GetLatencyMs() const noexcept;
    std::uint64_t GetUnderruns() const noexcept;
    std::uint64_t GetOverrunDrops() const noexcept;

    bool HasDevice() const noexcept { return m_device != 0; }
    void RecordOverrunDrop() noexcept;

    void ResetTelemetryForTesting() noexcept;
    void ForceWallClockForTesting() noexcept;

    /**
     * @brief Direct callback processing for simulated tests and headless verification.
     * @param framesNeeded Frames to consume; zero advances the wall-clock fallback.
     * Stop the host device with ForceWallClockForTesting before simulation.
     */
    void SimulateCallback(std::uint32_t framesNeeded);

private:
    friend struct AudioMixerTestAccess;

    AudioMixer();
    ~AudioMixer();

    bool OpenDevice();
    void CloseDevice();
    void UpdateWallClockFallback();

    static void AudioCallback(void* userdata, Uint8* stream, int len);
    void ProcessCallback(float* stream, std::uint32_t framesNeeded);

    std::mutex m_initMutex;
    bool m_initialized = false;
    std::atomic<SDL_AudioDeviceID> m_device{0};
    SDL_AudioSpec m_obtainedSpec{};

    static constexpr std::size_t kMaxSources = 48;
    std::array<AudioSource, kMaxSources> m_sources;
    std::mutex m_sourcesMutex;

    // Only the single SDL callback uses these buffers. Simulated callbacks run
    // with the device stopped and are serialized with fallback retirement.
    static constexpr std::uint32_t kCallbackChunkFrames = 512;
    std::array<AudioFrame, kCallbackChunkFrames> m_mixBuffer{};
    std::array<AudioFrame, kCallbackChunkFrames> m_sourceBuffer{};

    std::atomic<std::uint64_t> m_framesConsumed{0};
    std::atomic<std::uint64_t> m_underruns{0};
    std::atomic<std::uint64_t> m_overrunDrops{0};

    std::chrono::steady_clock::time_point m_lastWallClockTime;
    std::mutex m_wallClockMutex;
};

extern "C" {
/**
 * @brief Returns total frames consumed by the host audio device.
 */
std::uint64_t APS5_VABI AudioMixerGetFramesConsumed() noexcept;

/**
 * @brief Returns current published output latency in milliseconds.
 */
double APS5_VABI AudioMixerGetLatencyMs() noexcept;

/**
 * @brief Resets process-wide underrun and overrun telemetry counters for testing.
 */
void APS5_VABI AudioMixerResetTelemetryForTesting() noexcept;
}

#endif // CORE_LIBS_PRX_LIBSCEAUDIOOUT_SRC_AUDIOMIXER_HPP
