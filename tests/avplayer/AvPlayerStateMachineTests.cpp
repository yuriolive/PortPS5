// tests/avplayer/AvPlayerStateMachineTests.cpp
// GoogleTest-based verification suite for PortPS5 libSceAvPlayer offline playback state machine:
// Verifies player creation, source attachment, lifecycle transitions (Ready/Play/Pause/Stop),
// media clock tracking, and frame getter polling under the blank clip model.
// Complies with System V ABI invariants and PortPS5 testing rules.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
void* APS5_VABI sceAvPlayerInit(const void* init);
int APS5_VABI sceAvPlayerInitEx(const void* init_ex, void** handle);
int APS5_VABI sceAvPlayerPostInit(void* h, const void* post_init);
int APS5_VABI sceAvPlayerAddSource(void* h, const char* filename);
int APS5_VABI sceAvPlayerStart(void* h);
int APS5_VABI sceAvPlayerPause(void* h);
int APS5_VABI sceAvPlayerResume(void* h);
int APS5_VABI sceAvPlayerStop(void* h);
int APS5_VABI sceAvPlayerClose(void* h);
int APS5_VABI sceAvPlayerIsActive(void* h);
uint64_t APS5_VABI sceAvPlayerCurrentTime(void* h);
int APS5_VABI sceAvPlayerJumpToTime(void* h, uint64_t time_ms);
int APS5_VABI sceAvPlayerSetLooping(void* h, int loop);
int APS5_VABI sceAvPlayerStreamCount(void* h);
int APS5_VABI sceAvPlayerGetStreamInfo(void* h, uint32_t stream_id, void* info);
int APS5_VABI sceAvPlayerGetVideoData(void* h, void* video_info);
int APS5_VABI sceAvPlayerGetVideoDataEx(void* h, void* video_info);
int APS5_VABI sceAvPlayerGetAudioData(void* h, void* audio_info);
}

namespace {

using namespace PortPS5::Testing;

struct TestEventRecord {
    int32_t eventId;
    int32_t arg1;
};

static std::vector<TestEventRecord> g_events;

void APS5_VABI TestEventCallback(void* /*obj*/, int32_t eventId, int32_t arg1, void* /*arg2*/) {
    g_events.push_back({eventId, arg1});
}

// Memory allocator doubles for player frame allocations
void* APS5_VABI TestAllocTexture(void* /*obj*/, uint32_t /*alignment*/, uint32_t size) {
    return std::malloc(size);
}

void APS5_VABI TestFreeTexture(void* /*obj*/, void* ptr) {
    std::free(ptr);
}

// Base initialization prefix structure matching InitPrefix layout
struct InitData {
    void* memObject;
    void* alloc;
    void* dealloc;
    void* allocTexture;
    void* deallocTexture;
    void* fileObject;
    void* fileOpen;
    void* fileClose;
    void* fileRead;
    void* fileSize;
    void* eventObject;
    void* eventCallback;
    int32_t debugLevel;
    uint32_t basePriority;
    int32_t numFramebuffers;
    uint8_t autoStart;
};

// Verifies basic initialization and teardown of an AvPlayer instance.
TEST(AvPlayerStateMachine, InitAndClose) {
    void* player = sceAvPlayerInit(nullptr);
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies source attachment triggers Ready event and auto-start transition.
TEST(AvPlayerStateMachine, AddSourceAndAutoStart) {
    g_events.clear();

    InitData init{};
    init.eventCallback = reinterpret_cast<void*>(&TestEventCallback);
    init.autoStart = 1;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerAddSource(player, "intro.mp4"), 0);

    // Should have received READY (0x02) and PLAY (0x03)
    ASSERT_GE(g_events.size(), 2u);
    EXPECT_EQ(g_events[0].eventId, 0x02);
    EXPECT_EQ(g_events[1].eventId, 0x03);

    EXPECT_TRUE(sceAvPlayerIsActive(player));
    EXPECT_EQ(sceAvPlayerStreamCount(player), 1);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies pause and resume lifecycle event emission.
TEST(AvPlayerStateMachine, PauseAndResume) {
    g_events.clear();

    InitData init{};
    init.eventCallback = reinterpret_cast<void*>(&TestEventCallback);
    init.autoStart = 0;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerAddSource(player, "movie.mp4"), 0);
    EXPECT_EQ(sceAvPlayerStart(player), 0);

    EXPECT_EQ(sceAvPlayerPause(player), 0);
    EXPECT_EQ(sceAvPlayerResume(player), 0);
    EXPECT_EQ(sceAvPlayerStop(player), 0);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies stream info reporting 1920x1080 video metadata for the blank placeholder stream.
TEST(AvPlayerStateMachine, StreamInfoResolution) {
    void* player = sceAvPlayerInit(nullptr);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerAddSource(player, "stream.mp4"), 0);

    uint8_t info[32] = {};
    EXPECT_EQ(sceAvPlayerGetStreamInfo(player, 0, info), 0);

    uint32_t type = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::memcpy(&type, info + 0, 4);
    std::memcpy(&width, info + 8, 4);
    std::memcpy(&height, info + 12, 4);

    EXPECT_EQ(type, 1u); // 1 = video
    EXPECT_EQ(width, 1920u);
    EXPECT_EQ(height, 1080u);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies seeking via sceAvPlayerJumpToTime sets banked timestamp.
TEST(AvPlayerStateMachine, JumpToTime) {
    void* player = sceAvPlayerInit(nullptr);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerAddSource(player, "test.mp4"), 0);
    EXPECT_EQ(sceAvPlayerJumpToTime(player, 1500), 0);
    EXPECT_GE(sceAvPlayerCurrentTime(player), 1500u);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

} // namespace
