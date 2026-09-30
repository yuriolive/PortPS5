#include <cstdlib>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <mutex>
#include <cerrno>
#include <cstring>
#include <map>
#include <vector>
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/GuestHeap.hpp"

namespace {
struct WorkingDirectory {
    std::mutex mutex;
    const std::filesystem::path root = std::filesystem::canonical(std::filesystem::current_path());
    std::filesystem::path current = root;
    // Guest top-level mount name (no slashes, e.g. "savedata0") -> host directory.
    // Guarded by `mutex` together with the working directory.
    std::map<std::string, std::filesystem::path> mounts;
};
WorkingDirectory& Directories() { static WorkingDirectory state; return state; }

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
    const char* base = std::getenv("LOCALAPPDATA");
    if (base && *base) return std::filesystem::path(base) / "PortPS5" / "saves";
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return std::filesystem::path(xdg) / "PortPS5" / "saves";
    const char* home = std::getenv("HOME");
    if (home && *home) return std::filesystem::path(home) / ".local" / "share" / "PortPS5" / "saves";
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
