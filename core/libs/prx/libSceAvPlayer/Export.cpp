// libSceAvPlayer: replacement system library exports forwarding to native AvPlayer implementation.
// Provides identical NID export entry points for non-native link targets under System V ABI.
#include <cstdint>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Forward declarations for functions defined in libSceAvPlayer.native
extern "C" {
void* sceAvPlayerInit(const void* init);
int sceAvPlayerInitEx(const void* init_ex, void** handle);
int sceAvPlayerPostInit(void* h, const void* post_init);
int sceAvPlayerSetLogCallback(void* callback, void* user_data);
int sceAvPlayerAddSource(void* h, const char* filename);
int sceAvPlayerAddSourceEx(void* h, std::uint32_t uri_type, const void* source_details);
int sceAvPlayerStart(void* h);
int sceAvPlayerStartEx(void* h, const void* start_info_ex);
int sceAvPlayerPause(void* h);
int sceAvPlayerResume(void* h);
int sceAvPlayerStop(void* h);
int sceAvPlayerClose(void* h);
Bool sceAvPlayerIsActive(void* h);
std::uint64_t sceAvPlayerCurrentTime(void* h);
int sceAvPlayerJumpToTime(void* h, std::uint64_t time_ms);
int sceAvPlayerSetLooping(void* h, Bool loop);
int sceAvPlayerSetAvSyncMode(void* h, std::uint32_t sync_mode);
int sceAvPlayerSetTrickSpeed(void* h, std::int32_t trick_speed);
int sceAvPlayerSetAvailableBandwidth(void* h, std::uint32_t start_bandwidth, std::uint32_t minimum_bandwidth, std::uint32_t maximum_bandwidth);
int sceAvPlayerStreamCount(void* h);
int sceAvPlayerEnableStream(void* h, std::uint32_t stream_id);
int sceAvPlayerDisableStream(void* h, std::uint32_t stream_id);
int sceAvPlayerChangeStream(void* h, std::uint32_t old_stream_id, std::uint32_t new_stream_id);
int sceAvPlayerGetStreamInfo(void* h, std::uint32_t stream_id, void* info);
int sceAvPlayerGetStreamInfoEx(void* h, std::uint32_t stream_id, void* info);
Bool sceAvPlayerGetVideoData(void* h, void* video_info);
Bool sceAvPlayerGetVideoDataEx(void* h, void* video_info);
Bool sceAvPlayerGetAudioData(void* h, void* audio_info);
}

namespace PortPS5::LibSceAvPlayer {

// Initializes base AvPlayer session forwarding to native implementation.
void* APS5_VABI sceAvPlayerInit(const void* init) {
    return ::sceAvPlayerInit(init);
}

// Initializes extended AvPlayer session forwarding to native implementation.
int APS5_VABI sceAvPlayerInitEx(const void* init_ex, void** handle) {
    return ::sceAvPlayerInitEx(init_ex, handle);
}

// Configures post-initialization state forwarding to native implementation.
int APS5_VABI sceAvPlayerPostInit(void* h, const void* post_init) {
    return ::sceAvPlayerPostInit(h, post_init);
}

// Registers logging callback forwarding to native implementation.
int APS5_VABI sceAvPlayerSetLogCallback(void* callback, void* user_data) {
    return ::sceAvPlayerSetLogCallback(callback, user_data);
}

// Adds media source file forwarding to native implementation.
int APS5_VABI sceAvPlayerAddSource(void* h, const char* filename) {
    return ::sceAvPlayerAddSource(h, filename);
}

// Adds extended media source forwarding to native implementation.
int APS5_VABI sceAvPlayerAddSourceEx(void* h, std::uint32_t uri_type, const void* source_details) {
    return ::sceAvPlayerAddSourceEx(h, uri_type, source_details);
}

// Starts playback forwarding to native implementation.
int APS5_VABI sceAvPlayerStart(void* h) {
    return ::sceAvPlayerStart(h);
}

// Starts playback with extended options forwarding to native implementation.
int APS5_VABI sceAvPlayerStartEx(void* h, const void* start_info_ex) {
    return ::sceAvPlayerStartEx(h, start_info_ex);
}

// Pauses playback forwarding to native implementation.
int APS5_VABI sceAvPlayerPause(void* h) {
    return ::sceAvPlayerPause(h);
}

// Resumes playback forwarding to native implementation.
int APS5_VABI sceAvPlayerResume(void* h) {
    return ::sceAvPlayerResume(h);
}

// Stops playback forwarding to native implementation.
int APS5_VABI sceAvPlayerStop(void* h) {
    return ::sceAvPlayerStop(h);
}

// Closes player instance forwarding to native implementation.
int APS5_VABI sceAvPlayerClose(void* h) {
    return ::sceAvPlayerClose(h);
}

// Queries active presentation state forwarding to native implementation.
Bool APS5_VABI sceAvPlayerIsActive(void* h) {
    return ::sceAvPlayerIsActive(h);
}

// Queries current media clock timestamp forwarding to native implementation.
std::uint64_t APS5_VABI sceAvPlayerCurrentTime(void* h) {
    return ::sceAvPlayerCurrentTime(h);
}

// Seeks media clock forwarding to native implementation.
int APS5_VABI sceAvPlayerJumpToTime(void* h, std::uint64_t time_ms) {
    return ::sceAvPlayerJumpToTime(h, time_ms);
}

// Configures looping forwarding to native implementation.
int APS5_VABI sceAvPlayerSetLooping(void* h, Bool loop) {
    return ::sceAvPlayerSetLooping(h, loop);
}

// Sets audio/video sync mode forwarding to native implementation.
int APS5_VABI sceAvPlayerSetAvSyncMode(void* h, std::uint32_t sync_mode) {
    return ::sceAvPlayerSetAvSyncMode(h, sync_mode);
}

// Sets trick play speed multiplier forwarding to native implementation.
int APS5_VABI sceAvPlayerSetTrickSpeed(void* h, std::int32_t trick_speed) {
    return ::sceAvPlayerSetTrickSpeed(h, trick_speed);
}

// Sets streaming bandwidth limits forwarding to native implementation.
int APS5_VABI sceAvPlayerSetAvailableBandwidth(void* h, std::uint32_t start_bandwidth, std::uint32_t minimum_bandwidth, std::uint32_t maximum_bandwidth) {
    return ::sceAvPlayerSetAvailableBandwidth(h, start_bandwidth, minimum_bandwidth, maximum_bandwidth);
}

// Queries stream count forwarding to native implementation.
int APS5_VABI sceAvPlayerStreamCount(void* h) {
    return ::sceAvPlayerStreamCount(h);
}

// Enables stream forwarding to native implementation.
int APS5_VABI sceAvPlayerEnableStream(void* h, std::uint32_t stream_id) {
    return ::sceAvPlayerEnableStream(h, stream_id);
}

// Disables stream forwarding to native implementation.
int APS5_VABI sceAvPlayerDisableStream(void* h, std::uint32_t stream_id) {
    return ::sceAvPlayerDisableStream(h, stream_id);
}

// Changes stream ID forwarding to native implementation.
int APS5_VABI sceAvPlayerChangeStream(void* h, std::uint32_t old_stream_id, std::uint32_t new_stream_id) {
    return ::sceAvPlayerChangeStream(h, old_stream_id, new_stream_id);
}

// Retrieves stream info forwarding to native implementation.
int APS5_VABI sceAvPlayerGetStreamInfo(void* h, std::uint32_t stream_id, void* info) {
    return ::sceAvPlayerGetStreamInfo(h, stream_id, info);
}

// Retrieves extended stream info forwarding to native implementation.
int APS5_VABI sceAvPlayerGetStreamInfoEx(void* h, std::uint32_t stream_id, void* info) {
    return ::sceAvPlayerGetStreamInfoEx(h, stream_id, info);
}

// Retrieves video data buffer forwarding to native implementation.
Bool APS5_VABI sceAvPlayerGetVideoData(void* h, void* video_info) {
    return ::sceAvPlayerGetVideoData(h, video_info);
}

// Retrieves extended video data buffer forwarding to native implementation.
Bool APS5_VABI sceAvPlayerGetVideoDataEx(void* h, void* video_info) {
    return ::sceAvPlayerGetVideoDataEx(h, video_info);
}

// Retrieves audio data buffer forwarding to native implementation.
Bool APS5_VABI sceAvPlayerGetAudioData(void* h, void* audio_info) {
    return ::sceAvPlayerGetAudioData(h, audio_info);
}

}  // namespace PortPS5::LibSceAvPlayer
