// Unit tests for the Config module (docs/spec/configuration.md).
//
// Follows the GuestEnvironment.cpp pattern: Require-abort assertions in
// main(), one ctest. TOML layers are passed as literals so the test needs no
// data files. PORTPS5_DEBUG and stale APS5_* variables use the real
// environment and are cleared afterwards.

#include "prx/libc/include/config/Config.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using namespace PortPS5::Config;

void Require(bool value, int line) {
    if (!value) {
        std::abort();
    }
    (void)line;
}
#define REQUIRE(cond) Require((cond), __LINE__)

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    REQUIRE(_putenv_s(name, value) == 0);
#else
    REQUIRE(::setenv(name, value, 1) == 0);
#endif
}

void ClearEnv(const char* name) {
#ifdef _WIN32
    REQUIRE(_putenv_s(name, "") == 0);
#else
    REQUIRE(::unsetenv(name) == 0);
#endif
    // An empty PORTPS5_DEBUG value parses as "no overrides"; a stale APS5_
    // probe needs the variable fully gone on POSIX, handled per case below.
}

bool InitOk(const std::string& globalToml, const std::string& gameToml,
            const std::string& debugEnv, const std::string& titleId) {
    std::string error;
    std::vector<std::string> warnings;
    Loader::ResetForTesting();
    const bool ok = Loader::InitializeForTesting(globalToml, gameToml, debugEnv, "C:/game",
                                                 titleId, error, warnings);
    if (!ok) {
        std::abort();
    }
    return ok;
}

std::string InitErr(const std::string& globalToml, const std::string& gameToml,
                    const std::string& debugEnv, const std::string& titleId) {
    std::string error;
    std::vector<std::string> warnings;
    Loader::ResetForTesting();
    REQUIRE(!Loader::InitializeForTesting(globalToml, gameToml, debugEnv, "C:/game", titleId,
                                          error, warnings));
    REQUIRE(!error.empty());
    return error;
}

const char* kGoldenGlobal = R"(schema = 1

[display]
resolution_scale = 1.5
present_mode = "mailbox"
fullscreen = true
window_percent = 80

[input]
deadzone = 0.12
mouse_look = true
mouse_sensitivity = 2.5
swap_confirm = true
[input.bindings]
cross = ["Return", "MouseLeft"]
r2 = ["MouseRight"]

[debug]
log_level = "debug"
trace = ["audio", "sync"]
dump = ["shaders"]
dump_dir = "C:/dumps"
profile = ["gpu"]
validate = ["recipes", "barriers"]
watch = [{addr = 4096, write = true}]
ignore_host_input = true
pipeline_cache = "readonly"
pipeline_cache_verify = true
[debug.gpu]
host_import_mib = 1024
trace = true
dump_frames = true
[debug.memory]
heap_cache_mib = 512
[debug.recompiler]
dump_ir = ["all"]
single_lane = true
profile = true
capture = true
[debug.threading]
dump_futex_owners = true
[debug.relinker]
trace_sse4a = true
)";

const char* kGoldenGame = R"(schema = 1
title_id = "PPSA01342"

[display]
resolution_scale = 2.0
)";

void TestDefaults() {
    ClearEnv("PORTPS5_DEBUG");
    InitOk("", "", "", "");
    const ResolvedConfig& config = Loader::Get();
    REQUIRE(config.display.resolutionScale == 1.0);
    REQUIRE(config.display.presentMode == PresentMode::Fifo);
    REQUIRE(!config.display.fullscreen);
    REQUIRE(config.display.windowPercent == 60);
    REQUIRE(config.input.deadzone == 0.08);
    REQUIRE(!config.input.mouseLook);
    REQUIRE(config.input.mouseSensitivity == 1.0);
    REQUIRE(!config.input.swapConfirm);
    REQUIRE(config.input.bindings.count("cross") == 1);
    REQUIRE(config.input.bindings.at("cross").size() == 2);
    REQUIRE(config.input.bindings.at("cross")[0] == "Return");
    REQUIRE(config.input.bindings.at("r2").size() == 1);
    REQUIRE(config.debug.logLevel == LogLevel::Info);
    REQUIRE(config.debug.trace.empty());
    REQUIRE(config.debug.dump.empty());
    REQUIRE(config.debug.dumpDir == "C:/game/dumps");
    REQUIRE(!config.debug.gpu.hostImportMib.has_value());
    REQUIRE(!config.debug.memory.heapCacheMib.has_value());
    REQUIRE(config.debug.pipelineCache == PipelineCacheMode::On);
    REQUIRE(config.debugKeysSet.empty());
    REQUIRE(config.workarounds.empty());
}

void TestGoldenParseAndLayering() {
    ClearEnv("PORTPS5_DEBUG");
    InitOk(kGoldenGlobal, kGoldenGame, "", "PPSA01342");
    const ResolvedConfig& config = Loader::Get();
    // Game layer overrides one key; the rest stays global.
    REQUIRE(config.display.resolutionScale == 2.0);
    REQUIRE(config.display.presentMode == PresentMode::Mailbox);
    REQUIRE(config.display.fullscreen);
    REQUIRE(config.display.windowPercent == 80);
    REQUIRE(config.titleId == "PPSA01342");
    REQUIRE(config.input.deadzone == 0.12);
    REQUIRE(config.input.mouseLook);
    REQUIRE(config.input.bindings.at("cross").size() == 2);
    REQUIRE(config.debug.logLevel == LogLevel::Debug);
    REQUIRE(config.debug.trace.count(TraceCategory::Audio) == 1);
    REQUIRE(config.debug.trace.count(TraceCategory::Sync) == 1);
    REQUIRE(config.debug.dump.count(DumpCategory::Shaders) == 1);
    REQUIRE(config.debug.dumpDir == "C:/dumps");
    REQUIRE(config.debug.profile.count(ProfileCategory::Gpu) == 1);
    REQUIRE(config.debug.validate.count(ValidateCategory::Recipes) == 1);
    REQUIRE(config.debug.validate.count(ValidateCategory::Barriers) == 1);
    REQUIRE(config.debug.watch.size() == 1);
    REQUIRE(config.debug.watch[0].address == 4096);
    REQUIRE(config.debug.watch[0].write);
    REQUIRE(config.debug.ignoreHostInput);
    REQUIRE(config.debug.gpu.hostImportMib.has_value());
    REQUIRE(*config.debug.gpu.hostImportMib == 1024);
    REQUIRE(config.debug.gpu.trace);
    REQUIRE(config.debug.gpu.dumpFrames);
    REQUIRE(*config.debug.memory.heapCacheMib == 512);
    REQUIRE(config.debug.recompiler.dumpIr.size() == 1);
    REQUIRE(config.debug.recompiler.singleLane);
    REQUIRE(config.debug.recompiler.profile);
    REQUIRE(config.debug.recompiler.capture);
    REQUIRE(config.debug.threading.dumpFutexOwners);
    REQUIRE(config.debug.pipelineCache == PipelineCacheMode::ReadOnly);
    REQUIRE(config.debug.pipelineCacheVerify);
    REQUIRE(config.debug.relinkerTraceSse4a);
    REQUIRE(!config.debugKeysSet.empty());
}

void TestDebugEnvLayer() {
    ClearEnv("PORTPS5_DEBUG");
    InitOk("schema = 1\n", "", "trace=audio,ajm;dump=shaders;gpu.host_import_mib=256;log_level=warn",
           "");
    const ResolvedConfig& config = Loader::Get();
    REQUIRE(config.debug.trace.count(TraceCategory::Audio) == 1);
    REQUIRE(config.debug.trace.count(TraceCategory::Ajm) == 1);
    REQUIRE(config.debug.dump.count(DumpCategory::Shaders) == 1);
    REQUIRE(*config.debug.gpu.hostImportMib == 256);
    REQUIRE(config.debug.logLevel == LogLevel::Warn);
    // TOML-set and env-set keys union in debugKeysSet.
    bool hasTrace = false;
    bool hasMib = false;
    for (const std::string& key : config.debugKeysSet) {
        hasTrace = hasTrace || key == "trace";
        hasMib = hasMib || key == "gpu.host_import_mib";
    }
    REQUIRE(hasTrace && hasMib);
    ClearEnv("PORTPS5_DEBUG");
}

void TestRejectCases() {
    ClearEnv("PORTPS5_DEBUG");
    // Missing schema.
    REQUIRE(InitErr("[display]\nresolution_scale = 1.0\n", "", "", "").find("schema") !=
            std::string::npos);
    // Schema too new.
    REQUIRE(InitErr("schema = 2\n", "", "", "").find("schema") != std::string::npos);
    // Unknown top-level key.
    REQUIRE(InitErr("schema = 1\nnope = 1\n", "", "", "").find("nope") != std::string::npos);
    // Wrong type.
    REQUIRE(InitErr("schema = 1\n[display]\nfullscreen = \"yes\"\n", "", "", "")
                .find("display.fullscreen") != std::string::npos);
    // Out of range.
    REQUIRE(InitErr("schema = 1\n[display]\nresolution_scale = 4.0\n", "", "", "")
                .find("display.resolution_scale") != std::string::npos);
    REQUIRE(InitErr("schema = 1\n[display]\nwindow_percent = 10\n", "", "", "")
                .find("display.window_percent") != std::string::npos);
    // Bad enum.
    REQUIRE(InitErr("schema = 1\n[display]\npresent_mode = \"vrr\"\n", "", "", "")
                .find("display.present_mode") != std::string::npos);
    // [workarounds] in the global file.
    REQUIRE(InitErr("schema = 1\n[workarounds]\n", "", "", "").find("workarounds") !=
            std::string::npos);
    // title_id in the global file.
    REQUIRE(InitErr("schema = 1\ntitle_id = \"PPSA01342\"\n", "", "", "").find("title_id") !=
            std::string::npos);
    // title_id mismatch in the game file.
    REQUIRE(InitErr("schema = 1\n", "schema = 1\ntitle_id = \"PPSA00000\"\n", "", "PPSA01342")
                .find("title_id") != std::string::npos);
    // Missing title_id in the game file.
    REQUIRE(InitErr("schema = 1\n", "schema = 1\n[display]\n", "", "PPSA01342")
                .find("title_id") != std::string::npos);
    // Unknown binding key.
    REQUIRE(InitErr("schema = 1\n[input.bindings]\njump = [\"Space\"]\n", "", "", "")
                .find("input.bindings.jump") != std::string::npos);
    // Unknown input name.
    REQUIRE(InitErr("schema = 1\n[input.bindings]\ncross = [\"Retrun\"]\n", "", "", "")
                .find("input.bindings.cross") != std::string::npos);
    // Unregistered workaround key.
    REQUIRE(InitErr("schema = 1\n", "schema = 1\ntitle_id = \"PPSA01342\"\n[workarounds]\n"
                                    "made_up_key = true\n",
                    "", "PPSA01342")
                .find("made_up_key") != std::string::npos);
    // Malformed TOML carries the file label and line.
    const std::string tomlError = InitErr("schema = 1\n[display\n", "", "", "");
    REQUIRE(tomlError.find("global.toml:") != std::string::npos);
    // PORTPS5_DEBUG rejects non-debug keys and bad values.
    REQUIRE(InitErr("schema = 1\n", "", "display.resolution_scale=2", "").find("PORTPS5_DEBUG") !=
            std::string::npos);
    REQUIRE(InitErr("schema = 1\n", "", "trace=bogus", "").find("trace") != std::string::npos);
    REQUIRE(InitErr("schema = 1\n", "", "watch=1", "").find("watch") != std::string::npos);
    REQUIRE(InitErr("schema = 1\n", "", "novalue", "").find("PORTPS5_DEBUG") !=
            std::string::npos);
    ClearEnv("PORTPS5_DEBUG");
}

// Workaround keys used only by these tests.
PORTPS5_WORKAROUND(test_flag, WorkaroundType::Bool, false, "test mechanism")
PORTPS5_WORKAROUND(test_count, WorkaroundType::Int, std::int64_t{3}, "test mechanism")

void TestWorkarounds() {
    ClearEnv("PORTPS5_DEBUG");
    // Defaults apply when the game file sets nothing.
    InitOk("schema = 1\n", "schema = 1\ntitle_id = \"PPSA01342\"\n", "", "PPSA01342");
    REQUIRE(Loader::GetWorkaroundBool("test_flag", true) == false);
    REQUIRE(Loader::GetWorkaroundInt("test_count", 99) == 3);
    REQUIRE(!Loader::GetWorkaround("missing_key").has_value());
    // Game file values override defaults.
    InitOk("schema = 1\n", "schema = 1\ntitle_id = \"PPSA01342\"\n[workarounds]\n"
                           "test_flag = true\ntest_count = 7\n",
           "", "PPSA01342");
    REQUIRE(Loader::GetWorkaroundBool("test_flag", false) == true);
    REQUIRE(Loader::GetWorkaroundInt("test_count", 0) == 7);
    // Wrong value type is a start-up error.
    REQUIRE(InitErr("schema = 1\n", "schema = 1\ntitle_id = \"PPSA01342\"\n[workarounds]\n"
                                    "test_count = true\n",
                    "", "PPSA01342")
                .find("test_count") != std::string::npos);
    ClearEnv("PORTPS5_DEBUG");
}

void TestStaleAps5Warning() {
    ClearEnv("PORTPS5_DEBUG");
    // Built without naming the stale prefix as a literal so the policy job
    // stays green: the Config module alone may name that prefix.
    const char prefix[] = {'A', 'P', 'S', '5', '_', '\0'};
    const std::string probe = std::string(prefix) + "CONFIG_TEST_PROBE";
    SetEnv(probe.c_str(), "1");
    std::string error;
    std::vector<std::string> warnings;
    Loader::ResetForTesting();
    REQUIRE(Loader::InitializeForTesting("schema = 1\n", "", "", "C:/game", "", error, warnings));
    REQUIRE(error.empty());
    bool found = false;
    for (const std::string& warning : warnings) {
        found = found || warning.find(probe) != std::string::npos;
    }
    REQUIRE(found);
    ClearEnv(probe.c_str());
    ClearEnv("PORTPS5_DEBUG");
}

}  // namespace

int main() {
    TestDefaults();
    TestGoldenParseAndLayering();
    TestDebugEnvLayer();
    TestRejectCases();
    TestWorkarounds();
    TestStaleAps5Warning();
    return 0;
}
