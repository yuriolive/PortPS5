// core/libs/prx/libc/src/General.cpp
// Implementation of general libc runtime support: path aliasing, guest directory resolution,
// common dialog state tracking, and unsupported call abort handling.

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <mutex>
#include <cerrno>
#include <cstring>
#include <vector>
#include <utility>
#include <optional>
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/GuestHeap.hpp"

namespace {
// Guest prefixes mapped to host directories, e.g. save-data mount points ("/_sm/0").
struct PathAliases {
    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> entries;
};

PathAliases& Aliases() {
    static PathAliases aliases;
    return aliases;
}

std::string TrimSlashes(const char* path) {
    std::string s(path);
    std::size_t start = 0;
    while (start < s.size() && (s[start] == '/' || s[start] == '\\')) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && (s[end - 1] == '/' || s[end - 1] == '\\')) {
        --end;
    }
    return s.substr(start, end - start);
}

std::optional<std::filesystem::path> ResolveAlias(const std::string& guestPath) {
    const auto relative = TrimSlashes(guestPath.c_str());
    auto& aliases = Aliases();
    std::lock_guard lock(aliases.mutex);
    for (const auto& [prefix, host] : aliases.entries) {
        if (relative.size() < prefix.size() || relative.compare(0, prefix.size(), prefix) != 0) continue;
        if (relative.size() == prefix.size()) return std::filesystem::path(host).make_preferred();
        if (relative[prefix.size()] != '/') continue;
        std::filesystem::path result = std::filesystem::path(host) / std::filesystem::path(relative.substr(prefix.size() + 1));
        return result.make_preferred();
    }
    return std::nullopt;
}

struct WorkingDirectory {
    std::mutex mutex;
    const std::filesystem::path root = std::filesystem::canonical(std::filesystem::current_path());
    std::filesystem::path current = root;
};
WorkingDirectory& Directories() { static WorkingDirectory state; return state; }
std::filesystem::path Resolve(WorkingDirectory& state, const char* path) {
    std::string text(path);
    for (auto& character : text) if (character == '\\') character = '/';
    std::filesystem::path input(text);
#ifdef _WIN32
    // Preserve the existing ability to pass explicit native drive paths.
    if (input.has_root_name()) return input;
#endif
    auto guest = (std::filesystem::path("/") / state.current.lexically_relative(state.root));
    guest = (input.is_absolute() ? input : guest / input).lexically_normal();
    if (auto aliased = ResolveAlias(guest.relative_path().generic_string())) return *aliased;
    return (state.root / guest.relative_path()).make_preferred();
}
int DirectoryFailure(const std::error_code& error) {
    if (error == std::errc::permission_denied) return 13;
    if (error == std::errc::not_a_directory) return 20;
    if (error == std::errc::no_such_file_or_directory) return 2;
    if (error == std::errc::filename_too_long) return 63;
    if (error == std::errc::too_many_symbolic_link_levels) return 62;
    return 5;
}
}

extern "C" void AddPathAlias_nid_no_patch(const char* guestPrefix, const char* hostPath) {
    if (guestPrefix == nullptr || hostPath == nullptr) {
        APS5_INVALID_ARG_EX;
    }
    auto& aliases = Aliases();
    std::lock_guard lock(aliases.mutex);
    const auto prefix = TrimSlashes(guestPrefix);
    for (auto& entry : aliases.entries) {
        if (entry.first == prefix) {
            entry.second = hostPath;
            return;
        }
    }
    aliases.entries.emplace_back(prefix, hostPath);
}

extern "C" void RemovePathAlias_nid_no_patch(const char* guestPrefix) {
    if (guestPrefix == nullptr) {
        APS5_INVALID_ARG_EX;
    }
    auto& aliases = Aliases();
    std::lock_guard lock(aliases.mutex);
    const auto prefix = TrimSlashes(guestPrefix);
    std::erase_if(aliases.entries, [&](const auto& entry) { return entry.first == prefix; });
}

extern "C" std::filesystem::path ResolvePath_nid_no_patch(const char* path) {
    if (!path) { APS5_INVALID_ARG_EX; }
    auto& state = Directories();
    std::lock_guard lock(state.mutex);
    return Resolve(state, path);
}

// Changes current guest working directory within guest sandbox root.
// Returns 0 on success or -1 on error with errno set (EFAULT, ENOENT, ENOTDIR).
extern "C" int APS5_VABI chdir_nid_postfix(const char* path) {
    if (!path) { errno = 14; return -1; }
    if (!*path) { errno = 2; return -1; }
    try {
        auto& state = Directories();
        std::lock_guard lock(state.mutex);
        std::error_code error;
        const auto resolved = std::filesystem::canonical(Resolve(state, path), error);
        if (error) { errno = DirectoryFailure(error); return -1; }
        if (!std::filesystem::is_directory(resolved, error)) {
            errno = error ? DirectoryFailure(error) : 20; return -1;
        }
        const auto relative = resolved.lexically_relative(state.root);
        if (relative.empty() || *relative.begin() == "..") { errno = 45; return -1; }
        state.current = resolved;
        return 0;
    } catch (const std::bad_alloc&) { errno = 12; return -1; }
      catch (const std::filesystem::filesystem_error& error) { errno = DirectoryFailure(error.code()); return -1; }
}

// Retrieves current guest working directory relative to guest root.
// Returns buffer pointer on success or nullptr on failure with errno set.
extern "C" char* APS5_VABI getcwd_nid_postfix(char* buffer, std::size_t size) {
    if (buffer && size == 0) { errno = 22; return nullptr; }
    try {
        auto& state = Directories();
        std::lock_guard lock(state.mutex);
        std::error_code error;
        if (!std::filesystem::is_directory(state.current, error)) {
            errno = error ? DirectoryFailure(error) : 2; return nullptr;
        }
        const auto relative = state.current.lexically_relative(state.root);
        const auto path = relative == "." ? std::string("/") : "/" + relative.generic_string();
        const auto required = path.size() + 1;
        if ((buffer || size) && size < required) { errno = 34; return nullptr; }
        if (!buffer) buffer = static_cast<char*>(GuestHeap::GuestHeapAllocate_nid_postfix(size ? size : required));
        std::memcpy(buffer, path.c_str(), required);
        return buffer;
    } catch (const std::bad_alloc&) { errno = 12; return nullptr; }
      catch (const std::filesystem::filesystem_error& error) { errno = DirectoryFailure(error.code()); return nullptr; }
}

void Unsupported(const char* what) {
    APS5_LOG_ERR("Unsupported: %s", what ? what : "?");
    std::abort();
}

extern "C" void NotImplemented_nid_no_patch(const char* funcName) {
    // Why abort, not throw: the shared unwinder lets guest catch(...) swallow
    // host exceptions, turning unimplemented calls into silent wrong behavior.
    Unsupported(funcName);
}

namespace {
std::atomic<int> g_activeCommonDialogs{0};
}

extern "C" void RegisterCommonDialogActive_nid_no_patch(bool active) {
    if (active) {
        g_activeCommonDialogs.fetch_add(1, std::memory_order_relaxed);
    } else {
        int current = g_activeCommonDialogs.load(std::memory_order_relaxed);
        while (current > 0 && !g_activeCommonDialogs.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
        }
    }
}

extern "C" bool IsAnyCommonDialogActive_nid_no_patch() {
    return g_activeCommonDialogs.load(std::memory_order_relaxed) > 0;
}

namespace {
std::mutex g_saveDirMutex;
std::string g_saveDirOverride;
}

extern "C" void SetSaveDataBaseDirOverride_nid_no_patch(const char* path) {
    std::lock_guard lock(g_saveDirMutex);
    if (path == nullptr || path[0] == '\0') {
        g_saveDirOverride.clear();
    } else {
        g_saveDirOverride = path;
    }
}

extern "C" const char* GetSaveDataBaseDirOverride_nid_no_patch() {
    std::lock_guard lock(g_saveDirMutex);
    if (g_saveDirOverride.empty()) {
        return nullptr;
    }
    return g_saveDirOverride.c_str();
}

