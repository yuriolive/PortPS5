// Shared native and non-native save-dialog regressions for scan filtering and
// common-dialog ownership. Each process uses a private synthetic save root;
// dialog state is reset before and after each test and no guest data is used.
#include "common/TestHarness.hpp"
#include "SceTypes.hpp"
#include "prx/libSceSaveData.native/SaveData.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {
int APS5_VABI sceSaveDataDialogInitialize() noexcept;
int APS5_VABI sceSaveDataDialogTerminate() noexcept;
int APS5_VABI sceSaveDataDialogOpen(const void*) noexcept;
int APS5_VABI sceSaveDataDialogClose(const void*) noexcept;
int APS5_VABI sceSaveDataDialogGetResult(void*) noexcept;
#ifndef SAVE_DATA_DIALOG_NON_NATIVE
int APS5_VABI sceSaveDataDialogUpdateStatus() noexcept;
void ResetSaveDataDialogStateForTesting();
#endif
}

#ifdef SAVE_DATA_DIALOG_NON_NATIVE
#define DIALOG_TEST_SUITE NonNativeSaveDataDialogScanTest
#else
#define DIALOG_TEST_SUITE NativeSaveDataDialogScanTest
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
/**
 * Sets directory last write time portably.
 * On Windows, std::filesystem::last_write_time on a directory throws
 * Permission denied because MinGW-w64 libstdc++ opens directories without
 * FILE_FLAG_BACKUP_SEMANTICS; Win32 SetFileTime works as documented.
 */
static void SetDirectoryWriteTime(const std::filesystem::path& path,
                                  std::filesystem::file_time_type time) {
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        auto sys_time = std::chrono::file_clock::to_sys(time);
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(sys_time.time_since_epoch()).count();
        ULARGE_INTEGER ull;
        ull.QuadPart = (static_cast<ULONGLONG>(millis) * 10000ULL) + 116444736000000000ULL;
        FILETIME ft;
        ft.dwLowDateTime = ull.LowPart;
        ft.dwHighDateTime = ull.HighPart;
        SetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
        return;
    }
#endif
    std::error_code ec;
    std::filesystem::last_write_time(path, time, ec);
}

/** Hermetic dialog fixture; the test thread is the only dialog-state owner. */
class DIALOG_TEST_SUITE : public PortPS5::Testing::TempDirectoryFixture {
protected:
    /** Start with an initialized dialog and a private save root. */
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        SetSaveDataBaseDirOverride(TempDir());
        ASSERT_EQ(sceSaveDataDialogTerminate(), SAVE_DATA_DIALOG_OK);
        ASSERT_EQ(sceSaveDataDialogInitialize(), SAVE_DATA_DIALOG_OK);
    }

    /** Release this dialog's state and any synthetic peer registration. */
    void TearDown() override {
        sceSaveDataDialogTerminate();
        if (peerActive) RegisterCommonDialogActive_nid_no_patch(false);
        SetSaveDataBaseDirOverride({});
        TempDirectoryFixture::TearDown();
    }

    /** Simulate an independently running dialog sharing the common counter. */
    void StartPeer() {
        RegisterCommonDialogActive_nid_no_patch(true);
        peerActive = true;
    }

    /** Both variants must expose the same list result after completion. */
    void ExpectSelected(const char* expected) {
        SaveDataDialogParam param{};
        param.mode = 8;
        ASSERT_EQ(sceSaveDataDialogOpen(&param), SAVE_DATA_DIALOG_OK);
#ifndef SAVE_DATA_DIALOG_NON_NATIVE
        EXPECT_EQ(sceSaveDataDialogUpdateStatus(), SAVE_DATA_DIALOG_STATUS_FINISHED);
#endif
        SaveDataDirName name{};
        SaveDataDialogResult result{};
        result.dir_name = &name;
        ASSERT_EQ(sceSaveDataDialogGetResult(&result), SAVE_DATA_DIALOG_OK);
        if (expected) {
            EXPECT_EQ(result.result, SAVE_DATA_DIALOG_RESULT_OK);
            EXPECT_STREQ(name.data, expected);
        } else {
            EXPECT_NE(result.result, SAVE_DATA_DIALOG_RESULT_OK);
            EXPECT_STREQ(name.data, "");
        }
    }

    bool peerActive = false;
};

// Even newer snapshots, staging copies, and memory directories cannot win a
// list selection; a name containing (but not ending in) the suffix is valid.
TEST_F(DIALOG_TEST_SUITE, IgnoresSnapshotsAndInternalDirectories) {
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const char* name : {"a.portps5-prev", "long-save.portps5-prev", ".portps5-prev",
                             ".save.portps5-prev.tmp", "_memory"}) {
        std::filesystem::create_directory(TempDir() / name);
        SetDirectoryWriteTime(TempDir() / name, now);
    }
    ExpectSelected(nullptr);
    std::filesystem::create_directory(TempDir() / "a.portps5-prev-extra");
    SetDirectoryWriteTime(TempDir() / "a.portps5-prev-extra", now - std::chrono::hours(1));
    ExpectSelected("a.portps5-prev-extra");
}

// Entry status errors from dangling symlinks must not cancel a valid scan.
TEST_F(DIALOG_TEST_SUITE, SkipsEntryErrors) {
    std::error_code ec;
    std::filesystem::create_symlink(TempDir() / "missing", TempDir() / "broken", ec);
    if (ec) GTEST_SKIP() << "Symlink creation unavailable: " << ec.message();
    std::filesystem::create_directory(TempDir() / "SAVE");
    ExpectSelected("SAVE");
}

// Initialize never registers an active dialog, so terminate must leave a peer alone.
TEST_F(DIALOG_TEST_SUITE, InitializedTerminatePreservesPeer) {
    StartPeer();
    EXPECT_EQ(sceSaveDataDialogTerminate(), SAVE_DATA_DIALOG_OK);
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
}

#ifndef SAVE_DATA_DIALOG_NON_NATIVE
// The testing reset obeys the same registration ownership as terminate.
TEST_F(DIALOG_TEST_SUITE, InitializedResetPreservesPeer) {
    StartPeer();
    ResetSaveDataDialogStateForTesting();
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
}

// Running native dialogs do own one registration and must release it on teardown.
TEST_F(DIALOG_TEST_SUITE, RunningTerminateAndResetReleaseRegistration) {
    SaveDataDialogParam param{};
    param.mode = 1;
    ASSERT_EQ(sceSaveDataDialogOpen(&param), SAVE_DATA_DIALOG_OK);
    ASSERT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
    EXPECT_EQ(sceSaveDataDialogTerminate(), SAVE_DATA_DIALOG_OK);
    EXPECT_FALSE(IsAnyCommonDialogActive_nid_no_patch());
    ASSERT_EQ(sceSaveDataDialogInitialize(), SAVE_DATA_DIALOG_OK);
    ASSERT_EQ(sceSaveDataDialogOpen(&param), SAVE_DATA_DIALOG_OK);
    ResetSaveDataDialogStateForTesting();
    EXPECT_FALSE(IsAnyCommonDialogActive_nid_no_patch());
}
#else
// The synchronous non-native dialog never registers, including during Open.
TEST_F(DIALOG_TEST_SUITE, ClosePreservesPeer) {
    StartPeer();
    SaveDataDialogParam param{};
    param.mode = 1;
    ASSERT_EQ(sceSaveDataDialogOpen(&param), SAVE_DATA_DIALOG_OK);
    EXPECT_EQ(sceSaveDataDialogClose(nullptr), SAVE_DATA_DIALOG_OK);
    EXPECT_TRUE(IsAnyCommonDialogActive_nid_no_patch());
}
#endif
} // namespace
