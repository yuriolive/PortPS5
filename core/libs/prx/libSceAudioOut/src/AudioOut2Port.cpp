// core/libs/prx/libSceAudioOut/src/AudioOut2Port.cpp
// AudioOut2 ports: creation, destruction, attribute decoding (PCM pointer, per-channel gain) and the
// per-context mix of all ports into the stereo grain the mixer source consumes. Ports are guest
// state: the PCM buffer is guest memory read at mix time, never copied at set time. All exports
// are APS5_VABI + noexcept and serialise on one port-table mutex; the mix runs on the pushing thread.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "AudioOut2Internal.hpp"

static constexpr std::uint16_t OUTPUT_MAIN = 1;
static constexpr std::int16_t VOLUME_MAX = 127;
// Attribute sets are traced in full for the first few calls of a port, then once per this many.
static constexpr std::uint64_t ATTRIBUTE_TRACE_FULL = 8;
static constexpr std::uint64_t ATTRIBUTE_TRACE_EVERY = 2000;

// Port attributes as observed on the wire: id 0 carries the grain's PCM as an
// 8-byte value holding the buffer pointer, id 1 one float gain per channel.
// Ids 5, 8 and 9 (a u32, a u32 and a byte on object and voice ports) are not
// understood yet; they are logged once per id when tracing and otherwise
// ignored, which keeps object-port layout changes visible until the M5
// panning work decodes them (docs/spec/audio.md Open questions).
static constexpr std::uint32_t ATTRIBUTE_DATA = 0;
static constexpr std::uint32_t ATTRIBUTE_VOLUME = 1;

static std::mutex g_portsLock;
// Grows on demand: the title opens its bed ports plus max_object_ports object ports at once.
static std::vector<AudioOut2Port> g_ports;

static AudioOut2Port* FromHandle(AudioOut2PortHandle handle) {
    const auto index = handle - 1;
    if (handle == 0 || index >= g_ports.size() || !g_ports[index].used) return nullptr;
    return &g_ports[index];
}

static void AccumulatePort(const AudioOut2Port& port, float* out, std::uint32_t frames) {
    const auto ch = port.channels;
    float in[AUDIO_OUT2_PORT_CHANNELS_MAX];
    for (std::uint32_t frame = 0; frame < frames; frame++) {
        AudioOut2LoadFrame(port.data, port.sampleType, frame, ch, in);
        AudioOut2DownmixFrame(in, ch, port.volume, out + static_cast<std::size_t>(frame) * AUDIO_OUT2_OUTPUT_CHANNELS);
    }
}

std::uint32_t AudioOut2MixPorts(const AudioOut2Context& context, float* out, std::uint32_t frames) {
    std::lock_guard lock(g_portsLock);
    std::uint32_t mixed = 0;
    for (const auto& port : g_ports) {
        if (!port.used || port.context != &context || port.data == nullptr || port.channels == 0) continue;
        AccumulatePort(port, out, frames);
        mixed++;
    }
    return mixed;
}

void AudioOut2ReleasePorts(const AudioOut2Context& context) {
    std::lock_guard lock(g_portsLock);
    for (auto& port : g_ports) {
        if (port.used && port.context == &context) port = AudioOut2Port{};
    }
}

static void TraceAttribute(AudioOut2PortHandle handle, const AudioOut2Port& port, const AudioOut2Attribute& attribute, const char* verdict) {
    if (!AudioOut2TraceEnabled()) return;
    char values[128] = "";
    if (attribute.value != nullptr) {
        std::size_t used = 0;
        const auto words = std::min<std::size_t>(attribute.value_size / sizeof(std::uint32_t), 4);
        for (std::size_t index = 0; index < words && used < sizeof(values); index++) {
            std::uint32_t bits = 0;
            float value = 0.0f;
            std::memcpy(&bits, static_cast<const std::uint8_t*>(attribute.value) + index * sizeof(bits), sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            used += static_cast<std::size_t>(std::snprintf(values + used, sizeof(values) - used, " %08x(%g)", bits, static_cast<double>(value)));
        }
    }
    std::fprintf(stderr, "[audioout2] t=%.3f port %llu set attribute id=0x%x size=%zu value=%p%s: %s (data sets so far %llu)\n",
        AudioOut2TraceSeconds(), static_cast<unsigned long long>(handle), attribute.attribute_id, attribute.value_size, attribute.value, values, verdict,
        static_cast<unsigned long long>(port.dataSets));
}

extern "C" {

/**
 * Creates a port on `ctx` from a guest SceAudioOut2PortParam.
 * Returns 0 and the 1-based handle; INVALID_HANDLE for an unknown context; INVALID_ARGUMENT for null
 * params or handle. A data_format whose channel count or sample type is not understood still
 * succeeds, but the port stays unrendered (channels == 0) rather than decoding guessed PCM.
 */
int APS5_VABI sceAudioOut2PortCreate(AudioOut2ContextHandle ctx, const AudioOut2PortParam* params, AudioOut2PortHandle* port) noexcept {
    if (!ctx || !AudioOut2IsValidContext(ctx)) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    if (!params || !port) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    std::lock_guard lock(g_portsLock);
    std::size_t index = 0;
    while (index < g_ports.size() && g_ports[index].used) index++;
    if (index == g_ports.size()) {
        try {
            g_ports.emplace_back();
        } catch (const std::bad_alloc&) {
            return SCE_AUDIO_OUT2_ERROR_OUT_OF_MEMORY;
        }
    }
    auto& entry = g_ports[index];
    entry = AudioOut2Port{};
    entry.used = true;
    entry.context = reinterpret_cast<AudioOut2Context*>(ctx);
    entry.type = params->port_type;
    entry.dataFormat = params->data_format;
    entry.samplingFreq = params->sampling_freq;
    entry.flags = params->flags;
    // A format whose channel count is not understood leaves channels at 0, so
    // the port is skipped by the mix instead of fabricating PCM. sampling_freq
    // is stored for the M2 resampler; the M1 mix runs at 48 kHz throughout.
    entry.sampleType = AudioOut2DecodeSampleType(params->data_format);
    entry.channels = entry.sampleType == AudioOut2SampleType::Unknown ? 0 : AudioOut2DecodeChannels(params->data_format);
    if (entry.channels == 0) {
        // Not an error code: a port with a format this mixer does not render is accepted and skipped
        // (docs/spec/audio.md), but never silently.
        APS5_LOG_OUT("sceAudioOut2PortCreate: data_format 0x%x is not rendered (channels or sample type unsupported)",
                     params->data_format);
    }
    *port = static_cast<AudioOut2PortHandle>(index) + 1;
    AUDIOOUT2_TRACE("t=%.3f PortCreate ctx=%llx -> port %llu: type=0x%x data_format=0x%x (%u ch, sample type %d) sampling_freq=%u flags=0x%x user=%llx\n",
        AudioOut2TraceSeconds(), static_cast<unsigned long long>(ctx), static_cast<unsigned long long>(*port), params->port_type, params->data_format,
        entry.channels, static_cast<int>(entry.sampleType), params->sampling_freq, params->flags, static_cast<unsigned long long>(params->user_handle));
    return 0;
}

/** Destroys a port. Returns 0, or INVALID_HANDLE for an unknown or already destroyed handle. */
int APS5_VABI sceAudioOut2PortDestroy(AudioOut2PortHandle port) noexcept {
    std::lock_guard lock(g_portsLock);
    auto* entry = FromHandle(port);
    if (!entry) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    AUDIOOUT2_TRACE("t=%.3f PortDestroy port %llu (data sets %llu)\n", AudioOut2TraceSeconds(), static_cast<unsigned long long>(port), static_cast<unsigned long long>(entry->dataSets));
    *entry = AudioOut2Port{};
    return 0;
}

/**
 * Reports the fixed main-output state of a port.
 * Returns 0, INVALID_HANDLE for an unknown handle, or INVALID_ARGUMENT for a null state pointer.
 */
int APS5_VABI sceAudioOut2PortGetState(AudioOut2PortHandle port, AudioOut2PortState* state) noexcept {
    std::lock_guard lock(g_portsLock);
    if (!FromHandle(port)) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    if (!state) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    state->output = OUTPUT_MAIN;
    state->num_channels = AUDIO_OUT2_OUTPUT_CHANNELS;
    state->volume = VOLUME_MAX;
    state->reroute_counter = 0;
    state->flags = 0;
    return 0;
}

/**
 * Applies guest attributes to a port: id 0 stores the PCM buffer pointer (8-byte value, read at mix
 * time), id 1 stores one float gain per channel. Other ids are traced and ignored.
 * Returns 0, INVALID_HANDLE for an unknown handle, or INVALID_ARGUMENT for a null array with num > 0.
 */
int APS5_VABI sceAudioOut2PortSetAttributes(AudioOut2PortHandle port, const AudioOut2Attribute* attributes, uint32_t num) noexcept {
    std::lock_guard lock(g_portsLock);
    auto* entry = FromHandle(port);
    if (!entry) return SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE;
    if (!attributes && num != 0) return SCE_AUDIO_OUT2_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0; index < num; index++) {
        const auto& attribute = attributes[index];
        const char* verdict = "ignored";
        if (attribute.value == nullptr) {
            verdict = "null value";
        } else if (attribute.attribute_id == ATTRIBUTE_DATA && attribute.value_size == sizeof(entry->data)) {
            std::memcpy(&entry->data, attribute.value, sizeof(entry->data));
            entry->dataSets++;
            verdict = "pcm data pointer";
        } else if (attribute.attribute_id == ATTRIBUTE_VOLUME && entry->channels != 0 && attribute.value_size == entry->channels * sizeof(float)) {
            std::memcpy(entry->volume, attribute.value, attribute.value_size);
            verdict = "volume";
        }
        const auto traces = entry->attributeTraces++;
        if (traces < ATTRIBUTE_TRACE_FULL || traces % ATTRIBUTE_TRACE_EVERY == 0) TraceAttribute(port, *entry, attribute, verdict);
    }
    return 0;
}

}
