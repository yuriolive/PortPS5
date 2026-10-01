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

// Runs (and removes) the handlers registered through __cxa_atexit for `dsoHandle`, newest first. A null
// `dsoHandle` runs every registered handler, which is what process exit does. Shared with libSceLibcInternal,
// which exports __cxa_finalize under its own module name (cross-prx verbatim `_nid_no_patch` name).
extern "C" void CxaFinalize_nid_no_patch(void* dsoHandle);

// Logs and aborts for genuinely unsupported states (docs/spec/threading.md
// Error policy). Replaces the throw in NotImplemented_nid_no_patch, which the
// shared DWARF unwinder lets guest catch(...) swallow. The log names what was
// hit; the threading slice adds caller offset and thread name.
[[noreturn]] void Unsupported(const char* what);

/**
 * @brief Unchecked guest-to-host path resolver.
 * @param path NUL-terminated guest path (null throws std::invalid_argument).
 * @return Host path. A path that escapes a mount, contains ':' inside a mount, or names an
 *         unmounted reserved mount (/savedata0) yields an EMPTY path so the native call fails;
 *         use ResolveGuestPathChecked / ResolveKernelPath where the error code must surface.
 */
extern "C" std::filesystem::path ResolvePath_nid_no_patch(const char* path);
extern "C" void AddPathAlias_nid_no_patch(const char* guestPrefix, const char* hostPath);
extern "C" void RemovePathAlias_nid_no_patch(const char* guestPrefix);
extern "C" void RegisterCommonDialogActive_nid_no_patch(bool active);
extern "C" bool IsAnyCommonDialogActive_nid_no_patch();
extern "C" void SetSaveDataBaseDirOverride_nid_no_patch(const char* path);
extern "C" std::string GetSaveDataBaseDirOverride_nid_no_patch();

/** @brief Guest mount point name of the per-title save container (PRD F2): "/savedata0". */
inline constexpr const char* SaveDataMountName = "savedata0";

/**
 * @brief Result of resolving a guest path against the mount table.
 *
 * `error` is 0 on success, 13 (EACCES) when the path escapes its mount, 14 (EFAULT) for a
 * null path. `unmounted` is true when the path names a reserved mount (e.g. /savedata0)
 * that has not been mounted yet; `host` is then meaningless.
 */
struct GuestPathResult {
    std::filesystem::path host;
    int error = 0;
    bool unmounted = false;
};

/**
 * @brief Resolves a guest path and reports containment errors as codes.
 * @param path NUL-terminated guest path; null yields error 14.
 * @return GuestPathResult (see above). Thread-safe (shares the working-directory mutex).
 */
GuestPathResult ResolveGuestPathChecked(const char* path);

/**
 * @brief Mounts a host directory at a top-level guest name.
 * @param name Mount name without '/', '\\' or ':' (e.g. "savedata0").
 * @param hostDirectory Directory to expose; created if missing.
 * @return false on an invalid name or filesystem error.
 */
bool MountGuestDirectory(const char* name, const std::filesystem::path& hostDirectory);

/**
 * @brief Removes a mount created by MountGuestDirectory; unknown or null names are ignored.
 * @param name Mount name.
 */
void UnmountGuestDirectory(const char* name);

/**
 * @brief Default save root per docs/spec/save-data.md.
 * @return <LocalAppData>/PortPS5/saves, with XDG/HOME and ./savedata fallbacks on other hosts.
 */
std::filesystem::path DefaultSaveDataRoot();

/**
 * @brief Creates <saveRoot>/<titleId>/ and mounts it at /savedata0.
 * @param titleId Title id; it becomes a path component, so only [A-Za-z0-9_-] (<= 32 chars).
 * @param saveRoot Parent directory of the per-title containers.
 * @return false for an unsafe titleId or a filesystem error.
 */
bool MountSaveData(const std::string& titleId, const std::filesystem::path& saveRoot);

#define APS5_INVALID_ARG_EX throw std::invalid_argument(std::string(__func__) + ": invalid argument")

#define APS5_DUMMY_FUN \
int DummyFunction_nid_no_patch() { \
    NotImplemented_nid_no_patch(__func__); \
    return 0; \
}

#endif
