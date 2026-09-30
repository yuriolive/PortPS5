// AudioMixer.cpp
// PortPS5 - Single Host Audio Mixer Implementation (Audio Subsystem M2)
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libSceAudioOut. Manages process-wide audio output,
//   device lifecycle, lock-free ring buffering, and real-time mixing.
//
// Threading & Invariants:
//   - ProcessCallback runs strictly on the dedicated OS audio thread.
//   - Lock-free SPSC buffer decoupling ensures the audio callback never acquires
//     heap locks or invokes blocking kernel transitions.
//   - Guest push threads pace against ring fill using WaitOnAddress / WakeByAddressAll.

#include "AudioMixer.hpp"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "prx/libc/include/General.hpp"

// ---------------------------------------------------------------------------
// AudioRingBuffer Implementation
// ---------------------------------------------------------------------------

bool AudioRingBuffer::Push(const AudioFrame* frames, std::uint32_t count) noexcept {
    if (!frames || count == 0) return true;
    const std::uint32_t w = m_writePos.load(std::memory_order_relaxed);
    const std::uint32_t r = m_readPos.load(std::memory_order_acquire);
    const std::uint32_t queued = w - r;

    // Reject and drop grain if pushing exceeds the 100 ms ring ceiling
    if (queued + count > AUDIO_MIXER_CEILING_FRAMES) {
        return false;
    }

    // Safety guard against ring buffer capacity overflow
    if (queued + count > AUDIO_MIXER_RING_CAPACITY) {
        return false;
    }

    for (std::uint32_t i = 0; i < count; i++) {
        m_buffer[(w + i) & AUDIO_MIXER_RING_MASK] = frames[i];
    }
    m_writePos.store(w + count, std::memory_order_release);
    WakeByAddressAll(&m_writePos);
    return true;
}

std::uint32_t AudioRingBuffer::Pop(AudioFrame* out, std::uint32_t count) noexcept {
    if (!out || count == 0) return 0;
    const std::uint32_t r = m_readPos.load(std::memory_order_relaxed);
    const std::uint32_t w = m_writePos.load(std::memory_order_acquire);
    const std::uint32_t queued = w - r;
    const std::uint32_t toRead = std::min(queued, count);

    for (std::uint32_t i = 0; i < toRead; i++) {
        out[i] = m_buffer[(r + i) & AUDIO_MIXER_RING_MASK];
    }
    m_readPos.store(r + toRead, std::memory_order_release);
    WakeByAddressAll(&m_readPos);
    return toRead;
}

void AudioRingBuffer::Clear() noexcept {
    const std::uint32_t w = m_writePos.load(std::memory_order_relaxed);
    m_readPos.store(w, std::memory_order_release);
    WakeByAddressAll(&m_readPos);
}

void AudioRingBuffer::WaitUntilQueuedAtMost(std::uint32_t targetFrames, std::uint32_t timeoutMs) noexcept {
    const auto start = std::chrono::steady_clock::now();
    while (QueuedFrames() > targetFrames) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsedMs >= static_cast<long long>(timeoutMs)) {
            break;
        }
        const std::uint32_t remainingMs = static_cast<std::uint32_t>(timeoutMs - elapsedMs);
        std::uint32_t currentRead = m_readPos.load(std::memory_order_acquire);
        WaitOnAddress(&m_readPos, &currentRead, sizeof(currentRead), std::min(remainingMs, 5u));
    }
}

void AudioRingBuffer::Drain(std::uint32_t timeoutMs) noexcept {
    WaitUntilQueuedAtMost(0, timeoutMs);
}

std::uint32_t AudioRingBuffer::ReadPos() const noexcept {
    return m_readPos.load(std::memory_order_acquire);
}

void AudioRingBuffer::WaitForReadProgress(std::uint32_t observedRead, std::uint32_t timeoutMs) noexcept {
    std::uint32_t expected = observedRead;
    WaitOnAddress(&m_readPos, &expected, sizeof(expected), timeoutMs);
}

// ---------------------------------------------------------------------------
// AudioSource Implementation
// ---------------------------------------------------------------------------

AudioSource::AudioSource() = default;

AudioSource::~AudioSource() {
    Reset();
}

void AudioSource::Init(std::uint32_t sampleRate, std::uint32_t channels) {
    Reset();
    m_sampleRate = sampleRate ? sampleRate : AUDIO_MIXER_SAMPLE_RATE;
    m_channels = channels ? channels : AUDIO_MIXER_CHANNELS;
    // Sources start paused: an open-but-never-fed source must not count a
    // callback shortfall as an underrun. The first pushed grain unpauses.
    m_paused.store(true, std::memory_order_relaxed);

    if (m_sampleRate != AUDIO_MIXER_SAMPLE_RATE) {
        std::lock_guard lock(m_resamplerLock);
        m_resampler = SDL_NewAudioStream(
            AUDIO_F32SYS, static_cast<Uint8>(AUDIO_MIXER_CHANNELS), static_cast<int>(m_sampleRate),
            AUDIO_F32SYS, static_cast<Uint8>(AUDIO_MIXER_CHANNELS), static_cast<int>(AUDIO_MIXER_SAMPLE_RATE));
    }
    m_state.store(kActive, std::memory_order_release);
}

void AudioSource::Reset() {
    m_state.fetch_and(~kActive, std::memory_order_acq_rel);
    // Only lifecycle callers wait. A callback either owns the consuming bit
    // already or fails admission; it never waits for Reset/Init.
    while ((m_state.load(std::memory_order_acquire) & kConsuming) != 0) {
        std::this_thread::yield();
    }
    m_paused.store(true, std::memory_order_relaxed);
    m_ring.Clear();
    std::lock_guard lock(m_resamplerLock);
    if (m_resampler) {
        SDL_FreeAudioStream(m_resampler);
        m_resampler = nullptr;
    }
}

/** Atomically pins a live ring before callback selection, excluding other consumers. */
bool AudioSource::TryAcquireConsumer() noexcept {
    std::uint32_t expected = kActive;
    return m_state.compare_exchange_strong(expected, kActive | kConsuming,
                                           std::memory_order_acquire, std::memory_order_relaxed);
}

/** Publishes completion of all ring reads before lifecycle code may clear or reuse it. */
void AudioSource::ReleaseConsumer() noexcept {
    m_state.fetch_and(~kConsuming, std::memory_order_release);
}

bool AudioSource::PushStereo48k(const AudioFrame* frames, std::uint32_t count) {
    // Retire elapsed wall-clock frames first so a no-device ring reflects
    // real-time playback instead of filling to the ceiling between polls.
    AudioMixer::Get().PumpWallClock();
    if (count > 0) {
        // First data unpauses: only sources that have received audio can
        // starve the device, so only they count callback underruns.
        m_paused.store(false, std::memory_order_release);
    }
    const bool pushed = m_ring.Push(frames, count);
    if (!pushed) {
        AudioMixer::Get().RecordOverrunDrop();
    }
    return pushed;
}

void AudioSource::WaitUntilQueuedAtMost(std::uint32_t targetFrames, std::uint32_t timeoutMs) noexcept {
    const auto start = std::chrono::steady_clock::now();
    while (m_ring.QueuedFrames() > targetFrames) {
        // Retire wall-clock elapsed frames each iteration so a no-device wait
        // observes real-time drain instead of stalling to the timeout.
        AudioMixer::Get().PumpWallClock();
        if (m_ring.QueuedFrames() <= targetFrames) break;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsedMs >= static_cast<long long>(timeoutMs)) break;
        const std::uint32_t remainingMs = static_cast<std::uint32_t>(timeoutMs - elapsedMs);
        m_ring.WaitForReadProgress(m_ring.ReadPos(), std::min(remainingMs, 5u));
    }
}

void AudioSource::Drain(std::uint32_t timeoutMs) noexcept {
    WaitUntilQueuedAtMost(0, timeoutMs);
}

bool AudioSource::PushAndResample(const AudioFrame* frames, std::uint32_t inCount) {
    std::vector<AudioFrame> staged;
    if (!ResampleStereo(frames, inCount, staged)) {
        return false;
    }
    return PushStereo48k(staged.data(), static_cast<std::uint32_t>(staged.size()));
}

bool AudioSource::ResampleStereo(const AudioFrame* frames, std::uint32_t inCount,
                                 std::vector<AudioFrame>& out) {
    if (inCount == 0) {
        out.clear();
        return true;
    }
    if (m_sampleRate == AUDIO_MIXER_SAMPLE_RATE || !m_resampler) {
        out.assign(frames, frames + inCount);
        return true;
    }
    std::lock_guard lock(m_resamplerLock);
    if (SDL_AudioStreamPut(m_resampler, frames, static_cast<int>(inCount * sizeof(AudioFrame))) < 0) {
        return false;
    }
    const int availBytes = SDL_AudioStreamAvailable(m_resampler);
    if (availBytes < static_cast<int>(sizeof(AudioFrame))) {
        out.clear();
        return true;
    }
    const int outFrames = availBytes / static_cast<int>(sizeof(AudioFrame));
    out.resize(outFrames);
    const int got = SDL_AudioStreamGet(m_resampler, out.data(), availBytes);
    if (got <= 0) {
        out.clear();
        return true;
    }
    out.resize(static_cast<std::uint32_t>(got) / sizeof(AudioFrame));
    return true;
}

// ---------------------------------------------------------------------------
// AudioMixer Implementation
// ---------------------------------------------------------------------------

AudioMixer& AudioMixer::Get() {
    static AudioMixer s_instance;
    return s_instance;
}

AudioMixer::AudioMixer() {
    m_lastWallClockTime = std::chrono::steady_clock::now();
}

AudioMixer::~AudioMixer() {
    Shutdown();
}

bool AudioMixer::Initialize() {
    std::lock_guard lock(m_initMutex);
    if (m_initialized) {
        return true;
    }
    if (!OpenDevice()) {
        std::fprintf(stderr, "[audio] failed to open host device: %s\n", SDL_GetError());
        return false;
    }
    m_initialized = true;
    return true;
}

void AudioMixer::Shutdown() {
    std::lock_guard lock(m_initMutex);
    CloseDevice();
    for (auto& source : m_sources) {
        source.Reset();
    }
    m_initialized = false;
}

bool AudioMixer::OpenDevice() {
    // WASAPI shared mode preferred by PortPS5 target design.
    // 0 specifies not to overwrite if SDL_AUDIODRIVER was already configured (e.g. dummy in tests).
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", 0);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(AUDIO_MIXER_SAMPLE_RATE);
    desired.format = AUDIO_F32SYS;
    desired.channels = static_cast<Uint8>(AUDIO_MIXER_CHANNELS);
    desired.samples = 512;
    desired.callback = AudioCallback;
    desired.userdata = this;

    m_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &m_obtainedSpec, 0);
    if (m_device == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    // Finish any no-device retirement before the SDL consumer starts.
    std::lock_guard clockLock(m_wallClockMutex);
    SDL_PauseAudioDevice(m_device, 0);
    return true;
}

void AudioMixer::CloseDevice() {
    if (m_device != 0 && SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        SDL_CloseAudioDevice(m_device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    m_device = 0;
    m_obtainedSpec = {};
}

AudioSource* AudioMixer::RegisterSource(std::uint32_t sampleRate, std::uint32_t channels) {
    Initialize();
    std::lock_guard lock(m_sourcesMutex);
    for (auto& source : m_sources) {
        if (!source.IsActive()) {
            source.Init(sampleRate, channels);
            return &source;
        }
    }
    return nullptr;
}

void AudioMixer::UnregisterSource(AudioSource* source) {
    if (!source) return;
    std::lock_guard lock(m_sourcesMutex);
    source->Reset();
}

std::uint64_t AudioMixer::GetFramesConsumed() const noexcept {
    const_cast<AudioMixer*>(this)->UpdateWallClockFallback();
    return m_framesConsumed.load(std::memory_order_relaxed);
}

double AudioMixer::GetLatencyMs() const noexcept {
    const_cast<AudioMixer*>(this)->UpdateWallClockFallback();
    double devicePeriodMs = 0.0;
    if (m_device != 0 && m_obtainedSpec.freq > 0) {
        devicePeriodMs = (static_cast<double>(m_obtainedSpec.samples) * 1000.0) / m_obtainedSpec.freq;
    }
    std::uint32_t maxQueued = 0;
    for (const auto& source : m_sources) {
        if (source.IsActive()) {
            maxQueued = std::max(maxQueued, source.GetQueuedFrames());
        }
    }
    const double ringFillMs = (static_cast<double>(maxQueued) * 1000.0) / AUDIO_MIXER_SAMPLE_RATE;
    return ringFillMs + devicePeriodMs;
}

std::uint64_t AudioMixer::GetUnderruns() const noexcept {
    return m_underruns.load(std::memory_order_relaxed);
}

std::uint64_t AudioMixer::GetOverrunDrops() const noexcept {
    return m_overrunDrops.load(std::memory_order_relaxed);
}

void AudioMixer::RecordOverrunDrop() noexcept {
    m_overrunDrops.fetch_add(1, std::memory_order_relaxed);
    if (AudioOut2TraceEnabled()) {
        std::fprintf(stderr, "[audio.overrun_drop] t=%.3f overrun drop: ring past 100ms ceiling\n",
                     AudioOut2TraceSeconds());
    }
}

void AudioMixer::ResetTelemetryForTesting() noexcept {
    m_underruns.store(0, std::memory_order_relaxed);
    m_overrunDrops.store(0, std::memory_order_relaxed);
    m_framesConsumed.store(0, std::memory_order_relaxed);
}

void AudioMixer::ForceWallClockForTesting() noexcept {
    std::lock_guard lock(m_initMutex);
    CloseDevice();
    std::lock_guard clockLock(m_wallClockMutex);
    m_lastWallClockTime = std::chrono::steady_clock::now();
}

void AudioMixer::SimulateCallback(std::uint32_t framesNeeded) {
    if (framesNeeded == 0) {
        UpdateWallClockFallback();
        return;
    }
    std::lock_guard lock(m_wallClockMutex);
    ProcessCallback(nullptr, framesNeeded);
    m_lastWallClockTime = std::chrono::steady_clock::now();
}

void AudioMixer::UpdateWallClockFallback() {
    // Test freeze for deterministic orchestration: queued audio persists until
    // an explicit SimulateCallback/ProcessCallback drains it.
    if (m_wallClockPaused.load(std::memory_order_acquire)) return;
    std::lock_guard lock(m_wallClockMutex);
    if (m_device != 0) return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - m_lastWallClockTime;
    const double elapsedSec = std::chrono::duration<double>(elapsed).count();
    // 64-bit count: the previous 32-bit cast wrapped after ~24.8 h of fallback play.
    // Clamp negatives: a pump running on another thread can read a tick slightly
    // earlier than the stored anchor (unsynchronized timestamp counters), and an
    // unchecked cast would wrap to ~2^64 frames, draining every ring at once and
    // poisoning the anchor far into the future so all later pumps drain too.
    const auto framesToRetire = elapsedSec > 0.0
        ? static_cast<std::uint64_t>(elapsedSec * AUDIO_MIXER_SAMPLE_RATE)
        : 0ULL;

    if (framesToRetire > 0) {
        // Pop at most one ring capacity: no source ring holds more than that,
        // while telemetry still advances by the full elapsed count.
        const auto popCount = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(framesToRetire, AUDIO_MIXER_RING_CAPACITY));
        static thread_local std::array<AudioFrame, AUDIO_MIXER_RING_CAPACITY> discard;
        for (auto& source : m_sources) {
            if (source.TryAcquireConsumer()) {
                source.Pop(discard.data(), popCount);
                source.ReleaseConsumer();
            }
        }
        m_framesConsumed.fetch_add(framesToRetire, std::memory_order_relaxed);
        m_lastWallClockTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(framesToRetire) / AUDIO_MIXER_SAMPLE_RATE));
    }
}

void AudioMixer::PumpWallClock() noexcept {
    UpdateWallClockFallback();
}

void AudioMixer::PauseWallClockForTesting(bool paused) noexcept {
    m_wallClockPaused.store(paused, std::memory_order_release);
}

void AudioMixer::AudioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioMixer*>(userdata);
    const std::uint32_t framesNeeded = static_cast<std::uint32_t>(len) / (AUDIO_MIXER_CHANNELS * sizeof(float));
    self->ProcessCallback(reinterpret_cast<float*>(stream), framesNeeded);
}

void AudioMixer::ProcessCallback(float* stream, std::uint32_t framesNeeded) {
    std::array<AudioSource*, kMaxSources> activeSources{};
    std::size_t activeCount = 0;

    for (auto& source : m_sources) {
        if (source.TryAcquireConsumer()) {
            if (source.IsPaused()) {
                source.ReleaseConsumer();
            } else {
                activeSources[activeCount++] = &source;
            }
        }
    }

    if (activeCount == 0) {
        if (stream) {
            std::memset(stream, 0, framesNeeded * sizeof(AudioFrame));
        }
        m_framesConsumed.fetch_add(framesNeeded, std::memory_order_relaxed);
        return;
    }

    // Underrun detection: any active source with fewer queued frames than needed constitutes an underrun.
    bool shortfall = false;
    for (std::size_t i = 0; i < activeCount; i++) {
        if (activeSources[i]->GetQueuedFrames() < framesNeeded) {
            shortfall = true;
            break;
        }
    }

    if (shortfall) {
        const std::uint64_t u = m_underruns.fetch_add(1, std::memory_order_relaxed) + 1;
        if (AudioOut2TraceEnabled()) {
            std::fprintf(stderr, "[audio.underrun] t=%.3f underrun #%llu: shortfall in callback (needed %u frames)\n",
                         AudioOut2TraceSeconds(), static_cast<unsigned long long>(u), framesNeeded);
        }
    }

    for (std::uint32_t offset = 0; offset < framesNeeded;) {
        const auto chunk = std::min(kCallbackChunkFrames, framesNeeded - offset);
        std::fill_n(m_mixBuffer.begin(), chunk, AudioFrame{});
        for (std::size_t i = 0; i < activeCount; i++) {
            const auto popped = activeSources[i]->Pop(m_sourceBuffer.data(), chunk);
            for (std::uint32_t j = 0; j < popped; j++) {
                m_mixBuffer[j].left += m_sourceBuffer[j].left;
                m_mixBuffer[j].right += m_sourceBuffer[j].right;
            }
        }
        if (stream) {
            for (std::uint32_t j = 0; j < chunk; j++) {
                stream[(offset + j) * 2 + 0] = SoftLimit(m_mixBuffer[j].left);
                stream[(offset + j) * 2 + 1] = SoftLimit(m_mixBuffer[j].right);
            }
        }
        offset += chunk;
    }
    for (std::size_t i = 0; i < activeCount; i++) {
        activeSources[i]->ReleaseConsumer();
    }

    m_framesConsumed.fetch_add(framesNeeded, std::memory_order_relaxed);
}

extern "C" {

/**
 * @brief Returns total frames consumed by the host audio device.
 * @return Monotonically advancing count of output frames consumed.
 */
std::uint64_t APS5_VABI AudioMixerGetFramesConsumed() noexcept {
    return AudioMixer::Get().GetFramesConsumed();
}

/**
 * @brief Returns current published output latency in milliseconds.
 * @return Estimated output latency (ring fill + device period).
 */
double APS5_VABI AudioMixerGetLatencyMs() noexcept {
    return AudioMixer::Get().GetLatencyMs();
}

/**
 * @brief Resets process-wide underrun and overrun telemetry counters for testing.
 */
void APS5_VABI AudioMixerResetTelemetryForTesting() noexcept {
    AudioMixer::Get().ResetTelemetryForTesting();
}

}
