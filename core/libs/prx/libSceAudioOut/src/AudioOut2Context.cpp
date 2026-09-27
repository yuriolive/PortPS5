#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/config/Config.hpp"
#include "AudioOut2Internal.hpp"

// An AudioOut2 context is the hardware output queue: every push mixes the ports' current grain
// (num_grains samples) and appends it to a queue of queue_depth grains that plays in real time. The
// guest title paces its mixer on that queue (it polls GetQueueLevel and pushes non-blocking
// whenever a slot is free), so the level must follow the output clock: with an SDL device open the
// device's own queue is the hardware queue, otherwise a wall-clock model of it stands in.

using Clock = std::chrono::steady_clock;

static constexpr std::size_t CONTEXT_MEMORY = 0x10000;
static constexpr std::uint32_t DEFAULT_MAX_PORTS = 16;
// Pushes, polls and advances traced individually before the trace falls back to the per-second summary.
static constexpr std::uint64_t CALL_TRACE_FULL = 16;
// Silence queued ahead of the first grain (and again whenever the SDL queue ran dry) so scheduling
// jitter of the pushing thread does not starve the device; the queue level reported to the title
// counts only what lies beyond it.
static constexpr std::uint32_t CUSHION_MS = 40;
// The SDL queue is not allowed to run further ahead than this; grains beyond it are dropped.
static constexpr std::uint32_t MAX_QUEUED_MS = 250;
// A blocking push on a full queue gives up after this long.
static constexpr std::chrono::milliseconds FULL_WAIT_TIMEOUT{200};
static constexpr std::chrono::milliseconds FULL_WAIT_STEP{1};
static constexpr std::uint16_t DEVICE_SAMPLES = 512;
// The summed ports (a multichannel bed folded to stereo plus the object ports) can peak above full
// scale; the device takes float and clips hard, so the mix is attenuated and clamped. The fixed
// gain is interim: the single host mixer replaces it with a soft limiter (docs/spec/audio.md
// Target design); until then this keeps loud passages from hard-clipping.
static constexpr float MASTER_GAIN = 0.5f;

static std::atomic<std::uint64_t> g_telemetryUnderruns{0};
static std::atomic<std::uint64_t> g_telemetryOverrunDrops{0};

bool AudioOut2TraceEnabled() {
    // The config initializes once at startup before guest threads run; before
    // that there is nothing to trace with, so tracing stays off. The value is
    // read on every call (never cached in a static) so tests that initialize
    // the config late still take effect.
    if (!PortPS5::Config::Loader::IsInitialized()) return false;
    const auto& trace = PortPS5::Config::Loader::Get().debug.trace;
    return trace.count(PortPS5::Config::TraceCategory::Audio) != 0;
}

double AudioOut2TraceSeconds() {
    static const auto start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

AudioOut2Telemetry AudioOut2TelemetrySnapshot() {
    AudioOut2Telemetry snapshot;
    snapshot.underruns = g_telemetryUnderruns.load(std::memory_order_relaxed);
    snapshot.overrunDrops = g_telemetryOverrunDrops.load(std::memory_order_relaxed);
    return snapshot;
}

void AudioOut2ResetTelemetryForTesting() {
    g_telemetryUnderruns.store(0, std::memory_order_relaxed);
    g_telemetryOverrunDrops.store(0, std::memory_order_relaxed);
}

static std::mutex g_contextsLock;
static std::set<AudioOut2Context*> g_liveContexts;

bool AudioOut2IsValidContext(AudioOut2ContextHandle ctx) {
    if (!ctx) return false;
    std::lock_guard lock(g_contextsLock);
    return g_liveContexts.count(reinterpret_cast<AudioOut2Context*>(ctx)) != 0;
}

static AudioOut2Context* FromHandle(AudioOut2ContextHandle ctx) {
    if (!ctx) return nullptr;
    std::lock_guard lock(g_contextsLock);
    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    if (g_liveContexts.count(context) == 0) return nullptr;
    return context;
}

static Clock::duration GrainDuration(const AudioOut2Context& context) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(static_cast<double>(context.grain) / AUDIO_OUT2_SAMPLE_RATE));
}

static std::uint32_t GrainBytes(const AudioOut2Context& context) {
    return static_cast<std::uint32_t>(context.grain * AUDIO_OUT2_OUTPUT_FRAME_BYTES);
}

static std::uint32_t SdlQueuedMs(const AudioOut2Context& context) {
    return context.device ? SDL_GetQueuedAudioSize(context.device) / AUDIO_OUT2_OUTPUT_BYTES_PER_MS : 0;
}

// Retires the modelled grains whose playback finished by now. The caller holds the context lock.
static void Drain(AudioOut2Context& context, Clock::time_point now) {
    const auto duration = GrainDuration(context);
    while (context.queued > 0) {
        const auto oldestEnd = context.playHead - duration * (context.queued - 1);
        if (oldestEnd > now) break;
        context.queued--;
    }
    if (context.queued == 0 && context.playHead < now) context.playHead = now;
}

// Grains queued and not yet played, as the title sees them. The caller holds the context lock.
static std::uint32_t QueueLevel(AudioOut2Context& context, Clock::time_point now) {
    Drain(context, now);
    if (context.device == 0) return context.queued;
    const auto queuedBytes = SDL_GetQueuedAudioSize(context.device);
    const auto cushionBytes = CUSHION_MS * AUDIO_OUT2_OUTPUT_BYTES_PER_MS;
    const auto pending = queuedBytes > cushionBytes ? queuedBytes - cushionBytes : 0;
    return AudioOut2QueueLevelForPending(pending, GrainBytes(context), context.queueDepth);
}

void AudioOut2ForceWallClockForTesting(AudioOut2ContextHandle ctx) {
    // Tests close the device so the wall-clock model (not the host audio
    // driver) decides queue levels, keeping assertions deterministic with or
    // without audio hardware.
    auto* context = FromHandle(ctx);
    if (!context) return;
    std::lock_guard lock(context->lock);
    if (context->device != 0 && SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        SDL_ClearQueuedAudio(context->device);
        SDL_CloseAudioDevice(context->device);
    }
    context->device = 0;
}

double AudioOut2LatencyMs(AudioOut2ContextHandle ctx) {
    auto* context = FromHandle(ctx);
    if (!context) return 0.0;
    std::lock_guard lock(context->lock);
    // With a device the queued device bytes are the latency; without one the
    // modelled grains still pending are, so the FMV A/V offset stays defined
    // on machines with no audio device.
    if (context->device != 0) {
        return AudioOut2QueuedMs(SDL_GetQueuedAudioSize(context->device));
    }
    Drain(*context, Clock::now());
    return 1000.0 * static_cast<double>(context->queued) *
           static_cast<double>(context->grain) / AUDIO_OUT2_SAMPLE_RATE;
}

static void OpenDevice(AudioOut2Context& context) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        AUDIOOUT2_TRACE("SDL audio init failed: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(AUDIO_OUT2_SAMPLE_RATE);
    desired.format = AUDIO_F32SYS;
    desired.channels = AUDIO_OUT2_OUTPUT_CHANNELS;
    desired.samples = DEVICE_SAMPLES;
    desired.callback = nullptr;
    SDL_AudioSpec obtained{};
    // No format change is allowed: SDL converts to the device's native format itself, so the queue
    // always takes stereo float at 48 kHz.
    context.device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (context.device == 0) {
        AUDIOOUT2_TRACE("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(context.device, 0);
    AUDIOOUT2_TRACE("SDL device %u opened: %d Hz, format 0x%x, %u channels, %u samples\n", context.device, obtained.freq, obtained.format, obtained.channels, obtained.samples);
}

static void CloseDevice(AudioOut2Context& context) {
    if (context.device != 0 && SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        SDL_ClearQueuedAudio(context.device);
        SDL_CloseAudioDevice(context.device);
    }
    context.device = 0;
}

// Mixes the ports' current grain and queues it on the SDL device. The caller holds the lock.
static std::uint32_t Render(AudioOut2Context& context) {
    std::fill(context.mix.begin(), context.mix.end(), 0.0f);
    const auto mixed = AudioOut2MixPorts(context, context.mix.data(), context.grain);
    for (float& sample : context.mix) {
        sample = std::clamp(sample * MASTER_GAIN, -1.0f, 1.0f);
        if (AudioOut2TraceEnabled()) context.summaryPeak = std::max(context.summaryPeak, std::abs(sample));
    }
    if (context.device == 0) return mixed;
    const auto queuedBytes = SDL_GetQueuedAudioSize(context.device);
    if (queuedBytes > MAX_QUEUED_MS * AUDIO_OUT2_OUTPUT_BYTES_PER_MS) {
        // The guest pushes faster than real time; dropping (never blocking
        // the guest) is what keeps the title at real-time pace, and the drop
        // is counted for telemetry.
        context.overrunDrops++;
        g_telemetryOverrunDrops.fetch_add(1, std::memory_order_relaxed);
        return mixed;
    }
    if (queuedBytes == 0) {
        // The queue ran dry since the last push: the device starved, so this
        // push counts one underrun for telemetry as well as repriming the
        // cushion.
        static const std::vector<float> silence(static_cast<std::size_t>(CUSHION_MS) * AUDIO_OUT2_SAMPLE_RATE / 1000 * AUDIO_OUT2_OUTPUT_CHANNELS, 0.0f);
        context.underruns++;
        g_telemetryUnderruns.fetch_add(1, std::memory_order_relaxed);
        SDL_QueueAudio(context.device, silence.data(), static_cast<Uint32>(silence.size() * sizeof(float)));
    }
    SDL_QueueAudio(context.device, context.mix.data(), GrainBytes(context));
    return mixed;
}

static void TraceSummary(AudioOut2Context& context, Clock::time_point now) {
    if (!AudioOut2TraceEnabled()) return;
    if (context.summaryStart == Clock::time_point{}) {
        context.summaryStart = now;
        return;
    }
    const auto elapsed = std::chrono::duration<double>(now - context.summaryStart).count();
    if (elapsed < 1.0) return;
    std::fprintf(stderr, "[audioout2] t=%.3f ctx %p: %.1f pushes/s, %.1f advances/s, %.1f queue polls/s, hw queue %u/%u, sdl queue %u ms, mix peak %.3f; totals: pushes %llu (%llu blocking, %llu queue-full rejects), underruns %llu, overrun drops %llu\n",
        AudioOut2TraceSeconds(), static_cast<void*>(&context), static_cast<double>(context.summaryPushes) / elapsed, static_cast<double>(context.summaryAdvances) / elapsed,
        static_cast<double>(context.summaryPolls) / elapsed, QueueLevel(context, now), context.queueDepth, SdlQueuedMs(context), static_cast<double>(context.summaryPeak),
        static_cast<unsigned long long>(context.pushes), static_cast<unsigned long long>(context.blockingPushes), static_cast<unsigned long long>(context.fullRejects),
        static_cast<unsigned long long>(context.underruns), static_cast<unsigned long long>(context.overrunDrops));
    context.summaryStart = now;
    context.summaryPushes = 0;
    context.summaryAdvances = 0;
    context.summaryPolls = 0;
    context.summaryPeak = 0.0f;
}

extern "C" {

// The title's per-tick step between setting the ports' data and pushing. The ports are mixed at push
// time from the buffers they currently point at, and the push paces the clock, so nothing is due here.
int APS5_VABI sceAudioOut2ContextAdvance(AudioOut2ContextHandle ctx) noexcept {
    auto* context = FromHandle(ctx);
    if (!context) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    std::lock_guard lock(context->lock);
    context->advances++;
    context->summaryAdvances++;
    if (context->advances <= CALL_TRACE_FULL) AUDIOOUT2_TRACE("t=%.3f Advance ctx %p (pushes so far %llu)\n", AudioOut2TraceSeconds(), static_cast<void*>(context), static_cast<unsigned long long>(context->pushes));
    return 0;
}

int APS5_VABI sceAudioOut2ContextCreate(const AudioOut2ContextParam* params, void* buffer, size_t buffer_size, AudioOut2ContextHandle* ctx) noexcept {
    if (!params || !ctx) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    if (params->num_grains > 192000) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    AudioOut2Context* context = nullptr;
    try {
        context = new AudioOut2Context();
        context->grain = params->num_grains ? params->num_grains : AUDIO_OUT2_DEFAULT_GRAIN;
        context->queueDepth = params->queue_depth ? params->queue_depth : 1;
        context->playHead = Clock::now();
        context->mix.assign(static_cast<std::size_t>(context->grain) * AUDIO_OUT2_OUTPUT_CHANNELS, 0.0f);
    } catch (const std::bad_alloc&) {
        delete context;
        return SCE_AUDIO_OUT2_ERROR_OUT_OF_MEMORY;
    }
    AUDIOOUT2_TRACE("t=%.3f ContextCreate: max_ports=%u max_object_ports=%u guarantee_object_ports=%u queue_depth=%u num_grains=%u flags=0x%x buffer=%p size=%zu -> ctx %p: %u-sample grains (%.2f ms), %u queued, %u Hz stereo float output\n",
        AudioOut2TraceSeconds(), params->max_ports, params->max_object_ports, params->guarantee_object_ports, params->queue_depth, params->num_grains, params->flags, buffer, buffer_size,
        static_cast<void*>(context), context->grain, 1000.0 * context->grain / AUDIO_OUT2_SAMPLE_RATE, context->queueDepth, AUDIO_OUT2_SAMPLE_RATE);
    OpenDevice(*context);
    {
        std::lock_guard lock(g_contextsLock);
        g_liveContexts.insert(context);
    }
    *ctx = reinterpret_cast<AudioOut2ContextHandle>(context);
    return 0;
}

int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle ctx) noexcept {
    AudioOut2Context* context = nullptr;
    {
        std::lock_guard lock(g_contextsLock);
        context = reinterpret_cast<AudioOut2Context*>(ctx);
        if (g_liveContexts.erase(context) == 0) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    }
    AUDIOOUT2_TRACE("t=%.3f ContextDestroy ctx %p after %llu pushes\n", AudioOut2TraceSeconds(), static_cast<void*>(context), static_cast<unsigned long long>(context->pushes));
    {
        std::lock_guard lock(context->lock);
        CloseDevice(*context);
    }
    AudioOut2ReleasePorts(*context);
    delete context;
    return 0;
}

int APS5_VABI sceAudioOut2ContextGetQueueLevel(AudioOut2ContextHandle ctx, uint32_t* queue_level, uint32_t* available_queue) noexcept {
    auto* context = FromHandle(ctx);
    if (!context) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    std::lock_guard lock(context->lock);
    const auto level = QueueLevel(*context, Clock::now());
    if (queue_level) *queue_level = level;
    if (available_queue) *available_queue = context->queueDepth - level;
    context->queueLevelPolls++;
    context->summaryPolls++;
    if (context->queueLevelPolls <= CALL_TRACE_FULL) AUDIOOUT2_TRACE("t=%.3f GetQueueLevel ctx %p -> level %u, available %u\n", AudioOut2TraceSeconds(), static_cast<void*>(context), level, context->queueDepth - level);
    return 0;
}

int APS5_VABI sceAudioOut2ContextPush(AudioOut2ContextHandle ctx, uint32_t blocking) noexcept {
    auto* context = FromHandle(ctx);
    if (!context) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    std::unique_lock lock(context->lock);
    auto now = Clock::now();
    const auto waitStart = now;
    while (QueueLevel(*context, now) >= context->queueDepth) {
        if (!blocking) {
            context->fullRejects++;
            if (context->fullRejects <= CALL_TRACE_FULL) AUDIOOUT2_TRACE("t=%.3f Push ctx %p non-blocking on a full queue (%u): rejected\n", AudioOut2TraceSeconds(), static_cast<void*>(context), context->queueDepth);
            return SCE_AUDIO_OUT2_ERROR_QUEUE_FULL;
        }
        if (now - waitStart > FULL_WAIT_TIMEOUT) break;
        lock.unlock();
        std::this_thread::sleep_for(FULL_WAIT_STEP);
        lock.lock();
        now = Clock::now();
    }
    if (QueueLevel(*context, now) >= context->queueDepth) {
        return SCE_AUDIO_OUT2_ERROR_QUEUE_FULL;
    }
    context->queued++;
    context->playHead += GrainDuration(*context);
    const auto mixed = Render(*context);
    context->pushes++;
    context->summaryPushes++;
    if (blocking) context->blockingPushes++;
    if (context->pushes <= CALL_TRACE_FULL) {
        AUDIOOUT2_TRACE("t=%.3f Push ctx %p blocking=%u: grain from %u ports, hw queue %u/%u, sdl queue %u ms\n", AudioOut2TraceSeconds(), static_cast<void*>(context), blocking,
            mixed, QueueLevel(*context, now), context->queueDepth, SdlQueuedMs(*context));
    }
    TraceSummary(*context, now);
    return 0;
}

int APS5_VABI sceAudioOut2ContextQueryMemory(const AudioOut2ContextParam* params, size_t* memory_size) noexcept {
    if (!params || !memory_size) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    *memory_size = CONTEXT_MEMORY;
    return 0;
}

int APS5_VABI sceAudioOut2ContextResetParam(AudioOut2ContextParam* params) noexcept {
    if (!params) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    std::memset(params, 0, sizeof(*params));
    params->max_ports = DEFAULT_MAX_PORTS;
    params->queue_depth = 1;
    params->num_grains = AUDIO_OUT2_DEFAULT_GRAIN;
    return 0;
}

int APS5_VABI sceAudioOut2ContextSetAttributes(AudioOut2ContextHandle ctx, const AudioOut2Attribute* attributes, uint32_t num) noexcept {
    auto* context = FromHandle(ctx);
    if (!context) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    if (!attributes && num != 0) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    // Context attributes have no modelled effect yet; they are logged when
    // tracing and otherwise ignored, never silently changing the mix.
    for (uint32_t index = 0; index < num; index++) {
        AUDIOOUT2_TRACE("t=%.3f ContextSetAttributes ctx %p: id=0x%x size=%zu value=%p (ignored)\n", AudioOut2TraceSeconds(), static_cast<void*>(context), attributes[index].attribute_id, attributes[index].value_size, attributes[index].value);
    }
    return 0;
}

}
