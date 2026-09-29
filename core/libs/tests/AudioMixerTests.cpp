// AudioMixerTests.cpp
// PortPS5 - Audio Mixer GoogleTest Suite (Audio Subsystem M2)
//
// Purpose:
//   Unit test suite verifying the M2 audio mixer target design:
//     - Downmix matrix, LFE folding (-10 dB), and soft limiter headroom.
//     - Format channel decoding from data_format bits 8..11.
//     - SDL_AudioStream resampler length, phase fidelity, and drift tracking.
//     - Driverless mixer execution: 0 underruns over 10 simulated minutes steady
//       push, and exact N underruns counted for N injected gaps.
//     - Overrun drop ceiling enforcement past 100 ms.
//
// Invariants Verified:
//   - LFE folds to front pair at -10 dB (gain ~0.316228) per M2 specification.
//   - Soft limiter is transparent for amplitudes <= 0.8 and strictly < 1.0 for peaks.
//   - Steady real-time pushes never record underruns.
//   - Each callback experiencing a buffer shortfall registers exactly one underrun.

#define SDL_MAIN_HANDLED
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <future>
#include <thread>

#include "AudioMixerTestAccess.hpp"

#include "SDL.h"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceAudioOut/src/AudioMixer.hpp"
#include "prx/libSceAudioOut/src/AudioOut2Internal.hpp"

// ---------------------------------------------------------------------------
// 1. Downmix Matrix & LFE Fold Tests
// ---------------------------------------------------------------------------

// Verifies multi-channel downmix summing, -3 dB fold for C/RL/RR/SL/SR, and -10 dB fold for LFE.
TEST(AudioMixerTests, DownmixMatrixAndLfeFold) {
    // Mono fan-out: single channel duplicates to Left and Right
    {
        float in[1] = {2.0f};
        float vol[8] = {0.5f, 0.5f, 1, 1, 1, 1, 1, 1};
        float out[2] = {0, 0};
        AudioOut2DownmixFrame(in, 1, vol, out);
        EXPECT_NEAR(out[0], 1.0f, 1e-5f);
        EXPECT_NEAR(out[1], 1.0f, 1e-5f);
    }

    // Stereo separation: Left and Right channels remain independent
    {
        float in[2] = {1.5f, 3.0f};
        float vol[8] = {0.5f, 0.25f, 1, 1, 1, 1, 1, 1};
        float out[2] = {0, 0};
        AudioOut2DownmixFrame(in, 2, vol, out);
        EXPECT_NEAR(out[0], 0.75f, 1e-5f);
        EXPECT_NEAR(out[1], 0.75f, 1e-5f);
    }

    // 8-channel bed downmix:
    // Testing isolated channels one by one to verify individual matrix coefficients.
    {
        // Channel 0: Front Left (unity)
        float inFL[8] = {1.0f, 0, 0, 0, 0, 0, 0, 0};
        float outL = 0.0f, outR = 0.0f;
        AudioMixerDownmix8Ch(inFL, nullptr, outL, outR);
        EXPECT_NEAR(outL, 1.0f, 1e-5f);
        EXPECT_NEAR(outR, 0.0f, 1e-5f);

        // Channel 1: Front Right (unity)
        float inFR[8] = {0, 1.0f, 0, 0, 0, 0, 0, 0};
        AudioMixerDownmix8Ch(inFR, nullptr, outL, outR);
        EXPECT_NEAR(outL, 0.0f, 1e-5f);
        EXPECT_NEAR(outR, 1.0f, 1e-5f);

        // Channel 2: Centre (-3 dB = 0.70710678f into both Left and Right)
        float inC[8] = {0, 0, 1.0f, 0, 0, 0, 0, 0};
        AudioMixerDownmix8Ch(inC, nullptr, outL, outR);
        EXPECT_NEAR(outL, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);
        EXPECT_NEAR(outR, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);

        // Channel 3: LFE (-10 dB = 0.31622777f into both Left and Right per M2 target design)
        float inLFE[8] = {0, 0, 0, 1.0f, 0, 0, 0, 0};
        AudioMixerDownmix8Ch(inLFE, nullptr, outL, outR);
        EXPECT_NEAR(outL, AUDIO_MIXER_DOWNMIX_10DB, 1e-5f);
        EXPECT_NEAR(outR, AUDIO_MIXER_DOWNMIX_10DB, 1e-5f);

        // Channel 4: Rear Left (-3 dB into Left only)
        float inRL[8] = {0, 0, 0, 0, 1.0f, 0, 0, 0};
        AudioMixerDownmix8Ch(inRL, nullptr, outL, outR);
        EXPECT_NEAR(outL, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);
        EXPECT_NEAR(outR, 0.0f, 1e-5f);

        // Channel 5: Rear Right (-3 dB into Right only)
        float inRR[8] = {0, 0, 0, 0, 0, 1.0f, 0, 0};
        AudioMixerDownmix8Ch(inRR, nullptr, outL, outR);
        EXPECT_NEAR(outL, 0.0f, 1e-5f);
        EXPECT_NEAR(outR, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);

        // Channel 6: Side Left (-3 dB into Left only)
        float inSL[8] = {0, 0, 0, 0, 0, 0, 1.0f, 0};
        AudioMixerDownmix8Ch(inSL, nullptr, outL, outR);
        EXPECT_NEAR(outL, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);
        EXPECT_NEAR(outR, 0.0f, 1e-5f);

        // Channel 7: Side Right (-3 dB into Right only)
        float inSR[8] = {0, 0, 0, 0, 0, 0, 0, 1.0f};
        AudioMixerDownmix8Ch(inSR, nullptr, outL, outR);
        EXPECT_NEAR(outL, 0.0f, 1e-5f);
        EXPECT_NEAR(outR, AUDIO_MIXER_DOWNMIX_3DB, 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// 2. Soft Limiter Headroom Tests
// ---------------------------------------------------------------------------

// Verifies soft limiter transparency within linear range and smooth saturation on high peaks.
TEST(AudioMixerTests, SoftLimiterHeadroom) {
    // Transparency for normal audio levels (|x| <= 0.8)
    EXPECT_FLOAT_EQ(SoftLimit(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(SoftLimit(0.3f), 0.3f);
    EXPECT_FLOAT_EQ(SoftLimit(0.8f), 0.8f);
    EXPECT_FLOAT_EQ(SoftLimit(-0.5f), -0.5f);
    EXPECT_FLOAT_EQ(SoftLimit(-0.8f), -0.8f);

    // Smooth compression above 0.8
    const float atOne = SoftLimit(1.0f);
    EXPECT_GT(atOne, 0.8f);
    EXPECT_LT(atOne, 1.0f);

    // Title peak headroom test: upstream comment noted peak at 2.1
    const float atPeak = SoftLimit(2.1f);
    EXPECT_GT(atPeak, atOne);
    EXPECT_LT(atPeak, 1.0f);

    // Extreme peaks strictly stay within [-1.0, 1.0] without hard digital wrapping
    EXPECT_LE(SoftLimit(5.0f), 1.0f);
    EXPECT_LE(SoftLimit(100.0f), 1.0f);
    EXPECT_GE(SoftLimit(-5.0f), -1.0f);
    EXPECT_GE(SoftLimit(-100.0f), -1.0f);

    // Strict monotonicity: f(x1) < f(x2) for x1 < x2
    const float steps[] = {-10.0f, -2.1f, -1.0f, -0.8f, -0.2f, 0.0f, 0.2f, 0.8f, 1.0f, 2.1f, 10.0f};
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]) - 1; i++) {
        EXPECT_LT(SoftLimit(steps[i]), SoftLimit(steps[i + 1]));
    }
}

// ---------------------------------------------------------------------------
// 3. Format Channel Decoding Tests
// ---------------------------------------------------------------------------

// Verifies data_format channel bitfield extraction (bits 8..11).
TEST(AudioMixerTests, DataFormatChannelDecode) {
    EXPECT_EQ(AudioOut2DecodeChannels(0x0100), 1u); // Mono
    EXPECT_EQ(AudioOut2DecodeChannels(0x0200), 2u); // Stereo
    EXPECT_EQ(AudioOut2DecodeChannels(0x0880), 8u); // 8-channel bed
    EXPECT_EQ(AudioOut2DecodeChannels(0x0000), 0u); // Unspecified format
    EXPECT_EQ(AudioOut2DecodeChannels(0x0F00), 0u); // Invalid format
    EXPECT_EQ(AudioOut2DecodeChannels(0x0400), 0u); // 4-channel unhandled format
}

// ---------------------------------------------------------------------------
// 4. Resampler Length, Phase & Drift Tests
// ---------------------------------------------------------------------------

// Verifies SDL_AudioStream resampling length ratios, sinusoidal fidelity, and block drift stability.
TEST(AudioMixerTests, ResamplerLengthPhaseDrift) {
    // Resampling length check: 44.1 kHz -> 48 kHz
    {
        SDL_AudioStream* stream = SDL_NewAudioStream(
            AUDIO_F32SYS, 2, 44100,
            AUDIO_F32SYS, 2, 48000);
        ASSERT_NE(stream, nullptr);

        constexpr std::size_t inFrames = 44100;
        std::vector<AudioFrame> in(inFrames, AudioFrame{0.5f, -0.5f});
        int res = SDL_AudioStreamPut(stream, in.data(), static_cast<int>(inFrames * sizeof(AudioFrame)));
        EXPECT_EQ(res, 0);

        int avail = SDL_AudioStreamAvailable(stream);
        int outFrames = avail / static_cast<int>(sizeof(AudioFrame));
        // 44100 input at 44.1 kHz resampled to 48 kHz yields ~48000 frames minus resampler filter latency (~558 frames)
        EXPECT_NEAR(outFrames, 47442, 20);

        std::vector<AudioFrame> out(outFrames);
        int got = SDL_AudioStreamGet(stream, out.data(), avail);
        EXPECT_GT(got, 0);

        // A second second of audio produces exactly the steady-state 48,000 frames
        EXPECT_EQ(SDL_AudioStreamPut(stream, in.data(), static_cast<int>(inFrames * sizeof(AudioFrame))), 0);
        avail = SDL_AudioStreamAvailable(stream);
        outFrames = avail / static_cast<int>(sizeof(AudioFrame));
        EXPECT_NEAR(outFrames, 48000, 20);

        SDL_FreeAudioStream(stream);
    }

    // Sinusoidal phase and frequency fidelity: 32 kHz -> 48 kHz for a 1000 Hz sine wave
    {
        SDL_AudioStream* stream = SDL_NewAudioStream(
            AUDIO_F32SYS, 2, 32000,
            AUDIO_F32SYS, 2, 48000);
        ASSERT_NE(stream, nullptr);

        constexpr std::size_t inFrames = 32000;
        std::vector<AudioFrame> in(inFrames);
        for (std::size_t i = 0; i < inFrames; i++) {
            const float t = static_cast<float>(i) / 32000.0f;
            const float val = std::sin(2.0f * 3.14159265f * 1000.0f * t);
            in[i] = AudioFrame{val, val};
        }

        EXPECT_EQ(SDL_AudioStreamPut(stream, in.data(), static_cast<int>(inFrames * sizeof(AudioFrame))), 0);
        int avail = SDL_AudioStreamAvailable(stream);
        int outFrames = avail / static_cast<int>(sizeof(AudioFrame));
        std::vector<AudioFrame> out(outFrames);
        EXPECT_GT(SDL_AudioStreamGet(stream, out.data(), avail), 0);

        // After filter latency settling (sample 500+), verify peak amplitude is preserved within 5%
        float maxL = 0.0f;
        for (int i = 500; i < outFrames - 500; i++) {
            maxL = std::max(maxL, std::abs(out[i].left));
        }
        EXPECT_NEAR(maxL, 1.0f, 0.05f);

        SDL_FreeAudioStream(stream);
    }

    // Drift test: push 100 successive blocks of 441 frames (10 ms each at 44.1 kHz)
    {
        SDL_AudioStream* stream = SDL_NewAudioStream(
            AUDIO_F32SYS, 2, 44100,
            AUDIO_F32SYS, 2, 48000);
        ASSERT_NE(stream, nullptr);

        std::vector<AudioFrame> block(441, AudioFrame{0.2f, 0.2f});

        // Warm up resampler filter pipeline (initial filter latency settling: 10 blocks = 4410 samples)
        for (int w = 0; w < 10; w++) {
            EXPECT_EQ(SDL_AudioStreamPut(stream, block.data(), static_cast<int>(block.size() * sizeof(AudioFrame))), 0);
        }
        int initialAvail = SDL_AudioStreamAvailable(stream);
        if (initialAvail > 0) {
            std::vector<AudioFrame> discard(initialAvail / sizeof(AudioFrame));
            SDL_AudioStreamGet(stream, discard.data(), initialAvail);
        }

        // Measure steady-state frame rate over 100 blocks (1.0 second):
        // 441 * (48000 / 44100) = exactly 480 frames per block -> 48,000 frames total.
        std::size_t steadyStateFrames = 0;
        for (int b = 0; b < 100; b++) {
            EXPECT_EQ(SDL_AudioStreamPut(stream, block.data(), static_cast<int>(block.size() * sizeof(AudioFrame))), 0);
            int avail = SDL_AudioStreamAvailable(stream);
            if (avail >= static_cast<int>(sizeof(AudioFrame))) {
                int count = avail / static_cast<int>(sizeof(AudioFrame));
                std::vector<AudioFrame> buf(count);
                SDL_AudioStreamGet(stream, buf.data(), avail);
                steadyStateFrames += count;
            }
        }

        // Over the steady-state 1.0 second period (100 * 441 samples), ~48,000 samples are output with zero long-term drift
        EXPECT_NEAR(static_cast<double>(steadyStateFrames), 48000.0, 150.0);

        SDL_FreeAudioStream(stream);
    }
}

// ---------------------------------------------------------------------------
// 5. Driverless Mixer Tests (Steady Push & Injected Gaps)
// ---------------------------------------------------------------------------

// Verifies that a steady real-time push produces 0 underruns over 10 simulated minutes.
TEST(AudioMixerTests, DriverlessSteadyPushZeroUnderruns) {
    AudioMixer& mixer = AudioMixer::Get();
    mixer.Initialize();
    mixer.ForceWallClockForTesting();
    mixer.ResetTelemetryForTesting();

    AudioSource* source = mixer.RegisterSource(48000, 2);
    ASSERT_NE(source, nullptr);

    // 10 simulated minutes at 48 kHz:
    // 600 seconds * 48000 = 28,800,000 frames.
    // In grains of 512 samples: 56,250 iterations.
    constexpr std::uint32_t kGrainFrames = 512;
    constexpr std::size_t kIterations = 56250;
    std::vector<AudioFrame> grain(kGrainFrames, AudioFrame{0.1f, 0.1f});

    for (std::size_t i = 0; i < kIterations; i++) {
        // Producer pushes one grain
        source->PushStereo48k(grain.data(), kGrainFrames);
        // Driverless callback simulates hardware consumption
        mixer.SimulateCallback(kGrainFrames);
    }

    EXPECT_EQ(mixer.GetUnderruns(), 0u);
    EXPECT_EQ(mixer.GetFramesConsumed(), 28800000u);

    mixer.UnregisterSource(source);
}

// Verifies that injecting N buffer starvation gaps counts exactly N underrun events.
TEST(AudioMixerTests, DriverlessInjectedGapsCounted) {
    AudioMixer& mixer = AudioMixer::Get();
    mixer.Initialize();
    mixer.ForceWallClockForTesting();
    mixer.ResetTelemetryForTesting();

    AudioSource* source = mixer.RegisterSource(48000, 2);
    ASSERT_NE(source, nullptr);

    constexpr std::uint32_t kGrainFrames = 512;
    std::vector<AudioFrame> grain(kGrainFrames, AudioFrame{0.1f, 0.1f});

    // Prime with one steady grain and consume
    source->PushStereo48k(grain.data(), kGrainFrames);
    mixer.SimulateCallback(kGrainFrames);
    EXPECT_EQ(mixer.GetUnderruns(), 0u);

    // Inject exactly N = 7 starved callbacks without pushing
    constexpr std::uint32_t kInjectedGaps = 7;
    for (std::uint32_t g = 0; g < kInjectedGaps; g++) {
        mixer.SimulateCallback(kGrainFrames);
    }

    EXPECT_EQ(mixer.GetUnderruns(), kInjectedGaps);

    mixer.UnregisterSource(source);
}

// ---------------------------------------------------------------------------
// 6. Overrun Drop Ceiling Tests
// ---------------------------------------------------------------------------

// Verifies that pushing frames past the 100 ms ring ceiling drops excess grains and counts overrun drops.
TEST(AudioMixerTests, OverrunDropCeiling) {
    AudioMixer& mixer = AudioMixer::Get();
    mixer.Initialize();
    mixer.ForceWallClockForTesting();
    mixer.ResetTelemetryForTesting();

    AudioSource* source = mixer.RegisterSource(48000, 2);
    ASSERT_NE(source, nullptr);

    // 100 ms ceiling at 48 kHz is 4800 frames. Fill to 4608 frames (9 * 512)
    std::vector<AudioFrame> grain(512, AudioFrame{0.1f, 0.1f});
    for (int i = 0; i < 9; i++) {
        EXPECT_TRUE(source->PushStereo48k(grain.data(), 512));
    }
    EXPECT_EQ(source->GetQueuedFrames(), 4608u);

    // Next push: 4608 + 512 = 5120 > 4800 ceiling. Must be dropped!
    const bool accepted = source->PushStereo48k(grain.data(), 512);
    EXPECT_FALSE(accepted);
    EXPECT_EQ(source->GetQueuedFrames(), 4608u);
    EXPECT_GE(mixer.GetOverrunDrops(), 1u);

    mixer.UnregisterSource(source);
}

// Holds callback admission across Reset/Init to prove old frames cannot be
// cleared or overwritten until the selected consumer acknowledges its read.
TEST(AudioMixerTests, LifecycleWaitsForSelectedConsumer) {
    AudioSource source;
    for (bool reinitialize : {false, true}) {
        source.Init(48000, 2);
        const AudioFrame oldFrame{0.25f, -0.5f};
        ASSERT_TRUE(source.PushStereo48k(&oldFrame, 1));
        ASSERT_TRUE(AudioMixerTestAccess::Acquire(source));
        auto reset = std::async(std::launch::async, [&] {
            if (reinitialize) source.Init(44100, 2);
            else source.Reset();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (source.IsActive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        EXPECT_FALSE(source.IsActive());
        EXPECT_EQ(reset.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
        EXPECT_FALSE(AudioMixerTestAccess::Acquire(source));
        AudioFrame read;
        EXPECT_EQ(AudioMixerTestAccess::Pop(source, &read, 1), 1u);
        EXPECT_FLOAT_EQ(read.left, oldFrame.left);
        EXPECT_FLOAT_EQ(read.right, oldFrame.right);
        AudioMixerTestAccess::Release(source);
        reset.get();
        EXPECT_EQ(source.GetQueuedFrames(), 0u);
        EXPECT_EQ(source.IsActive(), reinitialize);
        if (reinitialize) EXPECT_EQ(source.GetSampleRate(), 44100u);
    }
}

// A multi-chunk request must sum each source, apply the limiter, pad exhausted
// inputs with silence, and count only one underrun for the entire callback.
TEST(AudioMixerTests, OversizedCallbackMixesChunksAndSilentTail) {
    auto& mixer = AudioMixer::Get();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    ASSERT_TRUE(mixer.Initialize());
    mixer.ForceWallClockForTesting();
    auto* first = mixer.RegisterSource(48000, 2);
    auto* second = mixer.RegisterSource(48000, 2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    std::vector<AudioFrame> a(1300, AudioFrame{0.6f, -0.2f});
    std::vector<AudioFrame> b(700, AudioFrame{0.5f, 0.4f});
    ASSERT_TRUE(first->PushStereo48k(a.data(), a.size()));
    ASSERT_TRUE(second->PushStereo48k(b.data(), b.size()));
    mixer.ResetTelemetryForTesting();
    std::vector<float> output(1700 * 2, 99.0f);
    AudioMixerTestAccess::Process(mixer, output.data(), 1700);
    for (std::size_t i = 0; i < 1700; ++i) {
        EXPECT_FLOAT_EQ(output[2 * i], i < 700 ? SoftLimit(1.1f) : i < 1300 ? 0.6f : 0.0f);
        EXPECT_FLOAT_EQ(output[2 * i + 1], i < 700 ? 0.2f : i < 1300 ? -0.2f : 0.0f);
    }
    EXPECT_EQ(first->GetQueuedFrames(), 0u);
    EXPECT_EQ(second->GetQueuedFrames(), 0u);
    EXPECT_EQ(mixer.GetUnderruns(), 1u);
    mixer.UnregisterSource(first);
    mixer.UnregisterSource(second);
    // Empty callbacks must overwrite prior output, including a partial chunk.
    AudioMixerTestAccess::Process(mixer, output.data(), 1700);
    for (float sample : output) EXPECT_FLOAT_EQ(sample, 0.0f);
    mixer.Shutdown();
}

// Failure must not latch initialization: changing to an available driver allows
// a later call to open successfully, and no-device sources still retire on shutdown.
TEST(AudioMixerTests, InitializeRetriesAfterDeviceFailure) {
    auto& mixer = AudioMixer::Get();
    mixer.Shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_setenv("SDL_AUDIODRIVER", "portps5-nonexistent-driver", 1);
    EXPECT_FALSE(mixer.Initialize());
    EXPECT_FALSE(mixer.HasDevice());
    auto* source = mixer.RegisterSource(48000, 2);
    ASSERT_NE(source, nullptr);
    mixer.Shutdown();
    EXPECT_FALSE(source->IsActive());
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    EXPECT_TRUE(mixer.Initialize());
    EXPECT_TRUE(mixer.HasDevice());
    EXPECT_TRUE(mixer.Initialize());
    mixer.Shutdown();
}

// ForceWallClockForTesting must take the same mutex as fallback/simulation
// before replacing the clock epoch, even after the device is already closed.
TEST(AudioMixerTests, ForceWallClockWaitsForClockMutex) {
    auto& mixer = AudioMixer::Get();
    mixer.Shutdown();
    auto lock = AudioMixerTestAccess::LockClock(mixer);
    std::promise<void> started;
    auto reset = std::async(std::launch::async, [&] {
        started.set_value();
        mixer.ForceWallClockForTesting();
    });
    started.get_future().wait();
    EXPECT_EQ(reset.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    lock.unlock();
    reset.get();
}
