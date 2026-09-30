// core/libs/prx/libc/include/General.hpp
// General runtime utilities, path aliasing, common dialog tracking, and unhandled stub helpers.
// Enforces System V ABI invariants and provides libc runtime primitives.

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_HPP

#include <stdexcept>
#include <filesystem>
#include <string>

#include "general/LogMacros.hpp"
#include "general/VabiMacros.hpp"
#include "general/ExportMacros.hpp"

extern "C" void NotImplemented_nid_no_patch(const char* funcName);

// Logs and aborts for genuinely unsupported states (docs/spec/threading.md
// Error policy). Replaces the throw in NotImplemented_nid_no_patch, which the
// shared DWARF unwinder lets guest catch(...) swallow. The log names what was
// hit; the threading slice adds caller offset and thread name.
[[noreturn]] void Unsupported(const char* what);

// Unchecked resolver. For a path that escapes a mount, contains ':' inside a mount,
// or names an unmounted reserved mount (/savedata0), it returns an EMPTY path (so the
// native call fails); use ResolveGuestPathChecked / ResolveKernelPath where the error
// code must surface.
extern "C" std::filesystem::path ResolvePath_nid_no_patch(const char* path);
extern "C" void AddPathAlias_nid_no_patch(const char* guestPrefix, const char* hostPath);
extern "C" void RemovePathAlias_nid_no_patch(const char* guestPrefix);
extern "C" void RegisterCommonDialogActive_nid_no_patch(bool active);
extern "C" bool IsAnyCommonDialogActive_nid_no_patch();
extern "C" void SetSaveDataBaseDirOverride_nid_no_patch(const char* path);
extern "C" std::string GetSaveDataBaseDirOverride_nid_no_patch();

// Guest mount point of the per-title save container (PRD F2).
inline constexpr const char* SaveDataMountName = "savedata0";

// Result of resolving a guest path against the mount table.
//  error     0 ok; 13 (EACCES) path escapes its mount; 14 (EFAULT) null path.
//  unmounted true when the path names a reserved mount (e.g. /savedata0) that has
//            not been mounted yet; `host` is then meaningless.
struct GuestPathResult {
    std::filesystem::path host;
    int error = 0;
    bool unmounted = false;
};

// Thread-safe (shares the working-directory mutex).
GuestPathResult ResolveGuestPathChecked(const char* path);

// Mounts `hostDirectory` (created if missing) at top-level guest name `name`
// (no separators). Returns false on an invalid name or filesystem error.
bool MountGuestDirectory(const char* name, const std::filesystem::path& hostDirectory);
void UnmountGuestDirectory(const char* name);

// <LocalAppData>/PortPS5/saves (spec save-data.md Target design), with XDG/HOME
// and ./savedata fallbacks for non-Windows hosts.
std::filesystem::path DefaultSaveDataRoot();

// Creates <saveRoot>/<titleId>/ and mounts it at /savedata0. Returns false for a
// titleId containing anything but [A-Za-z0-9_-] (it becomes a path component).
bool MountSaveData(const std::string& titleId, const std::filesystem::path& saveRoot);

#define APS5_INVALID_ARG_EX throw std::invalid_argument(std::string(__func__) + ": invalid argument")

#define APS5_DUMMY_FUN \
int DummyFunction_nid_no_patch() { \
    NotImplemented_nid_no_patch(__func__); \
    return 0; \
}

#endif
