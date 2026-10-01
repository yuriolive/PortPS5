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
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif
#include <vector>
#include <utility>
#include <optional>
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/config/Config.hpp"
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
    // Guest top-level mount name (no slashes, e.g. "savedata0") -> host directory.
    // Guarded by `mutex` together with the working directory.
    std::map<std::string, std::filesystem::path> mounts;
};
WorkingDirectory& Directories() { static WorkingDirectory state; return state; }

// Fully resolved path of an EXISTING file/directory, following symlinks and NTFS
// junctions. libstdc++'s canonical()/weakly_canonical() on MinGW does not resolve
// junctions (a test showed a junction inside the container leaking writes outside),
// so Windows asks the OS for the final path of an opened handle.
bool RealPathExisting(const std::filesystem::path& path, std::filesystem::path& out) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);  // BACKUP_SEMANTICS: allow opening directories
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::wstring buffer(512, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), VOLUME_NAME_DOS);
    if (length >= buffer.size()) {
        buffer.assign(length + 1, L'\0');
        length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), VOLUME_NAME_DOS);
    }
    CloseHandle(handle);
    if (length == 0 || length >= buffer.size()) return false;
    buffer.resize(length);
    // Strip the "\\?\" (or "\\?\UNC\" -> "\\") extended-length prefix for comparison.
    if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) buffer = L"\\\\" + buffer.substr(8);
    else if (buffer.rfind(L"\\\\?\\", 0) == 0) buffer = buffer.substr(4);
    out = std::filesystem::path(buffer);
    return true;
#else
    std::error_code error;
    out = std::filesystem::canonical(path, error);
    return !error;
#endif
}

// True when `host` (whose final component may not exist yet) stays under `root` after
// resolving links in its deepest existing ancestor. Non-existent tail components cannot
// be links, so they need no resolution.
bool StaysUnderRoot(const std::filesystem::path& host, const std::filesystem::path& root) {
    std::filesystem::path realRoot;
    if (!RealPathExisting(root, realRoot)) return false;
    std::filesystem::path ancestor = host;
    std::error_code error;
    while (!std::filesystem::exists(ancestor, error)) {
        if (!ancestor.has_relative_path() || ancestor == ancestor.parent_path()) return false;
        ancestor = ancestor.parent_path();
        error.clear();
    }
    std::filesystem::path realAncestor;
    if (!RealPathExisting(ancestor, realAncestor)) return false;
    const auto relative = realAncestor.lexically_relative(realRoot);
    return !relative.empty() && *relative.begin() != "..";
}

// Reserved mount names never fall through to <root>/<name>: an unmounted reserved
// name must fail rather than silently write next to the executable.
bool IsReservedMountName(const std::string& name) { return name == SaveDataMountName; }

// Walks `text` ('/'-separated; joined to the guest cwd when relative) and
// classifies it against the mount table. Returns true when the first surviving
// component is a mount or reserved name, filling `result`. ".." is resolved on the
// raw components BEFORE the host path is built, so a traversal out of a mount is
// reported (EACCES) instead of being lexically clamped to the guest root.
bool ResolveMounted(WorkingDirectory& state, const std::string& text, bool absolute,
                    GuestPathResult& result) {
    std::vector<std::string> stack;
    bool escaped = false;
    auto walk = [&](const std::string& source) {
        std::size_t pos = 0;
        while (pos <= source.size()) {
            std::size_t end = source.find('/', pos);
            if (end == std::string::npos) end = source.size();
            const std::string part = source.substr(pos, end - pos);
            pos = end + 1;
            if (part.empty() || part == ".") continue;
            if (part == "..") {
                // Popping the mount component itself would leave the container.
                if (stack.size() == 1 && (state.mounts.count(stack[0]) || IsReservedMountName(stack[0]))) escaped = true;
                if (!stack.empty()) stack.pop_back();
                continue;
            }
            stack.push_back(part);
        }
    };
    if (!absolute) {
        const auto relative = state.current.lexically_relative(state.root).generic_string();
        walk(relative == "." ? std::string() : relative);
    }
    walk(text);
    // A ".." that popped a mount component is a container escape even if the path
    // then re-enters elsewhere (e.g. /app0/../savedata0/../x): deny, never clamp.
    if (escaped) { result.error = 13; return true; }
    if (stack.empty()) return false;
    const auto mount = state.mounts.find(stack[0]);
    if (mount == state.mounts.end()) {
        if (!IsReservedMountName(stack[0])) return false;
        result.unmounted = true;
        return true;
    }
    // ':' would address NTFS alternate data streams / drive-relative paths.
    for (std::size_t i = 1; i < stack.size(); ++i)
        if (stack[i].find(':') != std::string::npos) escaped = true;
    if (escaped) { result.error = 13; return true; }
    std::filesystem::path host = mount->second;
    for (std::size_t i = 1; i < stack.size(); ++i) host /= stack[i];
    // Lexical checks cannot see a symlink/junction that a host user placed inside the
    // container. The deepest existing ancestor is resolved through the OS (final path of
    // an opened handle) and must stay under the real mount root.
    // Residual: a link swapped in between this check and the native open (TOCTOU);
    // guests cannot create links through the kernel API, so only a host-side actor can.
    if (!StaysUnderRoot(host, mount->second)) { result.error = 13; return true; }
    result.host = host.make_preferred();
    return true;
}

std::filesystem::path Resolve(WorkingDirectory& state, const char* path, GuestPathResult* checked = nullptr) {
    std::string text(path);
    for (auto& character : text) if (character == '\\') character = '/';
    std::filesystem::path input(text);
#ifdef _WIN32
    // Preserve the existing ability to pass explicit native drive paths.
    if (input.has_root_name()) { if (checked) checked->host = input; return input; }
#endif
    {
        GuestPathResult mounted;
        if (ResolveMounted(state, text, !text.empty() && text[0] == '/', mounted)) {
            if (checked) *checked = mounted;
            return mounted.host;
        }
    }
    auto guest = (std::filesystem::path("/") / state.current.lexically_relative(state.root));
    guest = (input.is_absolute() ? input : guest / input).lexically_normal();
    // Runtime path aliases (e.g. save-data "/_sm/<slot>") apply after the mount table.
    if (auto aliased = ResolveAlias(guest.relative_path().generic_string())) {
        if (checked) checked->host = *aliased;
        return *aliased;
    }
    auto host = (state.root / guest.relative_path()).make_preferred();
    if (checked) checked->host = host;
    return host;
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

GuestPathResult ResolveGuestPathChecked(const char* path) {
    GuestPathResult result;
    if (!path) { result.error = 14; return result; }
    auto& state = Directories();
    std::lock_guard lock(state.mutex);
    Resolve(state, path, &result);
    return result;
}

bool MountGuestDirectory(const char* name, const std::filesystem::path& hostDirectory) {
    if (!name || !*name || std::string(name).find_first_of("/\\:") != std::string::npos) return false;
    std::error_code error;
    std::filesystem::create_directories(hostDirectory, error);
    if (error) return false;
    const auto canonical = std::filesystem::canonical(hostDirectory, error);
    if (error) return false;
    auto& state = Directories();
    std::lock_guard lock(state.mutex);
    state.mounts[name] = canonical;
    return true;
}

void UnmountGuestDirectory(const char* name) {
    if (!name) return;
    auto& state = Directories();
    std::lock_guard lock(state.mutex);
    state.mounts.erase(name);
}

std::filesystem::path DefaultSaveDataRoot() {
    // docs/spec/save-data.md: %LOCALAPPDATA%/PortPS5/saves/<titleId>/.
    // Env access goes through Config (CI policy bans getenv outside it).
    if (const auto base = PortPS5::Config::HostEnvironmentValue("LOCALAPPDATA"))
        return std::filesystem::path(*base) / "PortPS5" / "saves";
    if (const auto xdg = PortPS5::Config::HostEnvironmentValue("XDG_DATA_HOME"))
        return std::filesystem::path(*xdg) / "PortPS5" / "saves";
    if (const auto home = PortPS5::Config::HostEnvironmentValue("HOME"))
        return std::filesystem::path(*home) / ".local" / "share" / "PortPS5" / "saves";
    return std::filesystem::path("savedata");
}

bool MountSaveData(const std::string& titleId, const std::filesystem::path& saveRoot) {
    // titleId becomes a path component: allow only [A-Za-z0-9_-] so a hostile
    // param.json cannot redirect the container ("..", separators, drive letters).
    if (titleId.empty() || titleId.size() > 32) return false;
    for (const char c : titleId) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return MountGuestDirectory(SaveDataMountName, saveRoot / titleId);
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

extern "C" std::string GetSaveDataBaseDirOverride_nid_no_patch() {
    std::lock_guard lock(g_saveDirMutex);
    return g_saveDirOverride;
}

