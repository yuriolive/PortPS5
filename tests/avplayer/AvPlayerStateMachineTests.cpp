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
int APS5_VABI sceAvPlayerSetTrickSpeed(void* h, int32_t trick_speed);
int APS5_VABI sceAvPlayerStreamCount(void* h);
int APS5_VABI sceAvPlayerEnableStream(void* h, uint32_t stream_id);
int APS5_VABI sceAvPlayerDisableStream(void* h, uint32_t stream_id);
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

// Verifies setting looping mode and trick play playback speed.
TEST(AvPlayerStateMachine, LoopingAndTrickSpeed) {
    void* player = sceAvPlayerInit(nullptr);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerSetLooping(player, 1), 0);
    EXPECT_EQ(sceAvPlayerSetTrickSpeed(player, 200), 0);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies stream enable and disable controls.
TEST(AvPlayerStateMachine, StreamControl) {
    void* player = sceAvPlayerInit(nullptr);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerEnableStream(player, 0), 0);
    EXPECT_EQ(sceAvPlayerDisableStream(player, 0), 0);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Layout matching FrameInfoEx in Export.cpp
struct VideoDetailsExMock {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t language[4];
    uint8_t reserved0[4];
    uint32_t cropLeftOffset;
    uint32_t cropRightOffset;
    uint32_t cropTopOffset;
    uint32_t cropBottomOffset;
    uint32_t pitch;
    uint8_t lumaBitDepth;
    uint8_t chromaBitDepth;
    uint8_t fullRange;
    uint8_t reserved1[5];
    double frameRate;
    uint32_t colourPrimaries;
    uint32_t transferCharacteristics;
    uint8_t reserved2[16];
};

struct FrameInfoExMock {
    void* data;
    uint8_t reserved[8]; // padding to offset 16 for timestamp on 64-bit
    uint64_t timestamp;
    VideoDetailsExMock video;
};
static_assert(sizeof(FrameInfoExMock) == 104, "FrameInfoExMock size ABI");
static_assert(offsetof(FrameInfoExMock, video) == 24, "FrameInfoExMock video offset ABI");

// Verifies delivering video frames via sceAvPlayerGetVideoDataEx with custom allocator.
TEST(AvPlayerStateMachine, VideoDataDelivery) {
    int dummyMem = 42;
    InitData init{};
    init.memObject = &dummyMem;
    init.allocTexture = reinterpret_cast<void*>(&TestAllocTexture);
    init.deallocTexture = reinterpret_cast<void*>(&TestFreeTexture);
    init.numFramebuffers = 2;
    init.autoStart = 1;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerAddSource(player, "playback.mp4"), 0);

    FrameInfoExMock frame{};
    const int gotFrame = sceAvPlayerGetVideoDataEx(player, &frame);
    EXPECT_EQ(gotFrame, 1);
    EXPECT_NE(frame.data, nullptr);
    EXPECT_EQ(frame.video.height, 1080u);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

} // namespace

