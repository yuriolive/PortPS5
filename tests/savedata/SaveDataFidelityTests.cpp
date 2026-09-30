// tests/savedata/SaveDataFidelityTests.cpp
// GoogleTest verification suite for PortPS5 M2 Save Data Fidelity slice.
// Verifies per-title layout, crash safety (RDWR snapshots, startup recovery, atomic swaps),
// mount modes, error codes, memory blobs + quota, pattern matching (% and _), param/icon round-trip,
// scripted dialog transitions, common dialog active tracking, and death tests for corrupt mount modes.

#include "common/TestHarness.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceSaveData.native/SaveData.hpp"
#include "prx/libSceSaveDataDialog.native/SaveDataDialog.hpp"
#include "prx/libSceCommonDialog/CommonDialog.hpp"
#include "SceTypes.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern "C" {
int APS5_VABI sceSaveDataInitialize3(const void* init) noexcept;
int APS5_VABI sceSaveDataTerminate(void) noexcept;
int APS5_VABI sceSaveDataMount3(const SaveDataMount3* mount, SaveDataMountResult* mount_result) noexcept;
int APS5_VABI sceSaveDataUmount2(uint32_t mode, const SaveDataMountPoint* mount_point) noexcept;
int APS5_VABI sceSaveDataGetMountInfo(const SaveDataMountPoint* mount_point, SaveDataMountInfo* info) noexcept;
int APS5_VABI sceSaveDataSetParam(const SaveDataMountPoint* mount_point, uint32_t param_type, const void* param_buf, size_t param_buf_size) noexcept;
int APS5_VABI sceSaveDataGetParam(const SaveDataMountPoint* mount_point, uint32_t param_type, void* param_buf, size_t param_buf_size, size_t* got_size) noexcept;
int APS5_VABI sceSaveDataSaveIcon(const SaveDataMountPoint* mount_point, const SaveDataIcon* icon) noexcept;
int APS5_VABI sceSaveDataGetMountInfo(const SaveDataMountPoint*, SaveDataMountInfo*) noexcept;
int APS5_VABI sceSaveDataSaveIconByPath(const SaveDataMountPoint*, const char*) noexcept;
int APS5_VABI sceSaveDataLoadIcon(const SaveDataMountPoint* mount_point, SaveDataIcon* icon) noexcept;
int APS5_VABI sceSaveDataDelete(const SaveDataDelete* del) noexcept;
int APS5_VABI sceSaveDataDirNameSearch(const SaveDataDirNameSearchCond* cond, SaveDataDirNameSearchResult* result) noexcept;
int APS5_VABI sceSaveDataSetupSaveDataMemory2(const SaveDataMemorySetup2* setup_param, SaveDataMemorySetupResult* result) noexcept;
int APS5_VABI sceSaveDataSetSaveDataMemory2(const SaveDataMemorySet2* set_param) noexcept;
int APS5_VABI sceSaveDataGetSaveDataMemory2(SaveDataMemoryGet2* get_param) noexcept;
int APS5_VABI sceSaveDataTransferringMount(const SaveDataTransferringMount* mount, SaveDataMountResult* mount_result) noexcept;

int APS5_VABI sceSaveDataDialogInitialize(void) noexcept;
int APS5_VABI sceSaveDataDialogTerminate(void) noexcept;
int APS5_VABI sceSaveDataDialogOpen(const void* param) noexcept;
int APS5_VABI sceSaveDataDialogClose(const void* close_param) noexcept;
int APS5_VABI sceSaveDataDialogGetStatus(void) noexcept;
int APS5_VABI sceSaveDataDialogUpdateStatus(void) noexcept;
int APS5_VABI sceSaveDataDialogGetResult(void* result) noexcept;
int APS5_VABI sceSaveDataDialogIsReadyToDisplay(void) noexcept;

int APS5_VABI sceCommonDialogInitialize(void) noexcept;
bool APS5_VABI sceCommonDialogIsUsed(void) noexcept;

void ResetSaveDataStateForTesting();
void ResetSaveDataDialogStateForTesting();
}

namespace {

using namespace PortPS5::Testing;

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

class SaveDataFidelityTest : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        SetSaveDataBaseDirOverride(TempDir());
        ResetSaveDataStateForTesting();
        ResetSaveDataDialogStateForTesting();
    }

    void TearDown() override {
        ResetSaveDataStateForTesting();
        ResetSaveDataDialogStateForTesting();
        SetSaveDataBaseDirOverride(std::filesystem::path{});
        TempDirectoryFixture::TearDown();
    }

    static SceSaveDataDirName MakeDirName(const char* name) {
        SceSaveDataDirName d{};
        std::strncpy(d.data, name, sizeof(d.data) - 1);
        return d;
    }
};

// Mount modes preserve exists, missing, busy, and double-unmount error semantics.
TEST_F(SaveDataFidelityTest, MountModesAndErrorCodes) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    // Double initialization rejected
    EXPECT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_ERROR_ALREADY_INITIALIZED);

    SceSaveDataDirName dirName = MakeDirName("SAVEDIR01");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE;
    mount.blocks = 100;

    SaveDataMountResult result{};
    // First CREATE mount succeeds
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);
    EXPECT_STREQ(result.mount_point.data, "/_sm/0");

    // Attempting to mount an already mounted directory fails with BUSY
    SaveDataMountResult resultBusy{};
    EXPECT_EQ(sceSaveDataMount3(&mount, &resultBusy), SAVE_DATA_ERROR_BUSY);

    // RDONLY mount on nonexistent save returns NOT_FOUND
    SceSaveDataDirName nonExistentDir = MakeDirName("NONEXISTENT");
    SaveDataMount3 mountNonExistent{};
    mountNonExistent.dir_name = &nonExistentDir;
    mountNonExistent.mount_mode = SAVE_DATA_MOUNT_MODE_RDONLY;
    EXPECT_EQ(sceSaveDataMount3(&mountNonExistent, &resultBusy), SAVE_DATA_ERROR_NOT_FOUND);

    // RDWR mount on nonexistent save returns NOT_FOUND
    mountNonExistent.mount_mode = SAVE_DATA_MOUNT_MODE_RDWR;
    EXPECT_EQ(sceSaveDataMount3(&mountNonExistent, &resultBusy), SAVE_DATA_ERROR_NOT_FOUND);

    // Unmount existing slot
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);

    // Now that slot is unmounted, CREATE on already existing save directory fails with EXISTS
    SaveDataMountResult resultExists{};
    EXPECT_EQ(sceSaveDataMount3(&mount, &resultExists), SAVE_DATA_ERROR_EXISTS);

    // Unmounting non-mounted mount point returns NOT_MOUNTED
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_ERROR_NOT_MOUNTED);

    // CREATE2 succeeds on existing directory
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);

    // RDONLY succeeds on existing directory
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_RDONLY;
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);

    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// A clean unmount/remount must preserve synthetic save payload bytes.
TEST_F(SaveDataFidelityTest, WriteUnmountRemountReadConsistency) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    SceSaveDataDirName dirName = MakeDirName("CONSISTENCY_DIR");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    mount.blocks = 100;

    SaveDataMountResult result{};
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    // Write file into the mounted directory
    std::filesystem::path savePath = TempDir() / "CONSISTENCY_DIR";
    std::filesystem::path testFile = savePath / "game_state.bin";
    std::string testData = "PortPS5 Save Consistency Test Payload 1234567890";
    {
        std::ofstream out(testFile, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out.write(testData.data(), testData.size());
    }

    // Clean unmount
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);

    // Remount RDONLY
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_RDONLY;
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    // Read back and verify
    std::string readBack;
    {
        std::ifstream in(testFile, std::ios::binary);
        ASSERT_TRUE(in.is_open());
        readBack.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(readBack, testData);

    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Restart after an unclean RDWR session restores the complete previous save.
TEST_F(SaveDataFidelityTest, CrashSafetySnapshotRestoreOnCrash) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    // 1. Establish pristine save state
    SceSaveDataDirName dirName = MakeDirName("CRASH_TEST_DIR");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    mount.blocks = 100;

    SaveDataMountResult result{};
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    std::filesystem::path savePath = TempDir() / "CRASH_TEST_DIR";
    std::filesystem::path pristineFile = savePath / "save.dat";
    std::string pristineContent = "PRISTINE_DATA_V1";
    {
        std::ofstream out(pristineFile, std::ios::binary);
        out.write(pristineContent.data(), pristineContent.size());
    }
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);

    // 2. Open RDWR mount to simulate mid-write crash
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_RDWR;
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    // A snapshot directory .portps5-prev should now exist
    std::filesystem::path snapshotPath = TempDir() / "CRASH_TEST_DIR.portps5-prev";
    EXPECT_TRUE(std::filesystem::exists(snapshotPath));

    // Game corrupts the active save file and adds a partial temp file
    {
        std::ofstream out(pristineFile, std::ios::binary);
        std::string corruptedContent = "CORRUPTED_INCOMPLETE_WRITE";
        out.write(corruptedContent.data(), corruptedContent.size());
    }
    std::filesystem::path partialFile = savePath / "partial.tmp";
    {
        std::ofstream out(partialFile, std::ios::binary);
        out << "garbage";
    }

    // 3. Simulate sudden crash/kill: no clean Umount2, state reset
    ResetSaveDataStateForTesting();

    // 4. Relaunch/initialize: startup scan detects leftover snapshot and restores pristine state
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    // Verify snapshot directory is consumed/removed
    EXPECT_FALSE(std::filesystem::exists(snapshotPath));

    // Verify pristine file is restored and partial corrupted files are gone
    std::string restoredContent;
    {
        std::ifstream in(pristineFile, std::ios::binary);
        ASSERT_TRUE(in.is_open());
        restoredContent.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(restoredContent, pristineContent);
    EXPECT_FALSE(std::filesystem::exists(partialFile));

    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Clean RDWR unmount consumes the rollback snapshot.
TEST_F(SaveDataFidelityTest, CrashSafetyCleanUnmountRemovesSnapshot) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    // Establish existing save directory first so that RDWR mount creates a snapshot
    std::filesystem::path savePath = TempDir() / "CLEAN_UNMOUNT_DIR";
    std::error_code ec;
    std::filesystem::create_directories(savePath, ec);
    {
        std::ofstream out(savePath / "state.dat");
        out << "sample data";
    }

    SceSaveDataDirName dirName = MakeDirName("CLEAN_UNMOUNT_DIR");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_RDWR;
    mount.blocks = 100;

    SaveDataMountResult result{};
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    std::filesystem::path snapshotPath = TempDir() / "CLEAN_UNMOUNT_DIR.portps5-prev";
    EXPECT_TRUE(std::filesystem::exists(snapshotPath));

    // Clean unmount must delete snapshot
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);
    EXPECT_FALSE(std::filesystem::exists(snapshotPath));

    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Memory saves enforce quota and round-trip bytes through atomic replacement.
TEST_F(SaveDataFidelityTest, MemoryBlobAtomicSwapAndQuota) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    // Quota test: setup exceeding maximum blocks rejected with NO_SPACE
    SaveDataMemorySetup2 hugeSetup{};
    hugeSetup.slot_id = 1;
    hugeSetup.memory_size = (SAVE_DATA_BLOCKS_MAX + 1) * SAVE_DATA_BLOCK_SIZE;
    SaveDataMemorySetupResult setupResult{};
    EXPECT_EQ(sceSaveDataSetupSaveDataMemory2(&hugeSetup, &setupResult), SAVE_DATA_ERROR_NO_SPACE);

    // Valid setup
    SaveDataMemorySetup2 validSetup{};
    validSetup.slot_id = 1;
    validSetup.memory_size = 1024;
    EXPECT_EQ(sceSaveDataSetupSaveDataMemory2(&validSetup, &setupResult), SAVE_DATA_OK);

    // Set memory payload
    std::string blobData = "PORTPS5_MEMORY_BLOB_TEST_DATA_987654321";
    SaveDataMemoryData memData{};
    memData.buf = blobData.data();
    memData.buf_size = blobData.size();
    memData.offset = 0;

    SaveDataMemorySet2 setParam{};
    setParam.slot_id = 1;
    setParam.data_num = 1;
    setParam.data = &memData;
    EXPECT_EQ(sceSaveDataSetSaveDataMemory2(&setParam), SAVE_DATA_OK);

    // Read back memory payload
    std::vector<char> readBuf(blobData.size(), 0);
    SaveDataMemoryData getMemData{};
    getMemData.buf = readBuf.data();
    getMemData.buf_size = readBuf.size();
    getMemData.offset = 0;

    SaveDataMemoryGet2 getParam{};
    getParam.slot_id = 1;
    getParam.data = &getMemData;
    EXPECT_EQ(sceSaveDataGetSaveDataMemory2(&getParam), SAVE_DATA_OK);

    EXPECT_EQ(std::string(readBuf.data(), readBuf.size()), blobData);

    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Directory search honors percent and underscore wildcards and exact names.
TEST_F(SaveDataFidelityTest, DirNameSearchPatternMatching) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    // Populate several save directories
    std::error_code ec;
    std::filesystem::create_directories(TempDir() / "SAVE0001", ec);
    std::filesystem::create_directories(TempDir() / "SAVE0002", ec);
    std::filesystem::create_directories(TempDir() / "AUTOSAVE", ec);
    std::filesystem::create_directories(TempDir() / "CONFIG", ec);

    // Search with "SAVE%" pattern
    SceSaveDataDirName pattern = MakeDirName("SAVE%");
    SaveDataDirNameSearchCond cond{};
    cond.dir_name = &pattern;

    SceSaveDataDirName matchedDirs[10]{};
    SaveDataDirNameSearchResult searchResult{};
    searchResult.dir_names = matchedDirs;
    searchResult.dir_names_num = 10;

    ASSERT_EQ(sceSaveDataDirNameSearch(&cond, &searchResult), SAVE_DATA_OK);
    EXPECT_EQ(searchResult.hit_num, 2u);

    // Search with single character wildcards "SAVE____"
    pattern = MakeDirName("SAVE____");
    ASSERT_EQ(sceSaveDataDirNameSearch(&cond, &searchResult), SAVE_DATA_OK);
    EXPECT_EQ(searchResult.hit_num, 2u);

    // Search with wildcard "%" matches all 4 directories
    pattern = MakeDirName("%");
    ASSERT_EQ(sceSaveDataDirNameSearch(&cond, &searchResult), SAVE_DATA_OK);
    EXPECT_EQ(searchResult.hit_num, 4u);

    // Exact match
    pattern = MakeDirName("AUTOSAVE");
    ASSERT_EQ(sceSaveDataDirNameSearch(&cond, &searchResult), SAVE_DATA_OK);
    EXPECT_EQ(searchResult.hit_num, 1u);
    EXPECT_STREQ(matchedDirs[0].data, "AUTOSAVE");

    // Non-matching pattern
    pattern = MakeDirName("NONEXISTENT%");
    ASSERT_EQ(sceSaveDataDirNameSearch(&cond, &searchResult), SAVE_DATA_OK);
    EXPECT_EQ(searchResult.hit_num, 0u);

    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Mounted metadata fields and icon bytes round-trip without loss.
TEST_F(SaveDataFidelityTest, ParamAndIconRoundTrip) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    SceSaveDataDirName dirName = MakeDirName("PARAM_ICON_TEST");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    mount.blocks = 100;

    SaveDataMountResult result{};
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);

    // Set parameters
    const char title[] = "Dreaming Sarah Chapter 1";
    const char subTitle[] = "Sub-title text";
    const char detail[] = "Detailed save description at checkpoint";
    uint32_t userParam = 0x12345678;

    EXPECT_EQ(sceSaveDataSetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_TITLE, title, sizeof(title)), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataSetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_SUB_TITLE, subTitle, sizeof(subTitle)), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataSetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_DETAIL, detail, sizeof(detail)), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataSetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_USER_PARAM, &userParam, sizeof(userParam)), SAVE_DATA_OK);

    // Get parameters back and verify
    char outTitle[128]{};
    size_t gotSize = 0;
    EXPECT_EQ(sceSaveDataGetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_TITLE, outTitle, sizeof(outTitle), &gotSize), SAVE_DATA_OK);
    EXPECT_STREQ(outTitle, title);

    char outSubTitle[128]{};
    EXPECT_EQ(sceSaveDataGetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_SUB_TITLE, outSubTitle, sizeof(outSubTitle), &gotSize), SAVE_DATA_OK);
    EXPECT_STREQ(outSubTitle, subTitle);

    char outDetail[1024]{};
    EXPECT_EQ(sceSaveDataGetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_DETAIL, outDetail, sizeof(outDetail), &gotSize), SAVE_DATA_OK);
    EXPECT_STREQ(outDetail, detail);

    uint32_t outUserParam = 0;
    EXPECT_EQ(sceSaveDataGetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_USER_PARAM, &outUserParam, sizeof(outUserParam), &gotSize), SAVE_DATA_OK);
    EXPECT_EQ(outUserParam, userParam);

    // Save and load icon
    const uint8_t fakeIcon[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01, 0x02, 0x03 };
    SaveDataIcon icon{};
    icon.buf = const_cast<uint8_t*>(fakeIcon);
    icon.data_size = sizeof(fakeIcon);
    icon.buf_size = sizeof(fakeIcon);
    EXPECT_EQ(sceSaveDataSaveIcon(&result.mount_point, &icon), SAVE_DATA_OK);

    uint8_t loadedIconBuf[32]{};
    SaveDataIcon loadIcon{};
    loadIcon.buf = loadedIconBuf;
    loadIcon.buf_size = sizeof(loadedIconBuf);
    EXPECT_EQ(sceSaveDataLoadIcon(&result.mount_point, &loadIcon), SAVE_DATA_OK);
    EXPECT_EQ(loadIcon.data_size, sizeof(fakeIcon));
    EXPECT_EQ(std::memcmp(loadedIconBuf, fakeIcon, sizeof(fakeIcon)), 0);

    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);
    EXPECT_EQ(sceSaveDataTerminate(), SAVE_DATA_OK);
}

// Native dialogs expose RUNNING before FINISHED and release active registrations.
TEST_F(SaveDataFidelityTest, DialogStateTransitionsAndCommonDialogUsed) {
    EXPECT_FALSE(sceCommonDialogIsUsed());

    ASSERT_EQ(sceSaveDataDialogInitialize(), SAVE_DATA_DIALOG_OK);
    EXPECT_EQ(sceSaveDataDialogGetStatus(), SAVE_DATA_DIALOG_STATUS_INITIALIZED);

    // 1. Unknown dialog mode rejects with Cancel
    SaveDataDialogParam badParam{};
    badParam.mode = 999;
    ASSERT_EQ(sceSaveDataDialogOpen(&badParam), SAVE_DATA_DIALOG_OK);
    EXPECT_TRUE(sceCommonDialogIsUsed());
    EXPECT_EQ(sceSaveDataDialogGetStatus(), SAVE_DATA_DIALOG_STATUS_RUNNING);

    // UpdateStatus finishes to FINISHED
    EXPECT_EQ(sceSaveDataDialogUpdateStatus(), SAVE_DATA_DIALOG_STATUS_FINISHED);
    SaveDataDialogResult result{};
    EXPECT_EQ(sceSaveDataDialogGetResult(&result), SAVE_DATA_DIALOG_OK);
    EXPECT_NE(result.result, SAVE_DATA_DIALOG_RESULT_OK); // Must not be silent OK

    EXPECT_EQ(sceSaveDataDialogClose(nullptr), SAVE_DATA_DIALOG_OK);
    EXPECT_FALSE(sceCommonDialogIsUsed());

    // 2. List-load with no saves returns Cancel
    SaveDataDialogParam loadParam{};
    loadParam.mode = 5; // Load dialog
    ASSERT_EQ(sceSaveDataDialogOpen(&loadParam), SAVE_DATA_DIALOG_OK);
    EXPECT_TRUE(sceCommonDialogIsUsed());
    EXPECT_EQ(sceSaveDataDialogUpdateStatus(), SAVE_DATA_DIALOG_STATUS_FINISHED);
    EXPECT_EQ(sceSaveDataDialogGetResult(&result), SAVE_DATA_DIALOG_OK);
    EXPECT_NE(result.result, SAVE_DATA_DIALOG_RESULT_OK); // Cancelled because no saves exist
    EXPECT_EQ(sceSaveDataDialogClose(nullptr), SAVE_DATA_DIALOG_OK);

    // 3. Create a save directory and verify list-load selects newest
    std::error_code ec;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::create_directories(TempDir() / "SAVE_OLD", ec);
    SetDirectoryWriteTime(TempDir() / "SAVE_OLD", now - std::chrono::hours(1));
    std::filesystem::create_directories(TempDir() / "SAVE_NEWEST", ec);
    SetDirectoryWriteTime(TempDir() / "SAVE_NEWEST", now);

    SaveDataDirName outDirName{};
    result = SaveDataDialogResult{};
    result.dir_name = &outDirName;

    ASSERT_EQ(sceSaveDataDialogOpen(&loadParam), SAVE_DATA_DIALOG_OK);
    EXPECT_EQ(sceSaveDataDialogUpdateStatus(), SAVE_DATA_DIALOG_STATUS_FINISHED);
    EXPECT_EQ(sceSaveDataDialogGetResult(&result), SAVE_DATA_DIALOG_OK);
    EXPECT_EQ(result.result, SAVE_DATA_DIALOG_RESULT_OK);
    EXPECT_STREQ(outDirName.data, "SAVE_NEWEST");

    EXPECT_EQ(sceSaveDataDialogClose(nullptr), SAVE_DATA_DIALOG_OK);
    EXPECT_FALSE(sceCommonDialogIsUsed());

    EXPECT_EQ(sceSaveDataDialogTerminate(), SAVE_DATA_DIALOG_OK);
}

// Unknown mount bits abort through the documented unsupported-state policy.
TEST_F(SaveDataFidelityTest, DeathTestCorruptMountModeAborts) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    SceSaveDataDirName dirName = MakeDirName("DEATH_TEST");
    SaveDataMount3 mount{};
    mount.dir_name = &dirName;
    mount.mount_mode = 0xFFFF; // Corrupt mount mode

    SaveDataMountResult result{};
    EXPECT_DEATH(sceSaveDataMount3(&mount, &result), ".*");
}

// Unsupported transferring mounts abort instead of throwing across the guest ABI.
TEST_F(SaveDataFidelityTest, DeathTestTransferringMountAborts) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);

    SceSaveDataDirName dirName = MakeDirName("TRANSFER_TEST");
    SaveDataTransferringMount mount{};
    mount.dir_name = &dirName;

    SaveDataMountResult result{};
    EXPECT_DEATH(sceSaveDataTransferringMount(&mount, &result), ".*");
}

// Invalid guest names must not delete the save root or internal memory storage.
TEST_F(SaveDataFidelityTest, DeleteRejectsInvalidNames) {
    std::filesystem::create_directories(TempDir() / "_memory");
    std::ofstream(TempDir() / "_memory" / "sentinel") << "keep";
    SaveDataDelete del{};
    EXPECT_EQ(sceSaveDataDelete(nullptr), SAVE_DATA_ERROR_PARAMETER);
    EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_ERROR_PARAMETER);
    for (const char* name : {"", ".", "..", "_memory", "../outside", "a/b", "a\\b", "C:save", "VALID.portps5-prev"}) {
        auto dir = MakeDirName(name);
        del.dir_name = &dir;
        EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_ERROR_PARAMETER) << name;
        EXPECT_TRUE(std::filesystem::exists(TempDir() / "_memory" / "sentinel"));
    }
    SceSaveDataDirName unterminated{};
    std::memset(unterminated.data, 'x', sizeof(unterminated.data));
    del.dir_name = &unterminated;
    EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_ERROR_PARAMETER);

    // Active mounts cannot be deleted (returns BUSY per spec)
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    auto mountedDir = MakeDirName("MOUNTED_DIR");
    SaveDataMount3 mount{};
    mount.dir_name = &mountedDir;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    SaveDataMountResult mountResult{};
    ASSERT_EQ(sceSaveDataMount3(&mount, &mountResult), SAVE_DATA_OK);
    del.dir_name = &mountedDir;
    EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_ERROR_BUSY);
    EXPECT_EQ(sceSaveDataUmount2(0, &mountResult.mount_point), SAVE_DATA_OK);

    auto valid = MakeDirName("VALID");
    del.dir_name = &valid;
    std::filesystem::create_directories(TempDir() / "VALID");
    std::ofstream(TempDir() / "VALID" / "state") << "remove";
    EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_OK);
    EXPECT_FALSE(std::filesystem::exists(TempDir() / "VALID"));
    EXPECT_EQ(sceSaveDataDelete(&del), SAVE_DATA_OK); // Missing saves retain success semantics.
}

// A dangling link forces recursive copy failure after staging starts. Never publish
// that partial snapshot, allocate a slot, or let recovery replace the original save.
TEST_F(SaveDataFidelityTest, FailedSnapshotCopyIsNotPublished) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    const auto save = TempDir() / "COPY_FAIL";
    std::filesystem::create_directory(save);
    std::ofstream(save / "state") << "original";
    std::error_code ec;
    std::filesystem::create_symlink(save / "missing", save / "broken", ec);
    if (ec) GTEST_SKIP() << "Symlink creation unavailable: " << ec.message();
    auto name = MakeDirName("COPY_FAIL");
    SaveDataMount3 mount{};
    mount.dir_name = &name;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_RDWR;
    SaveDataMountResult result{};
    EXPECT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_ERROR_INTERNAL);
    EXPECT_FALSE(std::filesystem::exists(TempDir() / "COPY_FAIL.portps5-prev"));
    EXPECT_FALSE(std::filesystem::exists(TempDir() / ".COPY_FAIL.portps5-prev.tmp"));
    ResetSaveDataStateForTesting();
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    std::string contents;
    std::ifstream(save / "state") >> contents;
    EXPECT_EQ(contents, "original");
    std::filesystem::remove(save / "broken");
    ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);
    EXPECT_STREQ(result.mount_point.data, "/_sm/0");
    EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);
}

// Concurrent callers of the same mount must observe exactly one owner; BUSY
// checking and free-slot selection form one transaction under the module mutex.
TEST_F(SaveDataFidelityTest, ConcurrentMountHasSingleOwner) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    auto name = MakeDirName("SHARED");
    SaveDataMount3 mount{};
    mount.dir_name = &name;
    mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
    constexpr std::size_t count = 8;
    std::barrier start(static_cast<std::ptrdiff_t>(count));
    std::array<int, count> codes{};
    std::array<SaveDataMountResult, count> results{};
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < count; ++i) {
        threads.emplace_back([&, i] {
            start.arrive_and_wait();
            codes[i] = sceSaveDataMount3(&mount, &results[i]);
        });
    }
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(std::count(codes.begin(), codes.end(), SAVE_DATA_OK), 1);
    EXPECT_EQ(std::count(codes.begin(), codes.end(), SAVE_DATA_ERROR_BUSY), count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        if (codes[i] == SAVE_DATA_OK) {
            EXPECT_EQ(sceSaveDataUmount2(0, &results[i].mount_point), SAVE_DATA_OK);
        }
    }
}

// Slot allocation/reuse must not cross-wire metadata or icons between concurrent
// saves. Each worker owns its mount while other workers mount and unmount theirs.
TEST_F(SaveDataFidelityTest, ConcurrentSlotsKeepMetadataAndIconsIsolated) {
    ASSERT_EQ(sceSaveDataInitialize3(nullptr), SAVE_DATA_OK);
    constexpr int count = 8;
    std::barrier start(count);
    std::vector<std::thread> threads;
    for (int i = 0; i < count; ++i) {
        threads.emplace_back([&, i] {
            const std::string payload = "worker" + std::to_string(i);
            auto name = MakeDirName(payload.c_str());
            SaveDataMount3 mount{};
            mount.dir_name = &name;
            mount.mount_mode = SAVE_DATA_MOUNT_MODE_CREATE2;
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 20; ++iteration) {
                SaveDataMountResult result{};
                ASSERT_EQ(sceSaveDataMount3(&mount, &result), SAVE_DATA_OK);
                EXPECT_EQ(sceSaveDataSetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_TITLE,
                                             payload.c_str(), payload.size() + 1), SAVE_DATA_OK);
                char title[128]{};
                EXPECT_EQ(sceSaveDataGetParam(&result.mount_point, SAVE_DATA_PARAM_TYPE_TITLE,
                                             title, sizeof(title), nullptr), SAVE_DATA_OK);
                EXPECT_STREQ(title, payload.c_str());
                SaveDataIcon icon{};
                icon.buf = const_cast<char*>(payload.data());
                icon.buf_size = icon.data_size = payload.size();
                EXPECT_EQ(sceSaveDataSaveIcon(&result.mount_point, &icon), SAVE_DATA_OK);
                const auto iconPath = TempDir() / payload / ".portps5" / "icon0.png";
                EXPECT_EQ(sceSaveDataSaveIconByPath(&result.mount_point, iconPath.string().c_str()), SAVE_DATA_OK);
                SaveDataMountInfo info{};
                EXPECT_EQ(sceSaveDataGetMountInfo(&result.mount_point, &info), SAVE_DATA_OK);
                EXPECT_LE(info.free_blocks, info.blocks);
                char bytes[32]{};
                icon.buf = bytes;
                icon.buf_size = sizeof(bytes);
                EXPECT_EQ(sceSaveDataLoadIcon(&result.mount_point, &icon), SAVE_DATA_OK);
                EXPECT_EQ(std::string(bytes, icon.data_size), payload);
                EXPECT_EQ(sceSaveDataUmount2(0, &result.mount_point), SAVE_DATA_OK);
            }
        });
    }
    for (auto& thread : threads) thread.join();
}

} // namespace
