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
     * @param frames Stereo frames to copy. @param count Frame count.
     * @return True if queued, false if dropped. Single producer only.
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

    /**
     * @brief Observes the current read position for WaitOnAddress-based waits.
     */
    std::uint32_t ReadPos() const noexcept;

    /**
     * @brief Waits until the read position advances past observedRead, or timeout elapses.
     */
    void WaitForReadProgress(std::uint32_t observedRead, std::uint32_t timeoutMs) noexcept;

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
    /** @brief Constructs an inactive source; call Init before use. */
    AudioSource();
    /** @brief Retires the source via Reset (waits for any selected consumer). */
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

    /** @brief Whether the source is registered and visible to the callback. @return True if active. */
    bool IsActive() const noexcept { return (m_state.load(std::memory_order_acquire) & kActive) != 0; }
    /** @brief Whether the callback skips this source. @return True if paused (sources start paused). */
    bool IsPaused() const noexcept { return m_paused.load(std::memory_order_relaxed); }
    /** @brief Pauses or resumes consumption. @param paused True to pause. Lock-free. */
    void SetPaused(bool paused) noexcept { m_paused.store(paused, std::memory_order_release); }

    /** @brief Frames queued in the ring. @return Queued frame count (48 kHz stereo). */
    std::uint32_t GetQueuedFrames() const noexcept { return m_ring.QueuedFrames(); }
    /** @brief Guest input rate. @return Sample rate in Hz. */
    std::uint32_t GetSampleRate() const noexcept { return m_sampleRate; }
    /** @brief Guest input channel count. @return Channel count. */
    std::uint32_t GetChannels() const noexcept { return m_channels; }

    /**
     * @brief Pushes already-48 kHz stereo frames and unpauses the source.
     * @param frames Stereo frames. @param count Frame count.
     * @return True if queued. False when the grain would exceed the 100 ms
     *         ceiling; the grain is dropped and an overrun drop is recorded.
     */
    bool PushStereo48k(const AudioFrame* frames, std::uint32_t count);
    /**
     * @brief Resamples then pushes (see ResampleStereo and PushStereo48k).
     * @param frames Input-rate stereo frames. @param inCount Input frame count.
     * @return True if queued; false on resampler failure or ceiling overrun.
     */
    bool PushAndResample(const AudioFrame* frames, std::uint32_t inCount);

    /**
     * @brief Resamples stereo frames to 48 kHz into out without touching the ring.
     *
     * Lets callers stage a grain once and retry only the ring push: re-feeding
     * the stream on every retry would lose already-resampled frames and bloat
     * the resampler. Keeps the stream's filter history across grains.
     * @return False only when the SDL stream itself rejects the input.
     */
    bool ResampleStereo(const AudioFrame* frames, std::uint32_t inCount,
                        std::vector<AudioFrame>& out);

    /**
     * @brief Waits until queued frames drop to or below targetFrames.
     *
     * Pumps the wall-clock fallback each iteration so a no-device ring drains
     * in real time instead of stalling to the timeout.
     */
    void WaitUntilQueuedAtMost(std::uint32_t targetFrames, std::uint32_t timeoutMs) noexcept;
    /**
     * @brief Drains all queued frames up to timeoutMs.
     */
    void Drain(std::uint32_t timeoutMs) noexcept;

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
    /** @brief Process-wide instance. @return The singleton mixer. */
    static AudioMixer& Get();

    /**
     * @brief Opens the single SDL device (idempotent once successful).
     * @return True if a device is open. False on device-open failure, in which
     *         case the mixer stays uninitialized and Initialize may be retried;
     *         callers then run on the wall-clock fallback.
     */
    bool Initialize();
    /** @brief Closes the device and resets all sources. Callers quiesce producers first. */
    void Shutdown();

    /**
     * @brief Claims a free source slot.
     * @param sampleRate Guest rate in Hz. @param channels Guest channel count.
     * @return The source, or nullptr when all 48 slots are occupied.
     */
    AudioSource* RegisterSource(std::uint32_t sampleRate, std::uint32_t channels);
    /** @brief Releases a slot from RegisterSource. @param source Source (nullptr ignored). */
    void UnregisterSource(AudioSource* source);

    /** @brief Master output clock. @return Frames consumed since start/reset. */
    std::uint64_t GetFramesConsumed() const noexcept;
    /** @brief Output latency (ring fill + device period). @return Milliseconds. */
    double GetLatencyMs() const noexcept;
    /** @brief Callbacks that found an active unpaused source short of frames. @return Count. */
    std::uint64_t GetUnderruns() const noexcept;
    /** @brief Grains dropped past the 100 ms ceiling. @return Count. */
    std::uint64_t GetOverrunDrops() const noexcept;
    /** @brief Count of blocking pushes that hit the 200 ms timeout (stuck-device stall input). */
    std::uint64_t GetStalls() const noexcept;

    /** @brief Whether an SDL device is open. @return True if the callback is the consumer. */
    bool HasDevice() const noexcept { return m_device != 0; }
    /** @brief Records one overrun drop (audio.overrun_drop). Lock-free. */
    void RecordOverrunDrop() noexcept;
    /** @brief Records one blocking-push timeout (audio.stall). Thread-safe, lock-free. */
    void RecordStall() noexcept;

    /**
     * @brief Retires wall-clock elapsed frames when no device is open.
     *
     * No-op while a device is open (the SDL callback is the consumer there).
     * Guest push/wait paths call this so no-device rings drain in real time.
     * Never called from the audio callback.
     */
    void PumpWallClock() noexcept;

    void ResetTelemetryForTesting() noexcept;
    void ForceWallClockForTesting() noexcept;

    /**
     * @brief Freezes wall-clock retirement for deterministic tests.
     *
     * While paused, PumpWallClock retires nothing, so queued audio persists
     * until an explicit SimulateCallback/ProcessCallback drains it. Production
     * never pauses; tests pair this with ForceWallClockForTesting.
     */
    void PauseWallClockForTesting(bool paused) noexcept;

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
    std::atomic<std::uint64_t> m_stalls{0};

    std::chrono::steady_clock::time_point m_lastWallClockTime;
    std::mutex m_wallClockMutex;
    // Test-only freeze for deterministic orchestration tests; production never sets it.
    std::atomic<bool> m_wallClockPaused{false};
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
