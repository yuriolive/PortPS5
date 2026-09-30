// core/libs/prx/libSceSaveData.native/Export.cpp
// Implementation of libSceSaveData.native runtime subsystem for save management.
// Enforces System V ABI invariants, per-title layout, crash safety snapshots, and atomic swaps.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include "SceTypes.hpp"
#include "SaveData.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/AppMetadata/include/AppMetadata.hpp"

// Global slots storage
std::array<MountSlot, SAVE_DATA_MOUNT_SLOTS> g_slots{};

namespace {

static std::atomic<std::int32_t> g_transaction_counter{1};
static bool g_initialized = false;
static std::mutex g_save_mutex;

// Testing directory override
static std::optional<std::filesystem::path> g_save_dir_override;

// Event queue for sceSaveDataBackup / sceSaveDataGetEventResult
static std::deque<SaveDataEvent> g_events;
constexpr std::uint32_t SAVE_DATA_EVENT_TYPE_BACKUP = 2;

// Memory blob constants
constexpr std::size_t MEM_MAX_SIZE = 0x1000000; // 16 MiB
static std::mutex g_mem_mutex;

// Migration tracking
static bool g_migration_checked = false;

} // namespace

int find_free_slot() {
    for (std::size_t i = 0; i < g_slots.size(); ++i) {
        if (!g_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find_slot_by_mount_point(const char* mount_point) {
    if (mount_point == nullptr) {
        return -1;
    }
    for (std::size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].used && g_slots[i].mount_point == mount_point) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool any_slot_used() {
    for (const auto& slot : g_slots) {
        if (slot.used) return true;
    }
    return false;
}

bool dir_name_match(const char* str, const char* pattern) {
    if (pattern == nullptr || pattern[0] == '\0') {
        return true;
    }
    while (*str != '\0' && *pattern != '\0') {
        if (*pattern == '%') {
            for (const char* s = str;; s++) {
                if (dir_name_match(s, pattern + 1)) {
                    return true;
                }
                if (*s == '\0') {
                    break;
                }
            }
            return false;
        }
        if (*pattern == '_') {
            str++;
            pattern++;
            continue;
        }
        if (*pattern != *str) {
            return false;
        }
        str++;
        pattern++;
    }
    return *str == '\0' && *pattern == '\0';
}

static bool restore_snapshots_and_migrate_if_needed() {
    std::error_code ec;
    const std::filesystem::path root = GetSaveDataBaseDir();

    // 1. One-time migration of legacy ./_sd -> target dir if target is empty/missing
    if (!g_migration_checked) {
        g_migration_checked = true;
        const std::filesystem::path legacySd = std::filesystem::current_path() / "_sd";
        if (std::filesystem::is_directory(legacySd, ec) && !ec) {
            bool targetEmptyOrMissing = true;
            if (std::filesystem::exists(root, ec) && !ec) {
                auto it = std::filesystem::directory_iterator(root, ec);
                if (!ec && it != std::filesystem::directory_iterator{}) {
                    targetEmptyOrMissing = false;
                }
            }
            if (targetEmptyOrMissing) {
                APS5_LOG_INFO("Migrating legacy save data from %s to %s", legacySd.string().c_str(), root.string().c_str());
                std::filesystem::create_directories(root, ec);
                std::filesystem::copy(legacySd, root, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
            }
        }
    }

    // 2. Scan root for any leftover *.portps5-prev snapshot directories and restore them
    if (std::filesystem::is_directory(root, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (entry.is_directory(ec)) {
                std::string filename = entry.path().filename().string();
                constexpr std::string_view suffix = ".portps5-prev";
                if (filename.size() > suffix.size() &&
                    filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    std::string originalName = filename.substr(0, filename.size() - suffix.size());
                    std::filesystem::path originalPath = root / originalName;
                    std::filesystem::path snapshotPath = entry.path();

                    APS5_LOG_INFO("Crash recovery: restoring save snapshot %s -> %s",
                                  snapshotPath.string().c_str(), originalPath.string().c_str());

                    // Stage copy in a temporary directory; retain snapshot if recovery fails
                    std::filesystem::path stagePath = root / ("." + originalName + ".restore.tmp");
                    std::filesystem::remove_all(stagePath, ec);
                    ec.clear();
                    std::filesystem::copy(snapshotPath, stagePath,
                                          std::filesystem::copy_options::recursive, ec);
                    if (ec) {
                        APS5_LOG_ERR("Crash recovery: failed to stage snapshot %s: %s",
                                     snapshotPath.string().c_str(), ec.message().c_str());
                        std::filesystem::remove_all(stagePath, ec);
                        return false;
                    }

                    // Remove existing destination and publish staged snapshot
                    std::filesystem::remove_all(originalPath, ec);
                    ec.clear();
                    std::filesystem::rename(stagePath, originalPath, ec);
                    if (ec) {
                        APS5_LOG_ERR("Crash recovery: failed to publish restored save %s: %s",
                                     originalPath.string().c_str(), ec.message().c_str());
                        std::filesystem::remove_all(stagePath, ec);
                        return false;
                    }

                    // Only delete snapshot after restore has fully succeeded
                    std::filesystem::remove_all(snapshotPath, ec);
                }
            }
        }
    }
    return true;
}

static bool atomic_write_file(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        if (size > 0 && data != nullptr) {
            f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        f.flush();
        if (!f) {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

#ifdef _WIN32
    std::wstring wtmp = tmpPath.wstring();
    std::wstring wpath = path.wstring();
    if (ReplaceFileW(wpath.c_str(), wtmp.c_str(), NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
        return true;
    }
    // If ReplaceFileW failed because destination didn't exist yet, fallback to MoveFileExW
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
        if (MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            return true;
        }
    }
    std::filesystem::remove(tmpPath, ec);
    return false;
#else
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
#endif
}

static bool file_size_of(const std::filesystem::path& path, std::size_t* out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) return false;
    *out = static_cast<std::size_t>(sz);
    return true;
}

static bool read_file_all(const std::filesystem::path& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    out.resize(n);
    if (n != 0) {
        f.read(out.data(), static_cast<std::streamsize>(n));
    }
    return static_cast<bool>(f);
}

static std::filesystem::path mem_path(std::uint32_t slot, const char* ext) {
    // Memory blobs live at <titleId>/_memory/<slot>.bin and <slot>.param
    return GetSaveDataBaseDir() / "_memory" / (std::to_string(slot) + "." + ext);
}

extern "C" void ResetSaveDataStateForTesting() {
    std::lock_guard lock(g_save_mutex);
    for (auto& slot : g_slots) {
        if (slot.used && !slot.mount_point.empty()) {
            RemovePathAlias_nid_no_patch(slot.mount_point.c_str());
        }
        slot = MountSlot{};
    }
    g_events.clear();
    g_initialized = false;
    g_migration_checked = false;
    g_transaction_counter.store(1);
}

extern "C" {

// Records save data backup request event.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_PARAMETER on invalid arguments.
int APS5_VABI sceSaveDataBackup(const SaveDataBackup* backup) noexcept {
    if (backup == nullptr || backup->dir_name == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    SaveDataEvent event{};
    event.type = SAVE_DATA_EVENT_TYPE_BACKUP;
    event.error_code = SAVE_DATA_OK;
    event.user_id = backup->user_id;
    if (backup->title_id != nullptr) event.title_id = *backup->title_id;
    event.dir_name = *backup->dir_name;

    std::lock_guard lock(g_save_mutex);
    g_events.push_back(event);
    return SAVE_DATA_OK;
}

// Commits open transaction on save storage.
// Returns SAVE_DATA_OK on success.
int APS5_VABI sceSaveDataCommit(const SaveDataCommitParam* param) noexcept {
    (void)param;
    return SAVE_DATA_OK;
}

// Allocates transaction resource identifier.
// Returns positive transaction resource id.
int APS5_VABI sceSaveDataCreateTransactionResource(uint32_t size) noexcept {
    (void)size;
    return g_transaction_counter.fetch_add(1);
}

// Deletes specified save data directory and contents.
// Returns SAVE_DATA_OK on success or error code on failure.
int APS5_VABI sceSaveDataDelete(const SaveDataDelete* del) noexcept {
    if (del == nullptr || del->dir_name == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    const auto* nameEnd = static_cast<const char*>(
        std::memchr(del->dir_name->data, '\0', sizeof(del->dir_name->data)));
    if (nameEnd == nullptr) return SAVE_DATA_ERROR_PARAMETER;
    const std::string_view name(del->dir_name->data, nameEnd - del->dir_name->data);
    if (name.empty() || name == "." || name == ".." || name == "_memory" ||
        name.find_first_of("/\\:") != std::string_view::npos) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    constexpr std::string_view kSnap = ".portps5-prev";
    if (name.size() >= kSnap.size() && name.substr(name.size() - kSnap.size()) == kSnap) {
        return SAVE_DATA_ERROR_PARAMETER;
    }

    std::lock_guard lock(g_save_mutex);
    const auto target = (GetSaveDataBaseDir() / name).string();
    for (const auto& s : g_slots) {
        if (s.used && s.real_path == target) {
            return SAVE_DATA_ERROR_BUSY;
        }
    }

    try {
        const std::filesystem::path path = GetSaveDataBaseDir() / name;
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            std::filesystem::remove_all(path, ec);
            if (ec) return SAVE_DATA_ERROR_INTERNAL;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Frees transaction resource identifier.
// Returns SAVE_DATA_OK on success.
int APS5_VABI sceSaveDataDeleteTransactionResource(int32_t resource) noexcept {
    (void)resource;
    return SAVE_DATA_OK;
}

// Searches for save directories matching pattern ('%' wildcard and '_' single character).
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_PARAMETER on invalid arguments.
int APS5_VABI sceSaveDataDirNameSearch(const SaveDataDirNameSearchCond* cond, SaveDataDirNameSearchResult* result) noexcept {
    if (cond == nullptr || result == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    try {
        result->hit_num = 0;
        result->set_num = 0;
        const char* pattern = (cond->dir_name != nullptr) ? cond->dir_name->data : nullptr;
        const std::filesystem::path root = GetSaveDataBaseDir();
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec) {
            return SAVE_DATA_OK;
        }

        std::uint32_t hit = 0;
        std::uint32_t set = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            // Skip internal or snapshot directories
            if (name.empty() || name[0] == '.' || name == "_memory" ||
                (name.size() > 14 && name.compare(name.size() - 14, 14, ".portps5-prev") == 0)) {
                continue;
            }
            if (!dir_name_match(name.c_str(), pattern)) {
                continue;
            }
            hit++;
            if (result->dir_names != nullptr && set < result->dir_names_num) {
                std::snprintf(result->dir_names[set].data, sizeof(result->dir_names[set].data), "%s", name.c_str());
                if (result->params != nullptr) {
                    std::memset(&result->params[set], 0, sizeof(SaveDataParam));
                    // Try to load param from <dirName>/.portps5/param.bin
                    std::filesystem::path paramFile = entry.path() / ".portps5" / "param.bin";
                    std::vector<char> pdata;
                    if (read_file_all(paramFile, pdata) && pdata.size() >= sizeof(SaveDataParam)) {
                        std::memcpy(&result->params[set], pdata.data(), sizeof(SaveDataParam));
                    }
                }
                set++;
            }
        }
        result->hit_num = hit;
        result->set_num = set;
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Retrieves pending save data background event notification.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_NOT_FOUND when queue is empty.
int APS5_VABI sceSaveDataGetEventResult(const void* event_param, SaveDataEvent* event) noexcept {
    (void)event_param;
    if (event == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    if (g_events.empty()) {
        return SAVE_DATA_ERROR_NOT_FOUND;
    }
    *event = g_events.front();
    g_events.pop_front();
    return SAVE_DATA_OK;
}

// Queries storage block capacity and free blocks for a mounted save slot.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_NOT_MOUNTED if slot invalid.
int APS5_VABI sceSaveDataGetMountInfo(const SaveDataMountPoint* mount_point, SaveDataMountInfo* info) noexcept {
    if (mount_point == nullptr || info == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }
    std::memset(info, 0, sizeof(*info));
    info->blocks = SAVE_DATA_BLOCKS_MAX;

    std::error_code ec;
    auto spaceInfo = std::filesystem::space(g_slots[slot].real_path, ec);
    if (!ec && spaceInfo.available != static_cast<std::uintmax_t>(-1)) {
        std::uint64_t freeBlocks = spaceInfo.available / SAVE_DATA_BLOCK_SIZE;
        info->free_blocks = std::min<std::uint64_t>(freeBlocks, SAVE_DATA_BLOCKS_MAX);
    } else {
        info->free_blocks = SAVE_DATA_BLOCKS_MAX;
    }

    return SAVE_DATA_OK;
}

// Retrieves metadata parameters (title, subtitle, detail, user_param) from mounted save directory.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataGetParam(const SaveDataMountPoint* mount_point, uint32_t param_type, void* param_buf, size_t param_buf_size, size_t* got_size) noexcept {
    if (mount_point == nullptr || param_buf == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }

    try {
        std::filesystem::path paramFile = std::filesystem::path(g_slots[slot].real_path) / ".portps5" / "param.bin";
        SaveDataParam param{};
        std::vector<char> pdata;
        if (read_file_all(paramFile, pdata) && pdata.size() >= sizeof(SaveDataParam)) {
            std::memcpy(&param, pdata.data(), sizeof(SaveDataParam));
        }

        std::memset(param_buf, 0, param_buf_size);
        std::size_t written = 0;

        switch (param_type) {
            case SAVE_DATA_PARAM_TYPE_ALL: {
                written = std::min(param_buf_size, sizeof(SaveDataParam));
                std::memcpy(param_buf, &param, written);
                break;
            }
            case SAVE_DATA_PARAM_TYPE_TITLE: {
                written = std::min(param_buf_size, sizeof(param.title));
                std::memcpy(param_buf, param.title, written);
                break;
            }
            case SAVE_DATA_PARAM_TYPE_SUB_TITLE: {
                written = std::min(param_buf_size, sizeof(param.sub_title));
                std::memcpy(param_buf, param.sub_title, written);
                break;
            }
            case SAVE_DATA_PARAM_TYPE_DETAIL: {
                written = std::min(param_buf_size, sizeof(param.detail));
                std::memcpy(param_buf, param.detail, written);
                break;
            }
            case SAVE_DATA_PARAM_TYPE_USER_PARAM: {
                written = std::min(param_buf_size, sizeof(param.user_param));
                std::memcpy(param_buf, &param.user_param, written);
                break;
            }
            case SAVE_DATA_PARAM_TYPE_MTIME: {
                written = std::min(param_buf_size, sizeof(param.mtime));
                std::memcpy(param_buf, &param.mtime, written);
                break;
            }
            default:
                written = std::min(param_buf_size, sizeof(SaveDataParam));
                std::memcpy(param_buf, &param, written);
                break;
        }

        if (got_size != nullptr) {
            *got_size = written;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Reads memory slot payload and metadata from persistent memory storage.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataGetSaveDataMemory2(SaveDataMemoryGet2* get_param) noexcept {
    if (get_param == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    if (!g_initialized) {
        return SAVE_DATA_ERROR_NOT_INITIALIZED;
    }

    try {
        std::lock_guard lock(g_mem_mutex);
        const std::filesystem::path binFile = mem_path(get_param->slot_id, "bin");
        std::size_t size = 0;
        if (!file_size_of(binFile, &size)) {
            return SAVE_DATA_ERROR_MEMORY_NOT_READY;
        }

        const SaveDataMemoryData* d = get_param->data;
        if (d != nullptr && d->buf_size != 0) {
            if (d->buf == nullptr || d->offset > size || d->buf_size > size - d->offset) {
                return SAVE_DATA_ERROR_PARAMETER;
            }
            std::ifstream f(binFile, std::ios::binary);
            if (!f) return SAVE_DATA_ERROR_INTERNAL;
            f.seekg(static_cast<std::streamoff>(d->offset));
            f.read(static_cast<char*>(d->buf), static_cast<std::streamsize>(d->buf_size));
            if (!f) return SAVE_DATA_ERROR_INTERNAL;
        }

        if (get_param->param != nullptr) {
            std::memset(get_param->param, 0, sizeof(SaveDataParam));
            std::vector<char> pd;
            if (read_file_all(mem_path(get_param->slot_id, "param"), pd)) {
                std::memcpy(get_param->param, pd.data(), std::min(pd.size(), sizeof(SaveDataParam)));
            }
        }

        if (get_param->icon != nullptr) {
            get_param->icon->data_size = 0;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Initializes the save data subsystem and recovers crash snapshots if needed.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_ALREADY_INITIALIZED if initialized.
int APS5_VABI sceSaveDataInitialize3(const void* init) noexcept {
    (void)init;
    std::lock_guard lock(g_save_mutex);
    if (g_initialized) {
        return SAVE_DATA_ERROR_ALREADY_INITIALIZED;
    }
    try {
        if (!restore_snapshots_and_migrate_if_needed()) {
            return SAVE_DATA_ERROR_INTERNAL;
        }
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
    g_initialized = true;
    return SAVE_DATA_OK;
}

// Loads saved game icon data from the mounted save directory.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataLoadIcon(const SaveDataMountPoint* mount_point, SaveDataIcon* icon) noexcept {
    if (mount_point == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }
    if (icon == nullptr) {
        return SAVE_DATA_OK;
    }

    try {
        std::filesystem::path iconFile = std::filesystem::path(g_slots[slot].real_path) / ".portps5" / "icon0.png";
        std::vector<char> idata;
        if (!read_file_all(iconFile, idata)) {
            icon->data_size = 0;
            return SAVE_DATA_OK;
        }
        icon->data_size = idata.size();
        if (icon->buf != nullptr && icon->buf_size > 0) {
            std::size_t toCopy = std::min(icon->buf_size, idata.size());
            std::memcpy(icon->buf, idata.data(), toCopy);
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Mounts a save data directory using requested mount mode (CREATE, CREATE2, RDONLY, RDWR).
// Returns SAVE_DATA_OK on success, or SCE error (BUSY, EXISTS, NOT_FOUND, NO_SPACE) or aborts on invalid mode.
int APS5_VABI sceSaveDataMount3(const SaveDataMount3* mount, SaveDataMountResult* mount_result) noexcept {
    if (mount == nullptr || mount_result == nullptr || mount->dir_name == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }

    const bool create = (mount->mount_mode & SAVE_DATA_MOUNT_MODE_CREATE) != 0;
    const bool create2 = (mount->mount_mode & SAVE_DATA_MOUNT_MODE_CREATE2) != 0;
    const bool rdonly = (mount->mount_mode & SAVE_DATA_MOUNT_MODE_RDONLY) != 0;
    const bool rdwr = (mount->mount_mode & SAVE_DATA_MOUNT_MODE_RDWR) != 0;
    const bool open = !create && !create2 && (rdonly || rdwr);

    constexpr std::uint32_t VALID_MOUNT_MODES = SAVE_DATA_MOUNT_MODE_RDONLY |
                                                SAVE_DATA_MOUNT_MODE_RDWR |
                                                SAVE_DATA_MOUNT_MODE_CREATE |
                                                SAVE_DATA_MOUNT_MODE_CREATE2;

    // Any unrecognized mount_mode aborts through Unsupported() per spec
    if ((mount->mount_mode & ~VALID_MOUNT_MODES) != 0 || (!create && !create2 && !open)) {
        Unsupported("sceSaveDataMount3: unknown mount_mode");
        return SAVE_DATA_ERROR_PARAMETER;
    }

    const auto* nameEnd = static_cast<const char*>(std::memchr(mount->dir_name->data, '\0', sizeof(mount->dir_name->data)));
    if (nameEnd == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    const std::string dirName(mount->dir_name->data, static_cast<std::size_t>(nameEnd - mount->dir_name->data));
    if (dirName.empty() || dirName == "." || dirName == ".." || dirName.find_first_of("/\\:") != std::string::npos) {
        return SAVE_DATA_ERROR_PARAMETER;
    }

    try {
        // Serialize BUSY checks, slot selection, and publication with slot users.
        std::lock_guard lock(g_save_mutex);
        const std::filesystem::path root = GetSaveDataBaseDir();
        const std::filesystem::path real_path = root / dirName;
        const std::filesystem::path snapshot_path = root / (dirName + ".portps5-prev");

        // Quota check: if requested blocks > SAVE_DATA_BLOCKS_MAX
        if (mount->blocks > SAVE_DATA_BLOCKS_MAX) {
            return SAVE_DATA_ERROR_NO_SPACE;
        }

        std::error_code ec;
        auto spaceInfo = std::filesystem::space(root, ec);
        if (!ec && spaceInfo.available != static_cast<std::uintmax_t>(-1)) {
            std::uint64_t availableBlocks = spaceInfo.available / SAVE_DATA_BLOCK_SIZE;
            if (mount->blocks > availableBlocks) {
                return SAVE_DATA_ERROR_NO_SPACE;
            }
        }

        for (const auto& used : g_slots) {
            if (used.used && used.real_path == real_path.string()) {
                return SAVE_DATA_ERROR_BUSY;
            }
        }

        const bool exists = std::filesystem::is_directory(real_path, ec);
        if (create && exists) {
            return SAVE_DATA_ERROR_EXISTS;
        }
        if (open && !exists) {
            return SAVE_DATA_ERROR_NOT_FOUND;
        }

        int slot = find_free_slot();
        if (slot == -1) {
            return SAVE_DATA_ERROR_MOUNT_FULL;
        }

        if (create || create2) {
            std::filesystem::create_directories(real_path, ec);
            std::filesystem::create_directories(real_path / ".portps5", ec);
        }

        // Publish only complete snapshots. Hidden staging copies are ignored by recovery
        // and dialog scans, including if the process dies while copying.
        bool snapshotCreated = false;
        if (rdwr && exists) {
            const auto temporaryPath = root / ("." + dirName + ".portps5-prev.tmp");
            std::filesystem::remove_all(temporaryPath, ec);
            if (ec) return SAVE_DATA_ERROR_INTERNAL;
            std::filesystem::copy(real_path, temporaryPath,
                                  std::filesystem::copy_options::recursive, ec);
            if (!ec) std::filesystem::remove_all(snapshot_path, ec);
            if (!ec) std::filesystem::rename(temporaryPath, snapshot_path, ec);
            if (ec) {
                std::error_code cleanupError;
                std::filesystem::remove_all(temporaryPath, cleanupError);
                return SAVE_DATA_ERROR_INTERNAL;
            }
            snapshotCreated = true;
        }

        // Short mount point aliased via path alias table
        const std::string mountPoint = "/_sm/" + std::to_string(slot);
        AddPathAlias_nid_no_patch(mountPoint.c_str(), std::filesystem::absolute(real_path).string().c_str());

        g_slots[slot].used = true;
        g_slots[slot].mount_point = mountPoint;
        g_slots[slot].real_path = real_path.string();
        g_slots[slot].snapshot_path = snapshot_path.string();
        g_slots[slot].is_rdwr = rdwr;
        g_slots[slot].snapshot_created = snapshotCreated;

        std::memset(mount_result, 0, sizeof(*mount_result));
        std::snprintf(mount_result->mount_point.data, sizeof(mount_result->mount_point.data), "%s", mountPoint.c_str());
        mount_result->required_blocks = 0;
        mount_result->mount_status = (create || create2) ? 1u : 0u;

        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Prepares transaction resources for save operations.
// Returns SAVE_DATA_OK on success.
int APS5_VABI sceSaveDataPrepare(const SaveDataMountPoint* mount_point, const SaveDataPrepareParam* param) noexcept {
    (void)mount_point;
    (void)param;
    return SAVE_DATA_OK;
}

// Saves icon binary data to the mounted save directory (.portps5/icon0.png).
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataSaveIcon(const SaveDataMountPoint* mount_point, const SaveDataIcon* icon) noexcept {
    if (mount_point == nullptr || icon == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }
    try {
        std::filesystem::path iconPath = std::filesystem::path(g_slots[slot].real_path) / ".portps5" / "icon0.png";
        if (!atomic_write_file(iconPath, icon->buf, icon->data_size)) {
            return SAVE_DATA_ERROR_INTERNAL;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Copies an icon image file from guest path to the mounted save directory (.portps5/icon0.png).
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataSaveIconByPath(const SaveDataMountPoint* mount_point, const char* path) noexcept {
    if (mount_point == nullptr || path == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }
    try {
        std::vector<char> data;
        if (!read_file_all(path, data)) {
            return SAVE_DATA_ERROR_NOT_FOUND;
        }
        std::filesystem::path iconPath = std::filesystem::path(g_slots[slot].real_path) / ".portps5" / "icon0.png";
        if (!atomic_write_file(iconPath, data.data(), data.size())) {
            return SAVE_DATA_ERROR_INTERNAL;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Updates metadata parameters (title, subtitle, detail, user_param) in the mounted save directory.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataSetParam(const SaveDataMountPoint* mount_point, uint32_t param_type, const void* param_buf, size_t param_buf_size) noexcept {
    if (mount_point == nullptr || param_buf == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }

    try {
        std::filesystem::path paramPath = std::filesystem::path(g_slots[slot].real_path) / ".portps5" / "param.bin";
        SaveDataParam param{};
        std::vector<char> pdata;
        if (read_file_all(paramPath, pdata) && pdata.size() >= sizeof(SaveDataParam)) {
            std::memcpy(&param, pdata.data(), sizeof(SaveDataParam));
        }

        switch (param_type) {
            case SAVE_DATA_PARAM_TYPE_ALL: {
                std::memcpy(&param, param_buf, std::min(param_buf_size, sizeof(SaveDataParam)));
                break;
            }
            case SAVE_DATA_PARAM_TYPE_TITLE: {
                std::memset(param.title, 0, sizeof(param.title));
                std::memcpy(param.title, param_buf, std::min(param_buf_size, sizeof(param.title) - 1));
                break;
            }
            case SAVE_DATA_PARAM_TYPE_SUB_TITLE: {
                std::memset(param.sub_title, 0, sizeof(param.sub_title));
                std::memcpy(param.sub_title, param_buf, std::min(param_buf_size, sizeof(param.sub_title) - 1));
                break;
            }
            case SAVE_DATA_PARAM_TYPE_DETAIL: {
                std::memset(param.detail, 0, sizeof(param.detail));
                std::memcpy(param.detail, param_buf, std::min(param_buf_size, sizeof(param.detail) - 1));
                break;
            }
            case SAVE_DATA_PARAM_TYPE_USER_PARAM: {
                if (param_buf_size >= sizeof(param.user_param)) {
                    std::memcpy(&param.user_param, param_buf, sizeof(param.user_param));
                }
                break;
            }
            case SAVE_DATA_PARAM_TYPE_MTIME: {
                if (param_buf_size >= sizeof(param.mtime)) {
                    std::memcpy(&param.mtime, param_buf, sizeof(param.mtime));
                }
                break;
            }
            default:
                std::memcpy(&param, param_buf, std::min(param_buf_size, sizeof(SaveDataParam)));
                break;
        }

        if (!atomic_write_file(paramPath, &param, sizeof(SaveDataParam))) {
            return SAVE_DATA_ERROR_INTERNAL;
        }
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Atomically writes guest memory blob chunks into persistent storage slot.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataSetSaveDataMemory2(const SaveDataMemorySet2* set_param) noexcept {
    if (set_param == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    if (!g_initialized) {
        return SAVE_DATA_ERROR_NOT_INITIALIZED;
    }

    try {
        std::lock_guard lock(g_mem_mutex);
        const std::filesystem::path binFile = mem_path(set_param->slot_id, "bin");
        std::size_t size = 0;
        if (!file_size_of(binFile, &size)) {
            return SAVE_DATA_ERROR_MEMORY_NOT_READY;
        }

        // Validate ranges first
        const std::uint32_t n = set_param->data != nullptr ? (set_param->data_num != 0 ? set_param->data_num : 1u) : 0u;
        for (std::uint32_t i = 0; i < n; i++) {
            const SaveDataMemoryData& d = set_param->data[i];
            if (d.buf_size == 0) continue;
            if (d.buf == nullptr || d.offset > size || d.buf_size > size - d.offset) {
                return SAVE_DATA_ERROR_PARAMETER;
            }
        }

        if (n != 0) {
            std::vector<char> buffer;
            if (!read_file_all(binFile, buffer)) {
                return SAVE_DATA_ERROR_INTERNAL;
            }
            buffer.resize(size);
            for (std::uint32_t i = 0; i < n; i++) {
                const SaveDataMemoryData& d = set_param->data[i];
                if (d.buf_size == 0) continue;
                std::memcpy(buffer.data() + d.offset, d.buf, d.buf_size);
            }
            // Atomic file replace
            if (!atomic_write_file(binFile, buffer.data(), buffer.size())) {
                return SAVE_DATA_ERROR_INTERNAL;
            }
        }

        if (set_param->param != nullptr) {
            atomic_write_file(mem_path(set_param->slot_id, "param"), set_param->param, sizeof(SaveDataParam));
        }

        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Sets up memory save slot file with designated quota size.
// Returns SAVE_DATA_OK on success or SCE error code (NO_SPACE, PARAMETER) on failure.
int APS5_VABI sceSaveDataSetupSaveDataMemory2(const SaveDataMemorySetup2* setup_param, SaveDataMemorySetupResult* result) noexcept {
    if (setup_param == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    if (!g_initialized) {
        return SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (setup_param->memory_size == 0) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    if (setup_param->memory_size > MEM_MAX_SIZE) {
        return SAVE_DATA_ERROR_NO_SPACE;
    }

    try {
        std::lock_guard lock(g_mem_mutex);
        const std::filesystem::path binFile = mem_path(setup_param->slot_id, "bin");
        std::size_t existed = 0;
        const bool have = file_size_of(binFile, &existed);
        if (!have) existed = 0;

        if (!have || existed < setup_param->memory_size) {
            std::vector<char> data;
            if (have) {
                read_file_all(binFile, data);
            }
            data.resize(setup_param->memory_size, 0);
            if (!atomic_write_file(binFile, data.data(), data.size())) {
                return SAVE_DATA_ERROR_INTERNAL;
            }
            if (setup_param->init_param != nullptr && (setup_param->option & 1u) != 0) {
                atomic_write_file(mem_path(setup_param->slot_id, "param"), setup_param->init_param, sizeof(SaveDataParam));
            }
        }

        if (result != nullptr) {
            std::memset(result, 0, sizeof(*result));
            result->existed_memory_size = have ? existed : 0;
        }

        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

// Synchronizes memory slot buffers to disk.
// Returns SAVE_DATA_OK on success or SCE error code on failure.
int APS5_VABI sceSaveDataSyncSaveDataMemory(const void* sync_param) noexcept {
    if (sync_param == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    const std::uint32_t slot_id = reinterpret_cast<const std::uint32_t*>(sync_param)[1];
    std::lock_guard lock(g_mem_mutex);
    std::size_t size = 0;
    if (!file_size_of(mem_path(slot_id, "bin"), &size)) {
        return SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }
    return SAVE_DATA_OK;
}

// Terminates the save data subsystem after ensuring all mounts are unmounted.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_BUSY if slots remain mounted.
int APS5_VABI sceSaveDataTerminate(void) noexcept {
    std::lock_guard lock(g_save_mutex);
    if (!g_initialized) {
        return SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (any_slot_used()) {
        return SAVE_DATA_ERROR_BUSY;
    }
    g_initialized = false;
    return SAVE_DATA_OK;
}

// Unsupported transferring mount for cloud/backup transfer.
// Aborts execution via Unsupported() per spec.
int APS5_VABI sceSaveDataTransferringMount(const SaveDataTransferringMount* mount, SaveDataMountResult* mount_result) noexcept {
    (void)mount;
    (void)mount_result;
    // docs/spec/save-data.md: TransferringMount calls Unsupported() until a gate title needs it
    Unsupported("sceSaveDataTransferringMount");
    return SAVE_DATA_ERROR_NOT_FOUND;
}

// Unmounts mounted save directory, removes path aliases, and deletes snapshot on clean unmount.
// Returns SAVE_DATA_OK on success or SAVE_DATA_ERROR_NOT_MOUNTED if not mounted.
int APS5_VABI sceSaveDataUmount2(uint32_t mode, const SaveDataMountPoint* mount_point) noexcept {
    (void)mode;
    if (mount_point == nullptr) {
        return SAVE_DATA_ERROR_PARAMETER;
    }
    std::lock_guard lock(g_save_mutex);
    int slot = find_slot_by_mount_point(mount_point->data);
    if (slot == -1) {
        return SAVE_DATA_ERROR_NOT_MOUNTED;
    }

    try {
        RemovePathAlias_nid_no_patch(g_slots[slot].mount_point.c_str());

        // Clean unmount deletes snapshot if one was created
        if (g_slots[slot].snapshot_created && !g_slots[slot].snapshot_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(g_slots[slot].snapshot_path, ec);
        }

        g_slots[slot] = MountSlot{};
        return SAVE_DATA_OK;
    } catch (...) {
        return SAVE_DATA_ERROR_INTERNAL;
    }
}

} // extern "C"
