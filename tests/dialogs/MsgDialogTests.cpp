// tests/dialogs/MsgDialogTests.cpp
// GoogleTest suite for the headless libSceMsgDialog state machine: every status transition, every
// SCE_COMMON_DIALOG error code the library returns, the 44-byte SceMsgDialogResult, and the
// auto-answer policy per button type. Parameter blocks are built from synthetic structs that
// repeat the guest layout (shadPS4 msgdialog_ui.h, GPL-2.0-or-later, as behaviour oracle); no
// game data or SDK header is involved. The library is process-global, so every test starts from a
// terminated library through the fixture.

#include "common/TestHarness.hpp"
#include "prx/libc/include/General.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
int APS5_VABI sceMsgDialogInitialize(void) noexcept;
int APS5_VABI sceMsgDialogTerminate(void) noexcept;
int APS5_VABI sceMsgDialogOpen(const void* param) noexcept;
int APS5_VABI sceMsgDialogGetStatus(void) noexcept;
int APS5_VABI sceMsgDialogUpdateStatus(void) noexcept;
int APS5_VABI sceMsgDialogGetResult(void* result) noexcept;
int APS5_VABI sceMsgDialogClose(void) noexcept;
int APS5_VABI sceMsgDialogProgressBarInc(int target, std::uint32_t delta) noexcept;
int APS5_VABI sceMsgDialogProgressBarSetMsg(int target, const char* msg) noexcept;
int APS5_VABI sceMsgDialogProgressBarSetValue(int target, std::uint32_t rate) noexcept;
}

namespace {

constexpr int kNotInitialized = static_cast<int>(0x80B80003u);
constexpr int kAlreadyInitialized = static_cast<int>(0x80B80004u);
constexpr int kNotFinished = static_cast<int>(0x80B80005u);
constexpr int kInvalidState = static_cast<int>(0x80B80006u);
constexpr int kParamInvalid = static_cast<int>(0x80B8000Au);
constexpr int kNotRunning = static_cast<int>(0x80B8000Bu);
constexpr int kArgNull = static_cast<int>(0x80B8000Du);
constexpr int kNotSupported = static_cast<int>(0x80B8000Fu);

constexpr int kNone = 0;
constexpr int kInitialized = 1;
constexpr int kRunning = 2;
constexpr int kFinished = 3;

// Guest layouts, repeated here on purpose so a layout change in the library is caught.
struct UserMessage {
    std::uint32_t buttonType;
    std::int32_t pad;
    std::uint64_t msg;
    std::uint64_t buttonsParam;
    std::uint8_t reserved[24];
};
struct Param {
    std::uint8_t base[0x30];
    std::uint64_t size;
    std::int32_t mode;
    std::int32_t pad;
    std::uint64_t userMsg;
    std::uint64_t progressBar;
    std::uint64_t systemMsg;
    std::int32_t userId;
    std::uint8_t reserved[44];
};
struct Result {
    std::int32_t mode;
    std::int32_t result;
    std::int32_t buttonId;
    std::uint8_t reserved[32];
};
static_assert(sizeof(Param) == 0x88 && sizeof(UserMessage) == 0x30 && sizeof(Result) == 44);

/** Resets the process-global library before and after each test. */
class MsgDialogTest : public ::testing::Test {
protected:
    void SetUp() override { sceMsgDialogTerminate(); }
    void TearDown() override { sceMsgDialogTerminate(); }

    /** Builds a user-message parameter block with the given button type. */
    Param UserParam(std::uint32_t buttonType) {
        user_ = {};
        user_.buttonType = buttonType;
        user_.msg = reinterpret_cast<std::uintptr_t>("synthetic message");
        Param p{};
        p.size = sizeof(Param);
        p.mode = 1;
        p.userMsg = reinterpret_cast<std::uintptr_t>(&user_);
        return p;
    }

    /** Polls until the status stops being RUNNING, at most `limit` polls. Returns the last status. */
    static int PollToEnd(int limit = 8) {
        int status = kRunning;
        for (int i = 0; i < limit && status == kRunning; ++i) {
            status = sceMsgDialogUpdateStatus();
        }
        return status;
    }

    /** Opens `p`, runs it to FINISHED and returns the result block. */
    Result RunToResult(const Param& p) {
        EXPECT_EQ(sceMsgDialogInitialize(), 0);
        EXPECT_EQ(sceMsgDialogOpen(&p), 0);
        EXPECT_EQ(PollToEnd(), kFinished);
        Result r;
        std::memset(&r, 0xAA, sizeof(r));
        EXPECT_EQ(sceMsgDialogGetResult(&r), 0);
        return r;
    }

    UserMessage user_{};
};

// Before sceMsgDialogInitialize every stateful call reports NOT_INITIALIZED and the status is NONE;
// a second Initialize reports ALREADY_INITIALIZED without disturbing the INITIALIZED status.
TEST_F(MsgDialogTest, LifecycleErrorsBeforeAndAfterInitialize) {
    Param p = UserParam(0);
    EXPECT_EQ(sceMsgDialogGetStatus(), kNone);
    EXPECT_EQ(sceMsgDialogOpen(&p), kNotInitialized);
    EXPECT_EQ(sceMsgDialogClose(), kNotInitialized);
    Result r{};
    EXPECT_EQ(sceMsgDialogGetResult(&r), kNotInitialized);
    EXPECT_EQ(sceMsgDialogTerminate(), kNotInitialized);

    EXPECT_EQ(sceMsgDialogInitialize(), 0);
    EXPECT_EQ(sceMsgDialogGetStatus(), kInitialized);
    EXPECT_EQ(sceMsgDialogInitialize(), kAlreadyInitialized);
    EXPECT_EQ(sceMsgDialogGetStatus(), kInitialized);
    EXPECT_EQ(sceMsgDialogTerminate(), 0);
    EXPECT_EQ(sceMsgDialogGetStatus(), kNone);
}

// Open validates its argument: null is ARG_NULL, an unknown mode or a missing sub-structure is
// PARAM_INVALID, and none of them leave the library in RUNNING.
TEST_F(MsgDialogTest, OpenRejectsBadParameters) {
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    EXPECT_EQ(sceMsgDialogOpen(nullptr), kArgNull);

    Param bad = UserParam(0);
    bad.mode = 0;
    EXPECT_EQ(sceMsgDialogOpen(&bad), kParamInvalid);
    bad.mode = 4;
    EXPECT_EQ(sceMsgDialogOpen(&bad), kParamInvalid);

    Param noUser = UserParam(0);
    noUser.userMsg = 0;
    EXPECT_EQ(sceMsgDialogOpen(&noUser), kParamInvalid);

    Param twoButtons = UserParam(9);
    twoButtons.size = sizeof(Param);
    user_.buttonsParam = 0;
    EXPECT_EQ(sceMsgDialogOpen(&twoButtons), kParamInvalid);
    EXPECT_EQ(sceMsgDialogGetStatus(), kInitialized);
}

#ifdef _WIN32
// A pointer outside committed memory must be rejected instead of dereferenced (guest pointers are
// untrusted). Windows only: the portable fallback of the range check only rejects null.
TEST_F(MsgDialogTest, UnmappedPointersAreRejected) {
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    EXPECT_EQ(sceMsgDialogOpen(reinterpret_cast<const void*>(0x10)), kParamInvalid);

    Param p = UserParam(0);
    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    ASSERT_EQ(PollToEnd(), kFinished);
    EXPECT_EQ(sceMsgDialogGetResult(reinterpret_cast<void*>(0x10)), kParamInvalid);
}
#endif

// An OK dialog stays RUNNING for the first poll (a title observes RUNNING), finishes on the second,
// and the 44-byte result carries mode 1, result OK and button 1 with zeroed reserved bytes.
TEST_F(MsgDialogTest, OkDialogAutoAnswersAfterTwoPolls) {
    Param p = UserParam(0);
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    EXPECT_EQ(sceMsgDialogOpen(&p), kInvalidState);

    Result r;
    std::memset(&r, 0xAA, sizeof(r));
    EXPECT_EQ(sceMsgDialogGetResult(&r), kNotFinished);
    EXPECT_EQ(sceMsgDialogGetStatus(), kRunning);
    EXPECT_EQ(sceMsgDialogUpdateStatus(), kFinished);
    EXPECT_EQ(sceMsgDialogGetStatus(), kFinished);

    // Buffer one byte longer than the result: the library must not write past 44 bytes.
    std::array<std::uint8_t, sizeof(Result) + 1> raw;
    raw.fill(0xAA);
    ASSERT_EQ(sceMsgDialogGetResult(raw.data()), 0);
    Result out;
    std::memcpy(&out, raw.data(), sizeof(out));
    EXPECT_EQ(out.mode, 1);
    EXPECT_EQ(out.result, 0);
    EXPECT_EQ(out.buttonId, 1);
    for (std::uint8_t b : out.reserved) {
        EXPECT_EQ(b, 0);
    }
    EXPECT_EQ(raw.back(), 0xAA);
}

// A title that only calls GetStatus (never UpdateStatus) must still see the dialog finish, or it
// would wait forever on a dialog nobody can answer.
TEST_F(MsgDialogTest, GetStatusOnlyPollerReachesFinished) {
    Param p = UserParam(0);
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    int status = kRunning;
    for (int i = 0; i < 8 && status == kRunning; ++i) {
        status = sceMsgDialogGetStatus();
    }
    EXPECT_EQ(status, kFinished);
}

// The auto-answer follows the focused button: focus-No answers No, focus-Cancel reports a user
// cancel on button 2, plain OK/YES-NO/OK-CANCEL answer the affirmative button 1. Button type 4 is
// unassigned and must behave as the default (an upstream port treated 4 as focus-No).
TEST_F(MsgDialogTest, AutoAnswerFollowsFocusedButton) {
    struct Case {
        std::uint32_t buttonType;
        std::int32_t result;
        std::int32_t buttonId;
    };
    const Case cases[] = {
        {0, 0, 1},  // OK
        {1, 0, 1},  // YES/NO
        {3, 0, 1},  // OK/CANCEL
        {4, 0, 1},  // unassigned: default answer, never focus-No
        {7, 0, 2},  // YES/NO, focus on No
        {8, 1, 2},  // OK/CANCEL, focus on Cancel: user canceled
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.buttonType);
        sceMsgDialogTerminate();
        Param p = UserParam(c.buttonType);
        const Result r = RunToResult(p);
        EXPECT_EQ(r.result, c.result);
        EXPECT_EQ(r.buttonId, c.buttonId);
    }
}

// A two-button dialog with a readable buttons block auto-answers the first button.
TEST_F(MsgDialogTest, TwoButtonsAnswersFirstButton) {
    std::array<std::uint64_t, 6> buttons{};
    Param p = UserParam(9);
    user_.buttonsParam = reinterpret_cast<std::uintptr_t>(buttons.data());
    const Result r = RunToResult(p);
    EXPECT_EQ(r.result, 0);
    EXPECT_EQ(r.buttonId, 1);
}

// Dialogs without a pressable button stay RUNNING however often they are polled; only the title's
// sceMsgDialogClose ends them, with no button in the result.
TEST_F(MsgDialogTest, ButtonlessDialogsWaitForClose) {
    for (std::uint32_t type : {2u, 5u, 6u}) {
        SCOPED_TRACE(type);
        sceMsgDialogTerminate();
        Param p = UserParam(type);
        ASSERT_EQ(sceMsgDialogInitialize(), 0);
        ASSERT_EQ(sceMsgDialogOpen(&p), 0);
        for (int i = 0; i < 20; ++i) {
            ASSERT_EQ(sceMsgDialogUpdateStatus(), kRunning);
            ASSERT_EQ(sceMsgDialogGetStatus(), kRunning);
        }
        EXPECT_EQ(sceMsgDialogClose(), 0);
        EXPECT_EQ(sceMsgDialogGetStatus(), kFinished);
        Result r;
        ASSERT_EQ(sceMsgDialogGetResult(&r), 0);
        EXPECT_EQ(r.buttonId, 0);
        EXPECT_EQ(r.result, 0);
    }
}

// Progress-bar mode: the three bar calls succeed for target DEFAULT while running, reject another
// target with PARAM_INVALID, report NOT_RUNNING outside a dialog and NOT_SUPPORTED in another
// mode; the dialog runs until Close, and the result reports mode 2.
TEST_F(MsgDialogTest, ProgressBarLifecycleAndErrors) {
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    EXPECT_EQ(sceMsgDialogProgressBarInc(0, 10), kNotRunning);
    EXPECT_EQ(sceMsgDialogProgressBarSetMsg(0, "x"), kNotRunning);
    EXPECT_EQ(sceMsgDialogProgressBarSetValue(0, 50), kNotRunning);

    Param user = UserParam(2);
    ASSERT_EQ(sceMsgDialogOpen(&user), 0);
    EXPECT_EQ(sceMsgDialogProgressBarInc(0, 10), kNotSupported);
    EXPECT_EQ(sceMsgDialogProgressBarSetMsg(0, "x"), kNotSupported);
    EXPECT_EQ(sceMsgDialogProgressBarSetValue(0, 50), kNotSupported);
    ASSERT_EQ(sceMsgDialogClose(), 0);

    Param bar{};
    bar.size = sizeof(Param);
    bar.mode = 2;
    std::array<std::uint64_t, 10> barParam{};  // Opaque to the library: only the mode matters.
    bar.progressBar = reinterpret_cast<std::uintptr_t>(barParam.data());
    ASSERT_EQ(sceMsgDialogOpen(&bar), 0);
    for (int i = 0; i < 20; ++i) {
        ASSERT_EQ(sceMsgDialogUpdateStatus(), kRunning);
    }
    EXPECT_EQ(sceMsgDialogProgressBarInc(0, 10), 0);
    EXPECT_EQ(sceMsgDialogProgressBarSetMsg(0, "working"), 0);
    EXPECT_EQ(sceMsgDialogProgressBarSetValue(0, 50), 0);
    EXPECT_EQ(sceMsgDialogProgressBarInc(1, 10), kParamInvalid);
    EXPECT_EQ(sceMsgDialogProgressBarSetMsg(1, "x"), kParamInvalid);
    EXPECT_EQ(sceMsgDialogProgressBarSetValue(1, 50), kParamInvalid);
    EXPECT_EQ(sceMsgDialogClose(), 0);
    Result r;
    ASSERT_EQ(sceMsgDialogGetResult(&r), 0);
    EXPECT_EQ(r.mode, 2);
    EXPECT_EQ(r.buttonId, 0);
    EXPECT_EQ(sceMsgDialogProgressBarInc(0, 10), kNotRunning);
}

// A system-message dialog (mode 3) has a single OK button and auto-answers it.
TEST_F(MsgDialogTest, SystemMessageAutoAnswersOk) {
    Param p{};
    p.size = sizeof(Param);
    p.mode = 3;
    std::array<std::uint64_t, 5> system{};
    p.systemMsg = reinterpret_cast<std::uintptr_t>(system.data());
    const Result r = RunToResult(p);
    EXPECT_EQ(r.mode, 3);
    EXPECT_EQ(r.result, 0);
    EXPECT_EQ(r.buttonId, 1);
}

// Close needs a running dialog (NOT_RUNNING otherwise, including after FINISHED), a finished
// dialog can be replaced by a new Open, and Terminate closes a running dialog and returns to NONE.
TEST_F(MsgDialogTest, CloseReopenAndTerminateWhileRunning) {
    Param p = UserParam(0);
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    EXPECT_EQ(sceMsgDialogClose(), kNotRunning);
    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    ASSERT_EQ(PollToEnd(), kFinished);
    EXPECT_EQ(sceMsgDialogClose(), kNotRunning);
    Result r;
    EXPECT_EQ(sceMsgDialogGetResult(nullptr), kArgNull);

    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    EXPECT_EQ(sceMsgDialogGetResult(&r), kNotFinished);
    EXPECT_EQ(sceMsgDialogTerminate(), 0);
    EXPECT_EQ(sceMsgDialogGetStatus(), kNone);
    EXPECT_EQ(sceMsgDialogGetResult(&r), kNotInitialized);
}

// While a dialog runs the common-dialog "in use" flag is set, and it clears on every way out:
// auto-answer, Close and Terminate.
TEST_F(MsgDialogTest, CommonDialogActiveFlagTracksRunningDialog) {
    Param p = UserParam(2);
    ASSERT_EQ(sceMsgDialogInitialize(), 0);
    ASSERT_FALSE(IsAnyCommonDialogActive_nid_no_patch());
    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
    ASSERT_EQ(sceMsgDialogClose(), 0);
    EXPECT_FALSE(IsAnyCommonDialogActive_nid_no_patch());

    Param ok = UserParam(0);
    ASSERT_EQ(sceMsgDialogOpen(&ok), 0);
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
    ASSERT_EQ(PollToEnd(), kFinished);
    EXPECT_FALSE(IsAnyCommonDialogActive_nid_no_patch());

    ASSERT_EQ(sceMsgDialogOpen(&p), 0);
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
    ASSERT_EQ(sceMsgDialogTerminate(), 0);
    EXPECT_FALSE(IsAnyCommonDialogActive_nid_no_patch());
}

}  // namespace
