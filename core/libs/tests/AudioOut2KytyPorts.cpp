// AudioOut2KytyPorts.cpp
// PortPS5 - AudioOut2 Port Lifecycle GoogleTest Suite (Audio Subsystem M2)
//
// Purpose:
//   Ports the port-lifecycle scenarios from KytyPS5 tests/AudioOut2PortTests.cpp
//   (GPL-2.0-or-later, used here under GPL-2.0 terms; scenarios adapted, no code
//   copied verbatim) onto PortPS5's single-mixer AudioOut2 API as GoogleTest cases
//   running on synthetic buffers without audio hardware.
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libSceAudioOut. Exercises context/port lifecycle,
//   concurrent creation, destroy-cleans-ports, unknown-format skip, late PCM
//   reads, and depth-1 queue backpressure.
//
// Threading & Invariants:
//   - Port table access is serialized inside the implementation (g_portsLock);
//     concurrent creates from guest threads are safe.
//   - Contexts run on the wall-clock model through the test hook so assertions
//     hold with or without audio hardware.
//
// Intentional divergences from KytyPS5 (verified against docs/spec/audio.md):
//   - One host mixer: contexts own mixer sources, ports never open their own OS
//     device, so device-count and backend-capture assertions have no equivalent.
//   - Channel counts 1/2/8 only: other data_format values decode to 0 channels
//     and the port is created but left unrendered instead of opening a device.
//   - Late PCM read: the mix reads the guest buffer at push time, not when the
//     data attribute is set, so rewriting the buffer changes the next mix.
//   - Unbounded port table: the table grows on demand, so over-full creation
//     succeeds where KytyPS5's fixed table rejects.
//   - Opens never block: there is no device-open gate, so destroy-during-create
//     cancellation is vacuous; destroy-releases-ports is tested instead.

#define SDL_MAIN_HANDLED
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAudioOut/src/AudioOut2Internal.hpp"

extern "C" {
// Resets a context parameter struct to defaults. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextResetParam(AudioOut2ContextParam* params) noexcept;
// Creates an AudioOut2 context. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextCreate(const AudioOut2ContextParam* params, void* buffer, size_t buffer_size, AudioOut2ContextHandle* ctx) noexcept;
// Destroys an AudioOut2 context. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle ctx) noexcept;
// Queries queue fill level and free slots. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextGetQueueLevel(AudioOut2ContextHandle ctx, uint32_t* queue_level, uint32_t* available_queue) noexcept;
// Pushes one mixed grain. Returns 0 on success.
int APS5_VABI sceAudioOut2ContextPush(AudioOut2ContextHandle ctx, uint32_t blocking) noexcept;
// Creates an AudioOut2 port. Returns 0 on success.
int APS5_VABI sceAudioOut2PortCreate(AudioOut2ContextHandle ctx, const AudioOut2PortParam* params, AudioOut2PortHandle* port) noexcept;
// Destroys an AudioOut2 port. Returns 0 on success.
int APS5_VABI sceAudioOut2PortDestroy(AudioOut2PortHandle port) noexcept;
// Queries port runtime state. Returns 0 on success.
int APS5_VABI sceAudioOut2PortGetState(AudioOut2PortHandle port, AudioOut2PortState* state) noexcept;
// Sets port PCM buffer and volume attributes. Returns 0 on success.
int APS5_VABI sceAudioOut2PortSetAttributes(AudioOut2PortHandle port, const AudioOut2Attribute* attributes, uint32_t num) noexcept;
}

namespace {

AudioOut2ContextHandle MakeWallClockContext(std::uint32_t depth, std::uint32_t grain) {
    AudioOut2ContextParam params{};
    EXPECT_EQ(sceAudioOut2ContextResetParam(&params), 0);
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

}  // namespace

// Verifies port handles are reusable across repeated create/destroy cycles.
// Ported from KytyPS5 TestSlotReuse.
TEST(AudioOut2KytyPortTests, SlotReuse) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    AudioOut2PortParam params{};
    params.data_format = 0x200;
    params.sampling_freq = 48000;
    for (int i = 0; i < 300; i++) {
        AudioOut2PortHandle port = 0;
        EXPECT_EQ(sceAudioOut2PortCreate(ctx, &params, &port), 0);
        EXPECT_NE(port, 0u);
        EXPECT_EQ(sceAudioOut2PortDestroy(port), 0);
    }
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies the port table grows on demand instead of rejecting past a fixed
// cap. Documents the intentional divergence from KytyPS5 TestFullTableRecovers
// (fixed 256-entry table); here all 300 concurrent ports must succeed.
TEST(AudioOut2KytyPortTests, TableGrowth) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    std::vector<AudioOut2PortHandle> ports;
    ports.reserve(300);
    std::set<AudioOut2PortHandle> distinct;
    for (int i = 0; i < 300; i++) {
        AudioOut2PortHandle port = 0;
        AudioOut2PortParam params{};
        params.data_format = 0x200;
        params.sampling_freq = 48000;
        EXPECT_EQ(sceAudioOut2PortCreate(ctx, &params, &port), 0);
        EXPECT_NE(port, 0u);
        ports.push_back(port);
        distinct.insert(port);
    }
    EXPECT_EQ(distinct.size(), 300u);
    for (auto port : ports) {
        EXPECT_EQ(sceAudioOut2PortDestroy(port), 0);
    }
    AudioOut2PortHandle reopened = 0;
    AudioOut2PortParam params{};
    params.data_format = 0x200;
    params.sampling_freq = 48000;
    EXPECT_EQ(sceAudioOut2PortCreate(ctx, &params, &reopened), 0);
    EXPECT_EQ(sceAudioOut2PortDestroy(reopened), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies concurrent port creation from multiple threads keeps each port's
// channel decoding and state intact. Ported from KytyPS5 TestConcurrentCreates
// (device-open gates have no equivalent: opens never block here).
TEST(AudioOut2KytyPortTests, ConcurrentCreate) {
    constexpr int kThreads = 8;
    AudioOut2ContextHandle ctx = MakeWallClockContext(16, 256);
    std::vector<AudioOut2PortHandle> ports(kThreads, 0);
    std::vector<int> results(kThreads, 0);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&, i]() {
            AudioOut2PortParam params{};
            params.data_format = 0x880;
            params.sampling_freq = 48000;
            results[i] = sceAudioOut2PortCreate(ctx, &params, &ports[i]);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (int i = 0; i < kThreads; i++) {
        EXPECT_EQ(results[i], 0);
        EXPECT_NE(ports[i], 0u);
        AudioOut2PortState state{};
        EXPECT_EQ(sceAudioOut2PortGetState(ports[i], &state), 0);
        EXPECT_EQ(state.output, 1u);
        EXPECT_EQ(state.num_channels, 2u);
        EXPECT_EQ(sceAudioOut2PortDestroy(ports[i]), 0);
    }
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies destroying a context releases its ports: their handles go invalid
// and a fresh context is unaffected. Adapted from KytyPS5
// TestContextDestroyCancelsPendingCreate (creates never block here, so the
// pending-create variant is vacuous; the release invariant is tested instead).
TEST(AudioOut2KytyPortTests, DestroyContextReleasesPorts) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    AudioOut2PortHandle port = MakePort(ctx, 0x200);
    std::vector<float> pcm(256 * 2, 0.5f);
    SetPortData(port, pcm.data());
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
    EXPECT_EQ(sceAudioOut2PortDestroy(port), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    EXPECT_EQ(sceAudioOut2PortGetState(port, nullptr), SCE_AUDIO_OUT2_ERROR_INVALID_HANDLE);
    AudioOut2ContextHandle fresh = MakeWallClockContext(4, 256);
    AudioOut2PortHandle freshPort = MakePort(fresh, 0x200);
    EXPECT_EQ(sceAudioOut2PortDestroy(freshPort), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(fresh), 0);
}

// Verifies ports with channel counts outside 1/2/8 are created but left
// unrendered instead of fabricating PCM. Documents the intentional divergence
// from KytyPS5 TestFloat12ChannelPortOutputsPcm (12-channel device beds are
// out of scope: 1.0 assumes stereo downmix input).
TEST(AudioOut2KytyPortTests, UnknownFormatUnrendered) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    EXPECT_EQ(AudioOut2DecodeChannels(0x0c00), 0u);
    EXPECT_EQ(AudioOut2DecodeChannels(0x0400), 0u);
    AudioOut2PortHandle bed12 = MakePort(ctx, 0x0c00);
    AudioOut2PortHandle bed4 = MakePort(ctx, 0x0400);
    std::vector<float> pcm(256 * 12, 0.5f);
    SetPortData(bed12, pcm.data());
    SetPortData(bed4, pcm.data());
    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    std::vector<float> out(256 * 2, 0.0f);
    EXPECT_EQ(AudioOut2MixPorts(*context, out.data(), 256), 0u);
    for (float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.0f);
    }
    EXPECT_EQ(sceAudioOut2PortDestroy(bed12), 0);
    EXPECT_EQ(sceAudioOut2PortDestroy(bed4), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies the mix reads the guest PCM buffer at push time, so rewriting the
// buffer changes the next mix. Documents the intentional divergence from
// KytyPS5 TestPcmCopiedBeforeScratchBufferReuse (copy-at-set); here the
// push-time copy also guarantees a guest rewrite cannot tear a callback.
TEST(AudioOut2KytyPortTests, LateReadSeesLatestGrain) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(4, 256);
    AudioOut2PortHandle port = MakePort(ctx, 0x200);
    std::vector<float> scratch(256 * 2, 0.25f);
    SetPortData(port, scratch.data());
    std::fill(scratch.begin(), scratch.end(), 1.0f);
    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    std::vector<float> out(256 * 2, 0.0f);
    EXPECT_EQ(AudioOut2MixPorts(*context, out.data(), 256), 1u);
    for (float sample : out) {
        EXPECT_FLOAT_EQ(sample, 1.0f);
    }
    EXPECT_EQ(sceAudioOut2PortDestroy(port), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}

// Verifies a depth-1 queue keeps backpressure: the first non-blocking push
// lands, the second is rejected with QUEUE_FULL, and the level reads 1/0. A
// push with no PCM set queues a silence grain and succeeds (matching KytyPS5's
// empty-sync-push semantics), while contributing nothing to the mix. Adapted
// from KytyPS5 TestAsynchronousDevicePushKeepsQueueBounded and
// TestHandleWithoutPcmDoesNotBypassQueue.
TEST(AudioOut2KytyPortTests, DepthOneBackpressure) {
    AudioOut2ContextHandle ctx = MakeWallClockContext(1, 256);
    AudioOut2PortHandle port = MakePort(ctx, 0x200);
    EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), 0);
    EXPECT_EQ(sceAudioOut2ContextPush(ctx, 0), SCE_AUDIO_OUT2_ERROR_QUEUE_FULL);
    std::uint32_t level = 99;
    std::uint32_t available = 99;
    EXPECT_EQ(sceAudioOut2ContextGetQueueLevel(ctx, &level, &available), 0);
    EXPECT_EQ(level, 1u);
    EXPECT_EQ(available, 0u);
    // No PCM was ever set, so the mix must be untouched silence: nothing is
    // fabricated for the empty port.
    auto* context = reinterpret_cast<AudioOut2Context*>(ctx);
    std::vector<float> out(256 * 2, 0.0f);
    EXPECT_EQ(AudioOut2MixPorts(*context, out.data(), 256), 0u);
    for (float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.0f);
    }
    EXPECT_EQ(sceAudioOut2PortDestroy(port), 0);
    EXPECT_EQ(sceAudioOut2ContextDestroy(ctx), 0);
}
