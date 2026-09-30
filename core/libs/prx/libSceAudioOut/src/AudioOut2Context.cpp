// AudioOut2Context.cpp
// PortPS5 - AudioOut2 Context Implementation (Audio Subsystem M2)
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libSceAudioOut. Manages guest AudioOut2 context
//   queues, port mixing, soft limiting, and pacing onto the single host AudioMixer.
//
// Threading & Invariants:
//   - Context state protected by per-context std::mutex.
//   - Context handles tracked safely via g_contextsLock and AcquireContext.
//   - Mix buffers feed into AudioMixer source ring using SPSC lock-free ring.
//   - Soft limiter replaces fixed gain to preserve dynamic range without clipping.

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

using Clock = std::chrono::steady_clock;

static constexpr std::size_t CONTEXT_MEMORY = 0x10000;
static constexpr std::uint32_t DEFAULT_MAX_PORTS = 16;
static constexpr std::uint64_t CALL_TRACE_FULL = 16;
static constexpr std::chrono::milliseconds FULL_WAIT_TIMEOUT{200};
static constexpr std::chrono::milliseconds FULL_WAIT_STEP{1};

bool AudioOut2TraceEnabled() {
    // Read on every call, never cached: startup initializes once before guest threads.
    // Why the verbatim wrappers: Loader:: methods hash under nid_patcher (libc has no
    // --preserve-exports), so cross-prx callers use the _nid_no_patch free functions.
    if (!PortPS5_Config_Loader_IsInitialized_nid_no_patch()) return false;
    const auto& trace = PortPS5_Config_Loader_Get_nid_no_patch().debug.trace;
    return trace.count(PortPS5::Config::TraceCategory::Audio) != 0;
}

double AudioOut2TraceSeconds() {
    static const auto start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

AudioOut2Telemetry AudioOut2TelemetrySnapshot() {
    AudioOut2Telemetry snapshot;
    snapshot.underruns = AudioMixer::Get().GetUnderruns();
    snapshot.overrunDrops = AudioMixer::Get().GetOverrunDrops();
    return snapshot;
}

void AudioOut2ResetTelemetryForTesting() {
    AudioMixer::Get().ResetTelemetryForTesting();
}

static std::mutex g_contextsLock;
static std::set<AudioOut2Context*> g_liveContexts;

struct ContextRef {
    AudioOut2Context* ctx = nullptr;
    ~ContextRef() {
        if (ctx) {
            ctx->inFlight.fetch_sub(1, std::memory_order_release);
        }
    }
    ContextRef() = default;
    explicit ContextRef(AudioOut2Context* c) : ctx(c) {}
    ContextRef(const ContextRef&) = delete;
    ContextRef& operator=(const ContextRef&) = delete;
    ContextRef(ContextRef&& other) noexcept : ctx(other.ctx) { other.ctx = nullptr; }
    ContextRef& operator=(ContextRef&& other) noexcept {
        if (this != &other) {
            if (ctx) ctx->inFlight.fetch_sub(1, std::memory_order_release);
            ctx = other.ctx;
            other.ctx = nullptr;
        }
        return *this;
    }
    AudioOut2Context* get() const { return ctx; }
    AudioOut2Context* operator->() const { return ctx; }
    explicit operator bool() const { return ctx != nullptr; }
};

static ContextRef AcquireContext(AudioOut2ContextHandle handle) {
    if (!handle) return {};
    std::lock_guard lock(g_contextsLock);
    auto* context = reinterpret_cast<AudioOut2Context*>(handle);
    if (g_liveContexts.count(context) == 0) return {};
    context->inFlight.fetch_add(1, std::memory_order_acquire);
    return ContextRef{context};
}

bool AudioOut2IsValidContext(AudioOut2ContextHandle ctx) {
    if (!ctx) return false;
    std::lock_guard lock(g_contextsLock);
    return g_liveContexts.count(reinterpret_cast<AudioOut2Context*>(ctx)) != 0;
}

static Clock::duration GrainDuration(const AudioOut2Context& context) {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(static_cast<double>(context.grain) / AUDIO_OUT2_SAMPLE_RATE));
}

static std::uint32_t GrainBytes(const AudioOut2Context& context) {
    return static_cast<std::uint32_t>(context.grain * AUDIO_OUT2_OUTPUT_FRAME_BYTES);
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
    if (!AudioMixer::Get().HasDevice() || !context.source) {
        return context.queued;
    }
    const auto queuedFrames = context.source ? context.source->GetQueuedFrames() : 0;
    // Report the queue full unless another whole grain fits under the 100 ms
    // ring ceiling, so a free slot is never reported when the push would drop.
    const auto headroom = AUDIO_MIXER_CEILING_FRAMES > queuedFrames
        ? AUDIO_MIXER_CEILING_FRAMES - queuedFrames : 0u;
    if (headroom < context.grain) return context.queueDepth;
    const auto cushionFrames = AUDIO_MIXER_TARGET_CUSHION_FRAMES;
    const auto pending = queuedFrames > cushionFrames ? queuedFrames - cushionFrames : 0;
    return AudioOut2QueueLevelForPending(pending * AUDIO_OUT2_OUTPUT_FRAME_BYTES,
                                         GrainBytes(context), context.queueDepth);
}

void AudioOut2ForceWallClockForTesting(AudioOut2ContextHandle ctx) {
    auto ref = AcquireContext(ctx);
    if (!ref) return;
    auto* context = ref.get();
    std::lock_guard lock(context->lock);
    AudioMixer::Get().ForceWallClockForTesting();
    context->device = 0;
}

double AudioOut2LatencyMs(AudioOut2ContextHandle ctx) {
    auto ref = AcquireContext(ctx);
    if (!ref) return 0.0;
    auto* context = ref.get();
    std::lock_guard lock(context->lock);
    if (context->device != 0 || AudioMixer::Get().HasDevice()) {
        return AudioMixer::Get().GetLatencyMs();
    }
    Drain(*context, Clock::now());
    return 1000.0 * static_cast<double>(context->queued) *
           static_cast<double>(context->grain) / AUDIO_OUT2_SAMPLE_RATE;
}

// Mixes the ports' current grain and queues it on the host mixer. The caller holds the lock.
static std::uint32_t Render(AudioOut2Context& context) {
    std::fill(context.mix.begin(), context.mix.end(), 0.0f);
    const auto mixed = AudioOut2MixPorts(context, context.mix.data(), context.grain);
    for (float& sample : context.mix) {
        // No SoftLimit here: AudioMixer::ProcessCallback limits the summed output
        // exactly once; limiting per source as well would compress peaks twice.
        if (AudioOut2TraceEnabled()) context.summaryPeak = std::max(context.summaryPeak, std::abs(sample));
    }
    if (!context.source) return mixed;
    const bool pushed = context.source->PushStereo48k(
        reinterpret_cast<const AudioFrame*>(context.mix.data()), context.grain);
    if (!pushed) {
        context.overrunDrops++;
        return mixed;
    }
    return mixed ? mixed : 1;
}

static void TraceSummary(AudioOut2Context& context, Clock::time_point now) {
    if (!AudioOut2TraceEnabled()) return;
    if (context.summaryStart == Clock::time_point{}) {
        context.summaryStart = now;
        return;
    }
    const auto elapsed = std::chrono::duration<double>(now - context.summaryStart).count();
    if (elapsed < 1.0) return;
    std::fprintf(stderr, "[audioout2] t=%.3f ctx %p: %.1f pushes/s, %.1f advances/s, %.1f queue polls/s, hw queue %u/%u, mix peak %.3f; totals: pushes %llu (%llu blocking, %llu queue-full rejects), underruns %llu, overrun drops %llu\n",
        AudioOut2TraceSeconds(), static_cast<void*>(&context), static_cast<double>(context.summaryPushes) / elapsed, static_cast<double>(context.summaryAdvances) / elapsed,
        static_cast<double>(context.summaryPolls) / elapsed, QueueLevel(context, now), context.queueDepth, static_cast<double>(context.summaryPeak),
        static_cast<unsigned long long>(context.pushes), static_cast<unsigned long long>(context.blockingPushes), static_cast<unsigned long long>(context.fullRejects),
        static_cast<unsigned long long>(context.underruns), static_cast<unsigned long long>(context.overrunDrops));
    context.summaryStart = now;
    context.summaryPushes = 0;
    context.summaryAdvances = 0;
    context.summaryPolls = 0;
    context.summaryPeak = 0.0f;
}

extern "C" {

/**
 * @brief Advances the context timeline for one grain tick.
 * @param ctx Valid handle to an initialized AudioOut2 context.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE.
 */
int APS5_VABI sceAudioOut2ContextAdvance(AudioOut2ContextHandle ctx) noexcept {
    auto ref = AcquireContext(ctx);
    if (!ref) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    auto* context = ref.get();
    std::lock_guard lock(context->lock);
    context->advances++;
    context->summaryAdvances++;
    if (context->advances <= CALL_TRACE_FULL) AUDIOOUT2_TRACE("t=%.3f Advance ctx %p (pushes so far %llu)\n", AudioOut2TraceSeconds(), static_cast<void*>(context), static_cast<unsigned long long>(context->pushes));
    return 0;
}

/**
 * @brief Creates and initializes an AudioOut2 output context registered with the host mixer.
 * @param params Context configuration parameters (grain size, queue depth).
 * @param buffer User memory buffer (unused, reserved by SDK).
 * @param buffer_size Size of user buffer in bytes.
 * @param ctx Pointer receiving the created context handle.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT / OUT_OF_MEMORY.
 */
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
    context->source = AudioMixer::Get().RegisterSource(AUDIO_OUT2_SAMPLE_RATE, AUDIO_OUT2_OUTPUT_CHANNELS);
    try {
        std::lock_guard lock(g_contextsLock);
        g_liveContexts.insert(context);
    } catch (const std::bad_alloc&) {
        AudioMixer::Get().UnregisterSource(context->source);
        delete context;
        return SCE_AUDIO_OUT2_ERROR_OUT_OF_MEMORY;
    }
    *ctx = reinterpret_cast<AudioOut2ContextHandle>(context);
    return 0;
}

/**
 * @brief Destroys an AudioOut2 context and releases attached sources from the host mixer.
 * @param ctx Context handle to destroy.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE.
 */
int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle ctx) noexcept {
    AudioOut2Context* context = nullptr;
    {
        std::lock_guard lock(g_contextsLock);
        context = reinterpret_cast<AudioOut2Context*>(ctx);
        if (g_liveContexts.erase(context) == 0) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    }
    while (context->inFlight.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
    AUDIOOUT2_TRACE("t=%.3f ContextDestroy ctx %p after %llu pushes\n", AudioOut2TraceSeconds(), static_cast<void*>(context), static_cast<unsigned long long>(context->pushes));
    {
        std::lock_guard lock(context->lock);
        AudioMixer::Get().UnregisterSource(context->source);
        context->source = nullptr;
    }
    AudioOut2ReleasePorts(*context);
    delete context;
    return 0;
}

/**
 * @brief Queries the current queue fill level and remaining free grain slots.
 * @param ctx Valid handle to an initialized AudioOut2 context.
 * @param queue_level Pointer receiving current occupied grain count.
 * @param available_queue Pointer receiving remaining free grain slots.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE.
 */
int APS5_VABI sceAudioOut2ContextGetQueueLevel(AudioOut2ContextHandle ctx, uint32_t* queue_level, uint32_t* available_queue) noexcept {
    auto ref = AcquireContext(ctx);
    if (!ref) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    auto* context = ref.get();
    std::lock_guard lock(context->lock);
    const auto level = QueueLevel(*context, Clock::now());
    if (queue_level) *queue_level = level;
    if (available_queue) *available_queue = context->queueDepth - level;
    context->queueLevelPolls++;
    context->summaryPolls++;
    if (context->queueLevelPolls <= CALL_TRACE_FULL) AUDIOOUT2_TRACE("t=%.3f GetQueueLevel ctx %p -> level %u, available %u\n", AudioOut2TraceSeconds(), static_cast<void*>(context), level, context->queueDepth - level);
    return 0;
}

/**
 * @brief Pushes and renders one mixed audio grain onto the host mixer queue.
 * @param ctx Valid handle to an initialized AudioOut2 context.
 * @param blocking 1 to block until a slot frees up, 0 for non-blocking push.
 * @return 0 on success, SCE_AUDIO_OUT2_ERROR_QUEUE_FULL if full, or error code.
 */
int APS5_VABI sceAudioOut2ContextPush(AudioOut2ContextHandle ctx, uint32_t blocking) noexcept {
    auto ref = AcquireContext(ctx);
    if (!ref) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    auto* context = ref.get();
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
    const auto mixed = Render(*context);
    if (context->source != nullptr && mixed == 0) {
        return static_cast<int>(0x80260501);
    }
    context->queued++;
    context->playHead += GrainDuration(*context);
    context->pushes++;
    context->summaryPushes++;
    if (blocking) context->blockingPushes++;
    if (context->pushes <= CALL_TRACE_FULL) {
        AUDIOOUT2_TRACE("t=%.3f Push ctx %p blocking=%u: grain from %u ports, hw queue %u/%u\n", AudioOut2TraceSeconds(), static_cast<void*>(context), blocking,
            mixed, QueueLevel(*context, now), context->queueDepth);
    }
    TraceSummary(*context, now);
    return 0;
}

/**
 * @brief Queries the required memory size for context creation.
 * @param params Context configuration parameters.
 * @param memory_size Pointer receiving required size in bytes.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT.
 */
int APS5_VABI sceAudioOut2ContextQueryMemory(const AudioOut2ContextParam* params, size_t* memory_size) noexcept {
    if (!params || !memory_size) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    *memory_size = CONTEXT_MEMORY;
    return 0;
}

/**
 * @brief Resets context parameters to default baseline configuration.
 * @param params Pointer to parameter struct to reset.
 * @return 0 on success, or SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT.
 */
int APS5_VABI sceAudioOut2ContextResetParam(AudioOut2ContextParam* params) noexcept {
    if (!params) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    std::memset(params, 0, sizeof(*params));
    params->max_ports = DEFAULT_MAX_PORTS;
    params->queue_depth = 1;
    params->num_grains = AUDIO_OUT2_DEFAULT_GRAIN;
    return 0;
}

/**
 * @brief Sets hardware and rendering attributes for an AudioOut2 context.
 * @param ctx Context handle to configure.
 * @param attributes Array of attribute descriptors.
 * @param num Number of attributes in array.
 * @return 0 on success, or error code.
 */
int APS5_VABI sceAudioOut2ContextSetAttributes(AudioOut2ContextHandle ctx, const AudioOut2Attribute* attributes, uint32_t num) noexcept {
    auto ref = AcquireContext(ctx);
    if (!ref) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    auto* context = ref.get();
    if (!attributes && num != 0) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0; index < num; index++) {
        AUDIOOUT2_TRACE("t=%.3f ContextSetAttributes ctx %p: id=0x%x size=%zu value=%p (ignored)\n", AudioOut2TraceSeconds(), static_cast<void*>(context), attributes[index].attribute_id, attributes[index].value_size, attributes[index].value);
    }
    return 0;
}

}
