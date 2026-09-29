// Config.hpp
// PortPS5 - Runtime Configuration System
//
// Subsystem Ownership:
//   Owned by core/libs/prx/libc. Coordinates configuration loading, parsing,
//   and query interfaces across all PortPS5 subsystems.
//
// Threading & Invariants:
//   - Config is initialized once before guest thread execution begins.
//   - All accessors are read-only and thread-safe without locks.

#ifndef CORE_LIBS_PRX_LIBC_CONFIG_HPP
#define CORE_LIBS_PRX_LIBC_CONFIG_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

// Runtime configuration for PortPS5 (docs/spec/configuration.md).
//
// A single Config module parses <install>/config/global.toml, then
// <install>/config/games/<titleId>.toml, then the PORTPS5_DEBUG variable,
// key by key. Later layers override earlier ones. Unknown keys, wrong types,
// out-of-range values and [workarounds] in the global file are start-up
// errors reported as "file:line: key: reason" before any guest code runs.
//
// This module lives in libc so every prx shares one parsed instance through
// the libc DLL. It has no guest-visible exports and takes no guest locks:
// Initialize runs once at start-up, and the getters are read-only after that.
namespace PortPS5 {
namespace Config {

enum class PresentMode { Fifo, Mailbox, Immediate };
enum class LogLevel { Error, Warn, Info, Debug };
enum class TraceCategory {
    Audio, Ajm, Pad, SaveData, Dialog, Np, Label, Sync,
    Timers, Memory, Bda, Bindless, Fiber, Exit
};
enum class DumpCategory { Shaders, Targets, Textures, Queue, Rejected };
enum class ProfileCategory { Gpu };
enum class ValidateCategory { Recipes, Barriers, Copies, Shadows, IndirectArgs };
enum class PipelineCacheMode { On, Off, ReadOnly };

struct DisplayConfig {
    double resolutionScale = 1.0;
    PresentMode presentMode = PresentMode::Fifo;
    bool fullscreen = false;
    int windowPercent = 60;
};

struct InputConfig {
    double deadzone = 0.08;
    bool mouseLook = false;
    double mouseSensitivity = 1.0;
    bool swapConfirm = false;
    // Binding key (e.g. "cross") to SDL scancode names or Mouse*/Wheel* pseudo-names.
    std::map<std::string, std::vector<std::string>> bindings;
};

struct GpuDebugConfig {
    // Empty means the automatic host-import budget (docs/spec/gpu-driver.md).
    std::optional<int> hostImportMib;
    bool trace = false;
    bool dumpFrames = false;
};

struct MemoryDebugConfig {
    // Empty means the automatically sized heap cache (docs/spec/guest-memory.md).
    std::optional<int> heapCacheMib;
};

struct RecompilerDebugConfig {
    std::vector<std::string> dumpIr;
    bool singleLane = false;
    bool profile = false;
    bool capture = false;
};

struct ThreadingDebugConfig {
    bool dumpFutexOwners = false;
};

struct WatchEntry {
    std::uint64_t address = 0;
    bool write = false;
};

struct DebugConfig {
    LogLevel logLevel = LogLevel::Info;
    std::set<TraceCategory> trace;
    std::set<DumpCategory> dump;
    std::string dumpDir;
    std::set<ProfileCategory> profile;
    std::set<ValidateCategory> validate;
    std::vector<WatchEntry> watch;
    bool ignoreHostInput = false;
    GpuDebugConfig gpu;
    MemoryDebugConfig memory;
    RecompilerDebugConfig recompiler;
    ThreadingDebugConfig threading;
    PipelineCacheMode pipelineCache = PipelineCacheMode::On;
    bool pipelineCacheVerify = false;
    bool relinkerTraceSse4a = false;
};

enum class WorkaroundType { Bool, Int, Double, String };
using WorkaroundValue = std::variant<bool, std::int64_t, double, std::string>;

struct WorkaroundInfo {
    WorkaroundType type = WorkaroundType::Bool;
    WorkaroundValue defaultValue = false;
    std::string mechanism;
};

// Registers one [workarounds] key with the schema. The key names the toggled
// mechanism, never a game, and must have a matching docs/workarounds.md entry
// (enforced by the policy CI job). Registration happens in static
// initializers, so every key is known before Initialize validates game files.
void RegisterWorkaround(const char* key, WorkaroundType type,
                        WorkaroundValue defaultValue, const char* mechanism);

// Usage: invoke the macro below with a key, a WorkaroundType, a default value
// and a mechanism string, at namespace scope; the static registrar runs before
// Initialize validates game files. (Spelled out in words: the policy job greps
// for invocations, so this comment must not contain one.)
#define PORTPS5_WORKAROUND(Key, Type, DefaultValue, Mechanism)                \
    namespace PortPS5 {                                                       \
    namespace Config {                                                        \
    namespace Detail {                                                        \
    struct WorkaroundRegistrar_##Key {                                        \
        WorkaroundRegistrar_##Key() {                                         \
            RegisterWorkaround(#Key, Type, DefaultValue, Mechanism);          \
        }                                                                     \
    };                                                                        \
    static WorkaroundRegistrar_##Key g_workaroundRegistrar_##Key;             \
    }                                                                         \
    }                                                                         \
    }

struct ResolvedConfig {
    std::string installDir;
    std::string titleId;
    DisplayConfig display;
    InputConfig input;
    // Explicitly set workaround keys and their values (results JSON workarounds_set).
    std::map<std::string, WorkaroundValue> workarounds;
    DebugConfig debug;
    // Dotted [debug] paths set by any layer, e.g. "trace", "gpu.trace".
    // Release runs must leave this empty (results JSON debug_keys_set).
    std::vector<std::string> debugKeysSet;
};

class Loader {
public:
    // Parses global.toml, then games/<titleId>.toml (skipped when titleId is
    // empty), then PORTPS5_DEBUG. Returns false with a "file:line: key: reason"
    // message in error on any config error. One stale-APS5_* warning is
    // appended to warnings when the environment still names any.
    static bool Initialize(const std::string& installDir, const std::string& titleId,
                           std::string& error, std::vector<std::string>& warnings);
    // Same as Initialize but parses TOML from memory for tests. Source labels
    // in errors are "global.toml", "<titleId>.toml" and "PORTPS5_DEBUG".
    static bool InitializeForTesting(const std::string& globalToml,
                                     const std::string& gameToml,
                                     const std::string& debugEnv,
                                     const std::string& installDir,
                                     const std::string& titleId,
                                     std::string& error,
                                     std::vector<std::string>& warnings);
    // Get returns the parsed config; aborts when Initialize has not run because start-up must precede guest threads.
    static const ResolvedConfig& Get();
    // IsInitialized reports whether Initialize has succeeded (for start-up sequencing only).
    static bool IsInitialized();
    // ResetForTesting clears the instance so tests can re-initialize with in-memory TOML.
    static void ResetForTesting();

    // Workaround accessors. Unregistered keys return nullopt (GetWorkaround)
    // or the caller fallback (typed getters, including on a type mismatch,
    // which is a programming error the policy-reviewed call sites avoid).
    // Unset keys return their registered default.
    static std::optional<WorkaroundValue> GetWorkaround(const std::string& key);
    static bool GetWorkaroundBool(const std::string& key, bool fallback);
    static std::int64_t GetWorkaroundInt(const std::string& key, std::int64_t fallback);
    static double GetWorkaroundDouble(const std::string& key, double fallback);
    static std::string GetWorkaroundString(const std::string& key, const std::string& fallback);

private:
    Loader() = delete;
};

}  // namespace Config
}  // namespace PortPS5

// Verbatim cross-prx Loader API (docs/spec/build-toolchain.md NID rule).
//
// Why free functions: `nid_patcher libc` runs without `--preserve-exports`
// (`core/libs/CMakeLists.txt`), so the mangled `Loader::` methods would hash
// in the patched DLL while other prx import the verbatim mangled names and
// the load fails with `GetLastError` 127 (`NidResolver.cpp:54-62`). These
// `extern "C"` `_nid_no_patch` names are kept verbatim, so every prx links
// them. Host-to-host calls only (never guest-reachable), hence no APS5_VABI.
// The `Get` wrapper inherits `Loader::Get` semantics: it aborts when
// `Initialize` has not run, because start-up must precede guest threads.
//
// @brief Reports whether `Loader::Initialize` has succeeded.
// @return True once a config instance is stored.
extern "C" bool PortPS5_Config_Loader_IsInitialized_nid_no_patch();
// @brief Returns the parsed config; aborts when not initialized.
// @return The process-wide parsed config owned by libc.
extern "C" const PortPS5::Config::ResolvedConfig& PortPS5_Config_Loader_Get_nid_no_patch();

#endif
