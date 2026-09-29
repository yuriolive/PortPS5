// AudioOut.cpp
// PortPS5 - AudioOut v1 Subsystem Implementation (Audio Subsystem M2)
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libSceAudioOut. Implements the PS5 AudioOut v1
//   legacy PCM port interfaces (Init, Open, Close, Output, Outputs, SetVolume,
//   GetPortState) routed onto the unified process-wide host AudioMixer.
//
// Threading & Invariants:
//   - Port table access is serialized with g_mutex.
//   - Each active port registers an AudioSource on AudioMixer with an SPSC ring.
//   - All multi-channel streams downmix to stereo F32 with LFE fold at -10 dB.
//   - Resampling for non-48 kHz sample rates is performed via SDL_AudioStream.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "prx/libc/include/General.hpp"
#include "SDL.h"
#include "SceTypes.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include "AudioMixer.hpp"

static constexpr int PORT_TYPE_MAIN = 0;
static constexpr int PORT_TYPE_BGM = 1;
static constexpr int PORT_TYPE_VOICE = 2;
static constexpr int PORT_TYPE_PERSONAL = 3;
static constexpr int PORT_TYPE_PADSPK = 4;
static constexpr int PORT_TYPE_VIBRATION = 10;
static constexpr int PORT_TYPE_AUDIO3D = 126;
static constexpr int PORT_TYPE_AUX = 127;

static constexpr int PORTS_MAX = 32;
static constexpr int DEFAULT_VOLUME = 32768;
static constexpr std::uint32_t FORMAT_MASK = 0xFFu;

// Guest error codes observed on this path: -2144993276 is the invalid-argument
// code (returned for null pointers), -2144993277 the invalid-handle code.
static constexpr int SCE_AUDIO_OUT_ERROR_INVALID_ARGUMENT = -2144993276;
static constexpr int SCE_AUDIO_OUT_ERROR_INVALID_HANDLE = -2144993277;
static constexpr int SCE_AUDIO_OUT_ERROR_OUT_OF_MEMORY = -2144993278;

enum class Format {
    Unknown,
    S16Mono,
    S16Stereo,
    S16_8Ch,
    F32Mono,
    F32Stereo,
    F32_8Ch,
    S16_8ChStd,
    F32_8ChStd,
};

static bool formatIsFloat(Format f) {
    return f == Format::F32Mono || f == Format::F32Stereo ||
           f == Format::F32_8Ch || f == Format::F32_8ChStd;
}

static bool formatIsStd(Format f) {
    return f == Format::S16_8ChStd || f == Format::F32_8ChStd;
}

static int channelsForFormat(Format f) {
    switch (f) {
        case Format::S16Mono:
        case Format::F32Mono:
            return 1;
        case Format::S16Stereo:
        case Format::F32Stereo:
            return 2;
        case Format::S16_8Ch:
        case Format::F32_8Ch:
        case Format::S16_8ChStd:
        case Format::F32_8ChStd:
            return 8;
        default:
            Unsupported("channelsForFormat: unknown format");
    }
}

static constexpr std::uint32_t STD_8CH_MAP[8] = {0, 1, 2, 3, 6, 7, 4, 5};

/**
 * Retains one mixer registration until every output snapshot releases it. The
 * producer mutex spans both pacing and push, keeping the ring single-producer.
 */
struct PortSource {
    AudioSource* source;
    std::mutex producerMutex;

    /** Registers the source only after shared ownership storage is allocated. */
    PortSource(std::uint32_t rate, std::uint32_t channels)
        : source(AudioMixer::Get().RegisterSource(rate, channels)) {}

    /** Releases the mixer slot after the last in-flight output completes. */
    ~PortSource() { AudioMixer::Get().UnregisterSource(source); }
};

struct Port {
    bool used = false;
    int type = 0;
    std::uint32_t samplesNum = 0;
    std::uint32_t freq = 0;
    Format format = Format::Unknown;
    int channels = 0;
    int volume[8] = {};
    std::uint64_t lastOutputTime = 0;
    std::shared_ptr<PortSource> source;
};

static std::mutex g_mutex;
/** Constructs the owning port table after the mixer so it is destroyed first. */
static std::array<Port, PORTS_MAX>& ports() {
    AudioMixer::Get();
    static std::array<Port, PORTS_MAX> table;
    return table;
}

static void convertAndDownmix(const Port& port, const void* data, std::vector<AudioFrame>& out) {
    const auto frames = port.samplesNum;
    const auto ch = static_cast<std::uint32_t>(port.channels);
    out.resize(frames);

    float vol[8] = {};
    for (std::uint32_t i = 0; i < ch; i++) {
        vol[i] = static_cast<float>(port.volume[i]) / static_cast<float>(DEFAULT_VOLUME);
    }

    const bool isStd = formatIsStd(port.format) && ch == 8;

    if (ch == 1) {
        if (formatIsFloat(port.format)) {
            const auto* src = static_cast<const float*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                const float s = src[i] * vol[0];
                out[i] = AudioFrame{s, s};
            }
        } else {
            const auto* src = static_cast<const std::int16_t*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                const float s = (static_cast<float>(src[i]) / 32768.0f) * vol[0];
                out[i] = AudioFrame{s, s};
            }
        }
    } else if (ch == 2) {
        if (formatIsFloat(port.format)) {
            const auto* src = static_cast<const float*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                out[i] = AudioFrame{src[i * 2 + 0] * vol[0], src[i * 2 + 1] * vol[1]};
            }
        } else {
            const auto* src = static_cast<const std::int16_t*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                out[i] = AudioFrame{
                    (static_cast<float>(src[i * 2 + 0]) / 32768.0f) * vol[0],
                    (static_cast<float>(src[i * 2 + 1]) / 32768.0f) * vol[1]
                };
            }
        }
    } else if (ch == 8) {
        float frameIn[8] = {};
        if (formatIsFloat(port.format)) {
            const auto* src = static_cast<const float*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                for (std::uint32_t c = 0; c < 8; c++) {
                    const auto srcCh = isStd ? STD_8CH_MAP[c] : c;
                    frameIn[c] = src[i * 8 + srcCh];
                }
                AudioMixerDownmix8Ch(frameIn, vol, out[i].left, out[i].right);
            }
        } else {
            const auto* src = static_cast<const std::int16_t*>(data);
            for (std::uint32_t i = 0; i < frames; i++) {
                for (std::uint32_t c = 0; c < 8; c++) {
                    const auto srcCh = isStd ? STD_8CH_MAP[c] : c;
                    frameIn[c] = static_cast<float>(src[i * 8 + srcCh]) / 32768.0f;
                }
                AudioMixerDownmix8Ch(frameIn, vol, out[i].left, out[i].right);
            }
        }
    }
}

static void queueAudio(const Port& port, const void* data) {
    if (!port.source || !port.source->source) return;
    std::lock_guard producerLock(port.source->producerMutex);
    auto* source = port.source->source;
    if (data == nullptr) {
        // Documented PS5 behavior: null buffer waits for queued audio to drain
        source->Drain(200);
        return;
    }

    std::vector<AudioFrame> stereoFrames;
    convertAndDownmix(port, data, stereoFrames);

    // Pace against the 40 ms target cushion to prevent runaway queuing
    source->WaitUntilQueuedAtMost(AUDIO_MIXER_TARGET_CUSHION_FRAMES, 200);

    // Push into the source ring (automatically resamples if freq != 48000)
    source->PushAndResample(stereoFrames.data(), port.samplesNum);
}

static bool portTypeValid(int type) {
    return (type >= PORT_TYPE_MAIN && type <= PORT_TYPE_PADSPK) ||
           type == PORT_TYPE_VIBRATION ||
           type == PORT_TYPE_AUDIO3D ||
           type == PORT_TYPE_AUX;
}

static Port* getPort(int handle) {
    const int idx = handle - 1;
    if (idx < 0 || idx >= PORTS_MAX || !ports()[idx].used) {
        return nullptr;
    }
    return &ports()[idx];
}

extern "C" {

/**
 * @brief Initializes the AudioOut subsystem and host mixer.
 * @return 0 on success.
 */
int APS5_VABI sceAudioOutInit() noexcept {
    AudioMixer::Get().Initialize();
    return 0;
}

/**
 * @brief Opens an AudioOut v1 output port and binds it to the host mixer.
 * @param userId User identifier (unused).
 * @param type Port type (Main, BGM, Voice, etc.).
 * @param index Port index (must be 0).
 * @param len Grain size in samples per channel.
 * @param freq Sampling frequency in Hz.
 * @param param Format bitfield (Format, Channels).
 * @return Positive port handle on success, or negative SCE error code.
 */
int APS5_VABI sceAudioOutOpen(int userId, int type, int index, std::uint32_t len,
    std::uint32_t freq, std::uint32_t param) noexcept {
    (void)userId;
    if (!portTypeValid(type)) {
        return -2144993270;
    }
    if (index != 0) {
        Unsupported("sceAudioOutOpen: index != 0 not supported");
    }

    Format format = Format::Unknown;
    switch (param & FORMAT_MASK) {
        case 0: format = Format::S16Mono; break;
        case 1: format = Format::S16Stereo; break;
        case 2: format = Format::S16_8Ch; break;
        case 3: format = Format::F32Mono; break;
        case 4: format = Format::F32Stereo; break;
        case 5: format = Format::F32_8Ch; break;
        case 6: format = Format::S16_8ChStd; break;
        case 7: format = Format::F32_8ChStd; break;
        default:
            return SCE_AUDIO_OUT_ERROR_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < PORTS_MAX; i++) {
        if (!ports()[i].used) {
            Port& port = ports()[i];
            port.used = true;
            port.type = type;
            port.samplesNum = len;
            port.freq = freq;
            port.format = format;
            port.channels = channelsForFormat(format);
            port.lastOutputTime = 0;
            for (int c = 0; c < port.channels; c++) {
                port.volume[c] = DEFAULT_VOLUME;
            }
            if (type != PORT_TYPE_VIBRATION) {
                try {
                    port.source = std::make_shared<PortSource>(freq, port.channels);
                } catch (const std::bad_alloc&) {
                    port = Port{};
                    return SCE_AUDIO_OUT_ERROR_OUT_OF_MEMORY;
                }
            }
            return i + 1;
        }
    }
    return -2144993275;
}

/**
 * @brief Closes an active AudioOut v1 port and detaches it from the host mixer.
 * @param handle Valid handle returned from sceAudioOutOpen.
 * @return 0 on success, or SCE_AUDIO_OUT_ERROR_INVALID_HANDLE.
 */
int APS5_VABI sceAudioOutClose(int handle) noexcept {
    std::shared_ptr<PortSource> retired;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Port* port = getPort(handle);
        if (port == nullptr) {
            return SCE_AUDIO_OUT_ERROR_INVALID_HANDLE;
        }
        retired = std::move(port->source);
        *port = Port{};
    }
    // Destruction may wait for a callback, so release outside the port table lock.
    return 0;
}

/**
 * @brief Outputs one block of audio samples on an AudioOut v1 port.
 * @param handle Port handle.
 * @param ptr Pointer to audio PCM buffer, or nullptr to drain queued audio.
 * @return Number of samples processed on success, or negative error code.
 */
int APS5_VABI sceAudioOutOutput(int handle, const void* ptr) noexcept {
    Port snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const Port* port = getPort(handle);
        if (port == nullptr) {
            return SCE_AUDIO_OUT_ERROR_INVALID_HANDLE;
        }
        snapshot = *port;
    }
    try {
        queueAudio(snapshot, ptr);
    } catch (const std::bad_alloc&) {
        return SCE_AUDIO_OUT_ERROR_OUT_OF_MEMORY;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto* port = getPort(handle); port && port->source == snapshot.source) {
            port->lastOutputTime = sceKernelGetProcessTime();
        }
    }
    return static_cast<int>(snapshot.samplesNum);
}

/**
 * @brief Outputs audio samples simultaneously across multiple AudioOut v1 ports.
 * @param param Array of output parameter descriptors containing handles and buffer pointers.
 * @param num Number of descriptors in array.
 * @return Number of samples output on primary port, or negative error code.
 */
int APS5_VABI sceAudioOutOutputs(AudioOutOutputParam* param, std::uint32_t num) noexcept {
    if (param == nullptr || num == 0) {
        return SCE_AUDIO_OUT_ERROR_INVALID_ARGUMENT;
    }

    // Capture all registrations before dropping the table lock, including ports
    // later in the batch that may close while an earlier entry is pacing.
    std::vector<Port> snapshots;
    try {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (std::uint32_t i = 0; i < num; i++) {
                if (getPort(param[i].handle) == nullptr) {
                    return SCE_AUDIO_OUT_ERROR_INVALID_HANDLE;
                }
            }
            snapshots.reserve(num);
            for (std::uint32_t i = 0; i < num; i++) {
                snapshots.push_back(*getPort(param[i].handle));
            }
        }
        for (std::uint32_t i = 0; i < num; i++) {
            queueAudio(snapshots[i], param[i].ptr);
        }
    } catch (const std::bad_alloc&) {
        return SCE_AUDIO_OUT_ERROR_OUT_OF_MEMORY;
    }

    const std::uint64_t done = sceKernelGetProcessTime();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (std::uint32_t i = 0; i < num; i++) {
            if (auto* port = getPort(param[i].handle); port && port->source == snapshots[i].source) {
                port->lastOutputTime = done;
            }
        }
    }
    return static_cast<int>(snapshots.front().samplesNum);
}

/**
 * @brief Sets per-channel volume multipliers for an AudioOut v1 port.
 * @param handle Port handle.
 * @param flag Bitmask selecting which channel volume values to apply.
 * @param vol Array of volume integers (32768 = 1.0f unity gain).
 * @return 0 on success, or error code.
 */
int APS5_VABI sceAudioOutSetVolume(int handle, std::uint32_t flag, int* vol) noexcept {
    if (vol == nullptr) {
        return SCE_AUDIO_OUT_ERROR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Port* port = getPort(handle);
    if (port == nullptr) {
        return SCE_AUDIO_OUT_ERROR_INVALID_HANDLE;
    }
    const bool isStd = formatIsStd(port->format);
    for (int i = 0; i < port->channels; i++, flag >>= 1u) {
        if ((flag & 1u) == 0) {
            continue;
        }
        int srcIdx = i;
        if (isStd) {
            if (i == 4) srcIdx = 6;
            else if (i == 5) srcIdx = 7;
            else if (i == 6) srcIdx = 4;
            else if (i == 7) srcIdx = 5;
        }
        port->volume[i] = vol[srcIdx];
    }
    return 0;
}

/**
 * @brief Queries the runtime output state and configuration of an AudioOut v1 port.
 * @param handle Port handle.
 * @param state Pointer receiving the port state structure.
 * @return 0 on success, or error code.
 */
int APS5_VABI sceAudioOutGetPortState(int handle, AudioOutPortState* state) noexcept {
    if (state == nullptr) {
        return SCE_AUDIO_OUT_ERROR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Port* port = getPort(handle);
    if (port == nullptr) {
        return SCE_AUDIO_OUT_ERROR_INVALID_HANDLE;
    }
    state->rerouteCounter = 0;
    state->volume = 127;
    state->flag = 0;
    state->activeState = 0;
    state->reserved[0] = 0;
    switch (port->type) {
        case PORT_TYPE_MAIN:
        case PORT_TYPE_BGM:
        case PORT_TYPE_AUDIO3D:
            state->output = 1;
            state->channel = static_cast<std::uint8_t>(port->channels > 2 ? 2 : port->channels);
            break;
        case PORT_TYPE_VOICE:
        case PORT_TYPE_PERSONAL:
            state->output = 0x40;
            state->channel = 1;
            break;
        case PORT_TYPE_PADSPK:
        case PORT_TYPE_VIBRATION:
            state->output = 4;
            state->channel = 1;
            break;
        case PORT_TYPE_AUX:
            state->output = 0x80;
            state->channel = 0;
            break;
        default:
            Unsupported("sceAudioOutGetPortState: unknown port type");
    }
    return 0;
}

}
