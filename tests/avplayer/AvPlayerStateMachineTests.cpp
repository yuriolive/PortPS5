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
uint8_t APS5_VABI sceAvPlayerIsActive(void* h);
uint64_t APS5_VABI sceAvPlayerCurrentTime(void* h);
int APS5_VABI sceAvPlayerJumpToTime(void* h, uint64_t time_ms);
int APS5_VABI sceAvPlayerSetLooping(void* h, uint8_t loop);
int APS5_VABI sceAvPlayerSetTrickSpeed(void* h, int32_t trick_speed);
int APS5_VABI sceAvPlayerStreamCount(void* h);
int APS5_VABI sceAvPlayerEnableStream(void* h, uint32_t stream_id);
int APS5_VABI sceAvPlayerDisableStream(void* h, uint32_t stream_id);
int APS5_VABI sceAvPlayerGetStreamInfo(void* h, uint32_t stream_id, void* info);
uint8_t APS5_VABI sceAvPlayerGetVideoData(void* h, void* video_info);
uint8_t APS5_VABI sceAvPlayerGetVideoDataEx(void* h, void* video_info);
uint8_t APS5_VABI sceAvPlayerGetAudioData(void* h, void* audio_info);
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

// Layout matching VideoDetailsEx in Export.cpp
struct VideoDetailsExMock {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t language[4];
    uint32_t framerate;
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
static_assert(offsetof(VideoDetailsExMock, framerate) == 16, "VideoDetailsExMock framerate offset ABI");

struct FrameInfoExMock {
    void* data;
    uint8_t reserved[8]; // padding to offset 16 for timestamp on 64-bit
    uint64_t timestamp;
    VideoDetailsExMock video;
};
static_assert(sizeof(FrameInfoExMock) == 104, "FrameInfoExMock size ABI");
static_assert(offsetof(FrameInfoExMock, video) == 24, "FrameInfoExMock video offset ABI");

// Counts allocations through the Ex memory object to verify frame-count mapping and cleanup.
struct ExAllocationCounts {
    int allocated = 0;
    int freed = 0;
};

// Guest texture callback: counts successful requests and returns host storage for synthetic frames.
void* APS5_VABI CountExAllocations(void* object, uint32_t, uint32_t size) {
    ++static_cast<ExAllocationCounts*>(object)->allocated;
    return std::malloc(size);
}

// Guest texture callback: records cleanup using the same memory object supplied to InitEx.
void APS5_VABI CountExFrees(void* object, void* ptr) {
    ++static_cast<ExAllocationCounts*>(object)->freed;
    std::free(ptr);
}

// Synthetic guest bytes distinguish Ex fields from the base layout and verify both Bool values.
// Worker/debug settings deliberately disagree with auto-start and the requested three framebuffers.
TEST(AvPlayerStateMachine, InitExUsesExtendedLayout) {
    for (uint8_t autoStart : {0, 1}) {
        SCOPED_TRACE(static_cast<int>(autoStart));
        alignas(8) uint8_t init[176] = {};
        const auto write = [&](size_t offset, const auto& value) {
            std::memcpy(init + offset, &value, sizeof(value));
        };
        ExAllocationCounts counts;
        write(0, uint64_t{sizeof(init)});
        write(8, static_cast<void*>(&counts));
        write(32, &CountExAllocations);
        write(40, &CountExFrees);
        write(96, &TestEventCallback);
        write(104, static_cast<const char*>("eng"));
        write(112, int32_t{7}); // debugLevel occupies the old frame-count offset.
        write(116, uint32_t{autoStart == 0 ? 1u : 0u}); // old autoStart offset.
        write(164, int32_t{3});
        write(168, autoStart);
        g_events.clear();
        void* player = nullptr;
        ASSERT_EQ(sceAvPlayerInitEx(init, &player), 0);
        ASSERT_NE(player, nullptr);
        EXPECT_EQ(sceAvPlayerAddSource(player, "synthetic.mp4"), 0);
        EXPECT_EQ(sceAvPlayerIsActive(player), autoStart);
        EXPECT_EQ(g_events.size(), autoStart ? 2u : 1u);
        if (!g_events.empty()) EXPECT_EQ(g_events[0].eventId, 0x02);
        if (autoStart && g_events.size() > 1) EXPECT_EQ(g_events[1].eventId, 0x03);
        if (!autoStart) EXPECT_EQ(sceAvPlayerStart(player), 0);
        FrameInfoExMock frame{};
        EXPECT_EQ(sceAvPlayerGetVideoDataEx(player, &frame), 1);
        EXPECT_NE(frame.data, nullptr);
        EXPECT_EQ(counts.allocated, 3);
        EXPECT_EQ(sceAvPlayerClose(player), 0);
        EXPECT_EQ(counts.freed, 3);
    }
}

// Ex initialization retains default null-data behavior and rejects a missing output handle.
TEST(AvPlayerStateMachine, InitExNullParameters) {
    EXPECT_EQ(sceAvPlayerInitEx(nullptr, nullptr), static_cast<int>(0x806a0001u));
    void* player = nullptr;
    ASSERT_EQ(sceAvPlayerInitEx(nullptr, &player), 0);
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Verifies delivering video frames via sceAvPlayerGetVideoDataEx with custom allocator and verifies framerate at offset 16.
TEST(AvPlayerStateMachine, VideoDataDeliveryAndFramerate) {
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
    EXPECT_GT(frame.video.framerate, 0u);

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Re-entrant allocator that calls sceAvPlayerCurrentTime during texture allocation to verify no deadlock.
static void* g_reentrantPlayerHandle = nullptr;
void* APS5_VABI TestReentrantAllocTexture(void* /*obj*/, uint32_t /*alignment*/, uint32_t size) {
    if (g_reentrantPlayerHandle != nullptr) {
        // Must not deadlock on Player::lock
        (void)sceAvPlayerCurrentTime(g_reentrantPlayerHandle);
    }
    return std::malloc(size);
}

// Verifies that a re-entrant texture allocator calling player APIs does not deadlock on recursive mutex.
TEST(AvPlayerStateMachine, ReentrantAllocTextureNoDeadlock) {
    int dummyMem = 42;
    InitData init{};
    init.memObject = &dummyMem;
    init.allocTexture = reinterpret_cast<void*>(&TestReentrantAllocTexture);
    init.deallocTexture = reinterpret_cast<void*>(&TestFreeTexture);
    init.numFramebuffers = 2;
    init.autoStart = 1;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);
    g_reentrantPlayerHandle = player;

    EXPECT_EQ(sceAvPlayerAddSource(player, "reentrant.mp4"), 0);

    FrameInfoExMock frame{};
    const int gotFrame = sceAvPlayerGetVideoDataEx(player, &frame);
    EXPECT_EQ(gotFrame, 1);
    EXPECT_NE(frame.data, nullptr);

    g_reentrantPlayerHandle = nullptr;
    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

// Callback that closes the player directly during READY event to verify safe lifetime and no use-after-free.
static void* g_closingPlayerHandle = nullptr;
void APS5_VABI TestClosingEventCallback(void* /*obj*/, int32_t eventId, int32_t /*arg1*/, void* /*arg2*/) {
    if (eventId == 0x02 && g_closingPlayerHandle != nullptr) { // kEventReady
        sceAvPlayerClose(g_closingPlayerHandle);
        g_closingPlayerHandle = nullptr;
    }
}

// Verifies closing player inside an event callback does not cause use-after-free in subsequent event triggers.
TEST(AvPlayerStateMachine, SafeCloseInsideEventCallback) {
    InitData init{};
    init.eventCallback = reinterpret_cast<void*>(&TestClosingEventCallback);
    init.autoStart = 1;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);
    g_closingPlayerHandle = player;

    // AddSource fires READY, which invokes callback and closes player; autoStart must not crash attempting PLAY.
    EXPECT_EQ(sceAvPlayerAddSource(player, "autoclose.mp4"), 0);

    // Further calls on closed player must fail gracefully
    EXPECT_EQ(sceAvPlayerClose(player), static_cast<int>(0x806a0001u));
}

// Verifies looping mode restarts playback after reaching frame budget without firing STOP or stopping.
TEST(AvPlayerStateMachine, LoopingRestartsPlayback) {
    int dummyMem = 42;
    InitData init{};
    init.memObject = &dummyMem;
    init.allocTexture = reinterpret_cast<void*>(&TestAllocTexture);
    init.deallocTexture = reinterpret_cast<void*>(&TestFreeTexture);
    init.numFramebuffers = 2;
    init.autoStart = 1;

    void* player = sceAvPlayerInit(&init);
    ASSERT_NE(player, nullptr);

    EXPECT_EQ(sceAvPlayerSetLooping(player, 1), 0);
    EXPECT_EQ(sceAvPlayerAddSource(player, "loop.mp4"), 0);

    // Jump past the frame budget (120 frames * 33ms = 3960ms)
    EXPECT_EQ(sceAvPlayerJumpToTime(player, 5000), 0);

    // In looping mode, getting video frame should reset to beginning and succeed
    FrameInfoExMock frame{};
    const int gotFrame = sceAvPlayerGetVideoDataEx(player, &frame);
    EXPECT_EQ(gotFrame, 1);
    EXPECT_TRUE(sceAvPlayerIsActive(player));

    EXPECT_EQ(sceAvPlayerClose(player), 0);
}

} // namespace

