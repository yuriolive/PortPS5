// core/libs/prx/libc/include/General.hpp
// General runtime utilities, path aliasing, common dialog tracking, and unhandled stub helpers.
// Enforces System V ABI invariants and provides libc runtime primitives.

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_HPP

#include <stdexcept>
#include <filesystem>

#include "general/LogMacros.hpp"
#include "general/VabiMacros.hpp"
#include "general/ExportMacros.hpp"

extern "C" void NotImplemented_nid_no_patch(const char* funcName);

// Logs and aborts for genuinely unsupported states (docs/spec/threading.md
// Error policy). Replaces the throw in NotImplemented_nid_no_patch, which the
// shared DWARF unwinder lets guest catch(...) swallow. The log names what was
// hit; the threading slice adds caller offset and thread name.
[[noreturn]] void Unsupported(const char* what);

extern "C" std::filesystem::path ResolvePath_nid_no_patch(const char* path);
extern "C" void AddPathAlias_nid_no_patch(const char* guestPrefix, const char* hostPath);
extern "C" void RemovePathAlias_nid_no_patch(const char* guestPrefix);
extern "C" void RegisterCommonDialogActive_nid_no_patch(bool active);
extern "C" bool IsAnyCommonDialogActive_nid_no_patch();
extern "C" void SetSaveDataBaseDirOverride_nid_no_patch(const char* path);
extern "C" std::string GetSaveDataBaseDirOverride_nid_no_patch();

#define APS5_INVALID_ARG_EX throw std::invalid_argument(std::string(__func__) + ": invalid argument")

#define APS5_DUMMY_FUN \
int DummyFunction_nid_no_patch() { \
    NotImplemented_nid_no_patch(__func__); \
    return 0; \
}

#endif
