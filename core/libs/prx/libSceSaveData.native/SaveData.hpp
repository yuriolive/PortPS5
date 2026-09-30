// core/libs/prx/libSceSaveData.native/SaveData.hpp
// Header declarations, constants, error codes, and path resolution for libSceSaveData.native.
// Enforces System V ABI invariants and provides hermetic save directory helpers.

#ifndef CORE_LIBS_PRX_LIBSCESAVEDATANATIVE_SAVEDATA_HPP
#define CORE_LIBS_PRX_LIBSCESAVEDATANATIVE_SAVEDATA_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// SCE SaveData Return Codes (docs/spec/save-data.md)
constexpr int SAVE_DATA_OK = 0;
constexpr int SAVE_DATA_ERROR_PARAMETER = -2137063424; // 0x809F0000
constexpr int SAVE_DATA_ERROR_NOT_INITIALIZED = -2137063423; // 0x809F0001
constexpr int SAVE_DATA_ERROR_ALREADY_INITIALIZED = -2137063422; // 0x809F0002
constexpr int SAVE_DATA_ERROR_OUT_OF_MEMORY = -2137063421; // 0x809F0003
constexpr int SAVE_DATA_ERROR_BUSY = -2137063420; // 0x809F0004
constexpr int SAVE_DATA_ERROR_NOT_MOUNTED = -2137063419; // 0x809F0005
constexpr int SAVE_DATA_ERROR_MOUNT_FULL = -2137063418; // 0x809F0006
constexpr int SAVE_DATA_ERROR_EXISTS = -2137063414; // 0x809F000A
constexpr int SAVE_DATA_ERROR_NOT_FOUND = -2137063413; // 0x809F000B
constexpr int SAVE_DATA_ERROR_NO_SPACE = -2137063412; // 0x809F000C
constexpr int SAVE_DATA_ERROR_INTERNAL = -2137063411; // 0x809F000D
constexpr int SAVE_DATA_ERROR_MEMORY_NOT_READY = -2137063406; // 0x809F0012

// Mount mode bits
constexpr std::uint32_t SAVE_DATA_MOUNT_MODE_RDONLY = 1;
constexpr std::uint32_t SAVE_DATA_MOUNT_MODE_RDWR = 2;
constexpr std::uint32_t SAVE_DATA_MOUNT_MODE_CREATE = 4;
constexpr std::uint32_t SAVE_DATA_MOUNT_MODE_CREATE2 = 32;

// Param type selectors
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_ALL = 0;
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_TITLE = 1;
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_SUB_TITLE = 2;
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_DETAIL = 3;
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_USER_PARAM = 4;
constexpr std::uint32_t SAVE_DATA_PARAM_TYPE_MTIME = 5;

// Block limit
constexpr std::uint64_t SAVE_DATA_BLOCKS_MAX = 32768;
constexpr std::uint64_t SAVE_DATA_BLOCK_SIZE = 32768; // 32 KiB blocks

// Slots
constexpr std::size_t SAVE_DATA_MOUNT_SLOTS = 16;
constexpr std::size_t SAVE_DATA_MEMORY_SLOTS = 16;

struct MountSlot {
    bool used = false;
    std::string mount_point;
    std::string real_path;
    std::string snapshot_path;
    bool is_rdwr = false;
    bool snapshot_created = false;
};

extern std::array<MountSlot, SAVE_DATA_MOUNT_SLOTS> g_slots;
int find_free_slot();
int find_slot_by_mount_point(const char* mount_point);
bool any_slot_used();
bool dir_name_match(const char* str, const char* pattern);

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include "prx/libc/include/General.hpp"
#include "prx/libkernel/AppMetadata/include/AppMetadata.hpp"

// Functions to query/override save base path for testing hermeticity
inline void SetSaveDataBaseDirOverride(const std::filesystem::path& path) {
    if (path.empty()) {
        SetSaveDataBaseDirOverride_nid_no_patch(nullptr);
    } else {
        SetSaveDataBaseDirOverride_nid_no_patch(path.string().c_str());
    }
}

inline std::string GetCurrentAppTitleId() {
    AppTitleId appTitleId = GetAppTitleId_nid_postfix();
    if (appTitleId.value[0] != '\0') {
        std::size_t len = 0;
        while (len < sizeof(appTitleId.value) && appTitleId.value[len] != '\0') {
            len++;
        }
        std::string s(appTitleId.value, len);
        if (!s.empty()) return s;
    }
    return "default";
}

inline std::filesystem::path GetSaveDataBaseDir() {
    const std::string overridePath = GetSaveDataBaseDirOverride_nid_no_patch();
    if (!overridePath.empty()) {
        return std::filesystem::path(overridePath);
    }

#ifdef _WIN32
    PWSTR localAppDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath))) {
        std::filesystem::path base(localAppDataPath);
        CoTaskMemFree(localAppDataPath);
        return base / "PortPS5" / "saves" / GetCurrentAppTitleId();
    }
#endif

    const char* envLocal = std::getenv("LOCALAPPDATA");
    if (envLocal != nullptr && envLocal[0] != '\0') {
        return std::filesystem::path(envLocal) / "PortPS5" / "saves" / GetCurrentAppTitleId();
    }

    return std::filesystem::current_path() / "saves" / GetCurrentAppTitleId();
}

extern "C" void ResetSaveDataStateForTesting();

#endif

