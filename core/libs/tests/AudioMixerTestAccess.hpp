// AudioMixerTestAccess.hpp
// Audio tests use this friend to control callback admission and inspect output.
// Callers stop SDL before accessing mixer buffers or source slots; lifecycle
// tests explicitly hold a consumer while a worker attempts reset/reuse.
#pragma once

#include "prx/libSceAudioOut/src/AudioMixer.hpp"

/** @brief Narrow test access for deterministic mixer lifecycle interleavings. */
struct AudioMixerTestAccess {
    /** @brief Pins a source as callback selection would. @return Whether admitted. */
    static bool Acquire(AudioSource& source) { return source.TryAcquireConsumer(); }
    /** @brief Acknowledges the selected callback's final read. */
    static void Release(AudioSource& source) { source.ReleaseConsumer(); }
    /** @brief Reads a pinned ring. @return Frames read into out. */
    static std::uint32_t Pop(AudioSource& source, AudioFrame* out, std::uint32_t count) {
        return source.Pop(out, count);
    }
    /** @brief Runs a stopped device's callback into caller-owned output storage. */
    static void Process(AudioMixer& mixer, float* out, std::uint32_t count) {
        mixer.ProcessCallback(out, count);
    }
    /** @brief Returns the first registered slot for isolated v1 tests. @return Source slot. */
    static AudioSource& FirstSource(AudioMixer& mixer) { return mixer.m_sources.front(); }
    /** @brief Holds clock synchronization to verify test clock reset serialization. @return Lock. */
    static std::unique_lock<std::mutex> LockClock(AudioMixer& mixer) {
        return std::unique_lock(mixer.m_wallClockMutex);
    }
};
