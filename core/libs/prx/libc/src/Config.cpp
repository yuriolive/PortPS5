// PortPS5 runtime configuration parser (docs/spec/configuration.md).
//
// Why libc: every prx links the libc DLL, so one parsed instance is shared
// process-wide without a new binary or cross-DLL statics. This file is the
// only place that may name the APS5_ prefix (to warn about stale variables)
// and the only place that reads the process environment for configuration;
// the policy CI job enforces both.

#include "prx/libc/include/config/Config.hpp"

#include "tomlplusplus/toml.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <set>
#include <string_view>
#include <system_error>

#ifdef _WIN32
// _environ is declared by <cstdlib> above.
#else
// Host environment block for the stale APS5_* scan (no getenv, by policy).
extern char** environ;
#endif

namespace PortPS5 {
namespace Config {
namespace {

// --- Small helpers -----------------------------------------------------------

unsigned LineOf(const toml::node* node) {
    if (node == nullptr) {
        return 1;
    }
    const unsigned line = node->source().begin.line;
    return line != 0 ? line : 1;
}

bool Fail(std::string& error, const std::string& file, const toml::node* node,
          const std::string& key, const std::string& reason) {
    error = file + ":" + std::to_string(LineOf(node)) + ": " + key + ": " + reason;
    return false;
}

bool IsAsciiEqualNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// --- Workaround registry -----------------------------------------------------

std::mutex& RegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, WorkaroundInfo>& Registry() {
    static std::map<std::string, WorkaroundInfo> registry;
    return registry;
}

// --- Loader state ------------------------------------------------------------

std::mutex& InitMutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<ResolvedConfig>& Instance() {
    static std::optional<ResolvedConfig> instance;
    return instance;
}

// --- Canonical SDL scancode names --------------------------------------------
// Why embedded: Config must reject unknown binding values without linking SDL
// (libc has no SDL dependency). The list is the key-name table from SDL's
// keyboard driver at the pinned submodule commit, matched case-insensitively
// exactly like SDL_GetScancodeFromName resolves them.

const char* const kScancodeNames[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "Return", "Escape", "Backspace", "Tab", "Space", "-", "=", "[", "]", "\\",
    "#", ";", "'", "`", ",", ".", "/", "CapsLock",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete",
    "End", "PageDown", "Right", "Left", "Down", "Up", "Numlock",
    "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter",
    "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6",
    "Keypad 7", "Keypad 8", "Keypad 9", "Keypad 0", "Keypad .",
    "Application", "Power", "Keypad =",
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    "Execute", "Help", "Menu", "Select", "Stop", "Again", "Undo", "Cut", "Copy",
    "Paste", "Find", "Mute", "VolumeUp", "VolumeDown",
    "Keypad ,", "Keypad = (AS400)",
    "AltErase", "SysReq", "Cancel", "Clear", "Prior", "Separator", "Out", "Oper",
    "Clear / Again", "CrSel", "ExSel",
    "Keypad 00", "Keypad 000", "ThousandsSeparator", "DecimalSeparator",
    "CurrencyUnit", "CurrencySubUnit",
    "Keypad (", "Keypad )", "Keypad {", "Keypad }", "Keypad Tab",
    "Keypad Backspace", "Keypad A", "Keypad B", "Keypad C", "Keypad D",
    "Keypad E", "Keypad F", "Keypad XOR", "Keypad ^", "Keypad %", "Keypad <",
    "Keypad >", "Keypad &", "Keypad &&", "Keypad |", "Keypad ||", "Keypad :",
    "Keypad #", "Keypad Space", "Keypad @", "Keypad !",
    "Keypad MemStore", "Keypad MemRecall", "Keypad MemClear", "Keypad MemAdd",
    "Keypad MemSubtract", "Keypad MemMultiply", "Keypad MemDivide", "Keypad +/-",
    "Keypad Clear", "Keypad ClearEntry", "Keypad Binary", "Keypad Octal",
    "Keypad Decimal", "Keypad Hexadecimal",
    "Left Ctrl", "Left Shift", "Left Alt", "Left GUI",
    "Right Ctrl", "Right Shift", "Right Alt", "Right GUI",
    "ModeSwitch",
    "AudioNext", "AudioPrev", "AudioStop", "AudioPlay", "AudioMute",
    "MediaSelect", "WWW", "Mail", "Calculator", "Computer",
    "AC Search", "AC Home", "AC Back", "AC Forward", "AC Stop", "AC Refresh",
    "AC Bookmarks", "BrightnessDown", "BrightnessUp", "DisplaySwitch",
    "KBDIllumToggle", "KBDIllumDown", "KBDIllumUp", "Eject", "Sleep",
    "App1", "App2", "AudioRewind", "AudioFastForward",
    "SoftLeft", "SoftRight", "Call", "EndCall",
};

// PortPS5 pseudo-names for mouse buttons and the wheel (docs/spec/input.md).
const char* const kMouseNames[] = {
    "MouseLeft", "MouseRight", "MouseMiddle", "MouseX1", "MouseX2",
    "WheelUp", "WheelDown",
};

bool IsKnownInputName(std::string_view name) {
    for (const char* known : kScancodeNames) {
        if (IsAsciiEqualNoCase(name, known)) {
            return true;
        }
    }
    for (const char* known : kMouseNames) {
        if (IsAsciiEqualNoCase(name, known)) {
            return true;
        }
    }
    return false;
}

// Binding keys: pad buttons, stick directions, touch regions and toggles.
// This vocabulary is owned here; libScePad consumes it (docs/spec/input.md).
bool IsKnownBindingKey(std::string_view key) {
    static const char* const kKeys[] = {
        "cross", "circle", "square", "triangle", "options",
        "up", "down", "left", "right",
        "l1", "r1", "l2", "r2", "l3", "r3", "touch_pad",
        "left_stick_left", "left_stick_right", "left_stick_up", "left_stick_down",
        "right_stick_left", "right_stick_right", "right_stick_up", "right_stick_down",
        "touch_left", "touch_right", "toggle_mouse", "toggle_fullscreen",
    };
    for (const char* known : kKeys) {
        if (key == known) {
            return true;
        }
    }
    return false;
}

void FillDefaultBindings(InputConfig& input) {
    // Mirrors the compile-time table in libScePad InputMapping.hpp.
    input.bindings = {
        {"toggle_fullscreen", {"F11"}},
        {"cross", {"Return", "Space"}},
        {"options", {"Escape"}},
        {"triangle", {"I"}},
        {"circle", {"C"}},
        {"l1", {"Q"}},
        {"r1", {"E", "Left Alt", "Right Alt"}},
        {"l3", {"Left Shift", "Right Shift"}},
        {"r3", {"Left Control", "Right Control"}},
        {"up", {"Up", "WheelUp"}},
        {"right", {"Right"}},
        {"down", {"Down", "WheelDown"}},
        {"left", {"Left"}},
        {"left_stick_left", {"A"}},
        {"left_stick_right", {"D"}},
        {"left_stick_up", {"W"}},
        {"left_stick_down", {"S"}},
        {"right_stick_left", {"F"}},
        {"right_stick_right", {"H"}},
        {"right_stick_up", {"T"}},
        {"right_stick_down", {"G"}},
        {"touch_left", {"Backspace"}},
        {"touch_right", {"Tab"}},
        {"toggle_mouse", {"MouseMiddle"}},
        {"square", {"MouseLeft"}},
        {"r2", {"MouseRight"}},
    };
}

// --- Scalar parsers (shared by TOML and PORTPS5_DEBUG layers) ----------------

bool ParseBoolText(std::string_view text, bool& out) {
    if (text == "true" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

bool ParseIntText(std::string_view text, std::int64_t& out) {
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

bool ParsePresentMode(std::string_view text, PresentMode& out) {
    if (text == "fifo") {
        out = PresentMode::Fifo;
        return true;
    }
    if (text == "mailbox") {
        out = PresentMode::Mailbox;
        return true;
    }
    if (text == "immediate") {
        out = PresentMode::Immediate;
        return true;
    }
    return false;
}

bool ParseLogLevel(std::string_view text, LogLevel& out) {
    if (text == "error") {
        out = LogLevel::Error;
        return true;
    }
    if (text == "warn") {
        out = LogLevel::Warn;
        return true;
    }
    if (text == "info") {
        out = LogLevel::Info;
        return true;
    }
    if (text == "debug") {
        out = LogLevel::Debug;
        return true;
    }
    return false;
}

bool ParseTraceCategory(std::string_view text, TraceCategory& out) {
    static const std::pair<std::string_view, TraceCategory> kTable[] = {
        {"audio", TraceCategory::Audio}, {"ajm", TraceCategory::Ajm},
        {"pad", TraceCategory::Pad}, {"savedata", TraceCategory::SaveData},
        {"dialog", TraceCategory::Dialog}, {"np", TraceCategory::Np},
        {"label", TraceCategory::Label}, {"sync", TraceCategory::Sync},
        {"timers", TraceCategory::Timers}, {"memory", TraceCategory::Memory},
        {"bda", TraceCategory::Bda}, {"bindless", TraceCategory::Bindless},
        {"fiber", TraceCategory::Fiber}, {"exit", TraceCategory::Exit},
    };
    for (const auto& entry : kTable) {
        if (text == entry.first) {
            out = entry.second;
            return true;
        }
    }
    return false;
}

bool ParseDumpCategory(std::string_view text, DumpCategory& out) {
    if (text == "shaders") {
        out = DumpCategory::Shaders;
        return true;
    }
    if (text == "targets") {
        out = DumpCategory::Targets;
        return true;
    }
    if (text == "textures") {
        out = DumpCategory::Textures;
        return true;
    }
    if (text == "queue") {
        out = DumpCategory::Queue;
        return true;
    }
    if (text == "rejected") {
        out = DumpCategory::Rejected;
        return true;
    }
    return false;
}

bool ParseProfileCategory(std::string_view text, ProfileCategory& out) {
    if (text == "gpu") {
        out = ProfileCategory::Gpu;
        return true;
    }
    return false;
}

bool ParseValidateCategory(std::string_view text, ValidateCategory& out) {
    if (text == "recipes") {
        out = ValidateCategory::Recipes;
        return true;
    }
    if (text == "barriers") {
        out = ValidateCategory::Barriers;
        return true;
    }
    if (text == "copies") {
        out = ValidateCategory::Copies;
        return true;
    }
    if (text == "shadows") {
        out = ValidateCategory::Shadows;
        return true;
    }
    if (text == "indirect_args") {
        out = ValidateCategory::IndirectArgs;
        return true;
    }
    return false;
}

bool ParsePipelineCacheMode(std::string_view text, PipelineCacheMode& out) {
    if (text == "on") {
        out = PipelineCacheMode::On;
        return true;
    }
    if (text == "off") {
        out = PipelineCacheMode::Off;
        return true;
    }
    if (text == "readonly") {
        out = PipelineCacheMode::ReadOnly;
        return true;
    }
    return false;
}

// --- TOML layer application --------------------------------------------------

const char* DescribeType(toml::node_type type) {
    switch (type) {
        case toml::node_type::table: return "table";
        case toml::node_type::array: return "array";
        case toml::node_type::string: return "string";
        case toml::node_type::integer: return "integer";
        case toml::node_type::floating_point: return "float";
        case toml::node_type::boolean: return "boolean";
        case toml::node_type::date:
        case toml::node_type::time:
        case toml::node_type::date_time: return "date/time";
        default: return "value";
    }
}

bool ReadString(const toml::node& node, std::string& out) {
    // Exact type match: value<T>() converts (e.g. bool to integer), which a
    // validating parser must not accept.
    if (node.type() != toml::node_type::string) {
        return false;
    }
    const std::optional<std::string> value = node.value<std::string>();
    if (!value.has_value()) {
        return false;
    }
    out = *value;
    return true;
}

bool ReadBool(const toml::node& node, bool& out) {
    if (node.type() != toml::node_type::boolean) {
        return false;
    }
    const std::optional<bool> value = node.value<bool>();
    if (!value.has_value()) {
        return false;
    }
    out = *value;
    return true;
}

bool ReadInt(const toml::node& node, std::int64_t& out) {
    if (node.type() != toml::node_type::integer) {
        return false;
    }
    const std::optional<std::int64_t> value = node.value<std::int64_t>();
    if (!value.has_value()) {
        return false;
    }
    out = *value;
    return true;
}

bool ReadDouble(const toml::node& node, double& out) {
    // Accept integers for float keys so "resolution_scale = 1" keeps working.
    if (node.type() != toml::node_type::floating_point &&
        node.type() != toml::node_type::integer) {
        return false;
    }
    const std::optional<double> floatValue = node.value<double>();
    if (floatValue.has_value()) {
        out = *floatValue;
        return true;
    }
    const std::optional<std::int64_t> intValue = node.value<std::int64_t>();
    if (intValue.has_value()) {
        out = static_cast<double>(*intValue);
        return true;
    }
    return false;
}

bool ApplyDisplay(const toml::table& table, const std::string& file, ResolvedConfig& out,
                  std::string& error) {
    static const char* const kKnown[] = {
        "resolution_scale", "present_mode", "fullscreen", "window_percent",
    };
    for (const auto& [key, node] : table) {
        const std::string name(key.str());
        bool known = false;
        for (const char* candidate : kKnown) {
            if (name == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            return Fail(error, file, &node, "display." + name, "unknown key");
        }
    }

    if (const toml::node* node = table.get("resolution_scale")) {
        double value = 0.0;
        if (!ReadDouble(*node, value)) {
            return Fail(error, file, node, "display.resolution_scale", "expected float");
        }
        if (value < 1.0 || value > 2.0) {
            return Fail(error, file, node, "display.resolution_scale",
                        "expected float in range 1.0-2.0");
        }
        out.display.resolutionScale = value;
    }
    if (const toml::node* node = table.get("present_mode")) {
        std::string text;
        if (!ReadString(*node, text)) {
            return Fail(error, file, node, "display.present_mode", "expected string");
        }
        if (!ParsePresentMode(text, out.display.presentMode)) {
            return Fail(error, file, node, "display.present_mode",
                        "expected one of: fifo, mailbox, immediate");
        }
    }
    if (const toml::node* node = table.get("fullscreen")) {
        if (!ReadBool(*node, out.display.fullscreen)) {
            return Fail(error, file, node, "display.fullscreen", "expected boolean");
        }
    }
    if (const toml::node* node = table.get("window_percent")) {
        std::int64_t value = 0;
        if (!ReadInt(*node, value)) {
            return Fail(error, file, node, "display.window_percent", "expected integer");
        }
        if (value < 25 || value > 100) {
            return Fail(error, file, node, "display.window_percent",
                        "expected integer in range 25-100");
        }
        out.display.windowPercent = static_cast<int>(value);
    }
    return true;
}

bool ApplyInput(const toml::table& table, const std::string& file, ResolvedConfig& out,
                std::string& error) {
    static const char* const kKnown[] = {
        "deadzone", "mouse_look", "mouse_sensitivity", "swap_confirm", "bindings",
    };
    for (const auto& [key, node] : table) {
        const std::string name(key.str());
        bool known = false;
        for (const char* candidate : kKnown) {
            if (name == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            return Fail(error, file, &node, "input." + name, "unknown key");
        }
    }

    if (const toml::node* node = table.get("deadzone")) {
        double value = 0.0;
        if (!ReadDouble(*node, value)) {
            return Fail(error, file, node, "input.deadzone", "expected float");
        }
        if (value < 0.0 || value > 0.5) {
            return Fail(error, file, node, "input.deadzone",
                        "expected float in range 0.0-0.5");
        }
        out.input.deadzone = value;
    }
    if (const toml::node* node = table.get("mouse_look")) {
        if (!ReadBool(*node, out.input.mouseLook)) {
            return Fail(error, file, node, "input.mouse_look", "expected boolean");
        }
    }
    if (const toml::node* node = table.get("mouse_sensitivity")) {
        double value = 0.0;
        if (!ReadDouble(*node, value)) {
            return Fail(error, file, node, "input.mouse_sensitivity", "expected float");
        }
        if (value < 0.1 || value > 10.0) {
            return Fail(error, file, node, "input.mouse_sensitivity",
                        "expected float in range 0.1-10.0");
        }
        out.input.mouseSensitivity = value;
    }
    if (const toml::node* node = table.get("swap_confirm")) {
        if (!ReadBool(*node, out.input.swapConfirm)) {
            return Fail(error, file, node, "input.swap_confirm", "expected boolean");
        }
    }
    if (const toml::node* node = table.get("bindings")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "input.bindings",
                        std::string("expected table, got ") + DescribeType(node->type()));
        }
        const toml::table& bindings = *node->as_table();
        for (const auto& [key, entry] : bindings) {
            const std::string name(key.str());
            if (!IsKnownBindingKey(name)) {
                return Fail(error, file, &entry, "input.bindings." + name,
                            "unknown binding key");
            }
            if (!entry.is_array()) {
                return Fail(error, file, &entry, "input.bindings." + name,
                            std::string("expected array of string, got ") +
                                DescribeType(entry.type()));
            }
            std::vector<std::string> names;
            for (const toml::node& item : *entry.as_array()) {
                std::string text;
                if (!ReadString(item, text)) {
                    return Fail(error, file, &item, "input.bindings." + name,
                                "expected array of string");
                }
                if (text.empty() || text.size() > 64) {
                    return Fail(error, file, &item, "input.bindings." + name,
                                "expected non-empty input name of at most 64 characters");
                }
                if (!IsKnownInputName(text)) {
                    return Fail(error, file, &item, "input.bindings." + name,
                                "unknown input name '" + text + "'");
                }
                names.push_back(text);
            }
            out.input.bindings[name] = std::move(names);
        }
    }
    return true;
}

bool ApplyDebug(const toml::table& table, const std::string& file, ResolvedConfig& out,
                std::set<std::string>& debugKeys, std::string& error) {
    static const char* const kKnown[] = {
        "log_level", "trace", "dump", "dump_dir", "profile", "validate", "watch",
        "ignore_host_input", "gpu", "memory", "recompiler", "threading",
        "pipeline_cache", "pipeline_cache_verify", "relinker",
    };
    for (const auto& [key, node] : table) {
        const std::string name(key.str());
        bool known = false;
        for (const char* candidate : kKnown) {
            if (name == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            return Fail(error, file, &node, "debug." + name, "unknown key");
        }
    }

    if (const toml::node* node = table.get("log_level")) {
        std::string text;
        if (!ReadString(*node, text) || !ParseLogLevel(text, out.debug.logLevel)) {
            return Fail(error, file, node, "debug.log_level",
                        "expected one of: error, warn, info, debug");
        }
        debugKeys.insert("log_level");
    }
    if (const toml::node* node = table.get("trace")) {
        if (!node->is_array()) {
            return Fail(error, file, node, "debug.trace", "expected array of string");
        }
        std::set<TraceCategory> trace;
        for (const toml::node& item : *node->as_array()) {
            std::string text;
            TraceCategory category = TraceCategory::Audio;
            if (!ReadString(item, text) || !ParseTraceCategory(text, category)) {
                return Fail(error, file, &item, "debug.trace",
                            "expected one of: audio, ajm, pad, savedata, dialog, np, label,"
                            " sync, timers, memory, bda, bindless, fiber, exit");
            }
            trace.insert(category);
        }
        out.debug.trace = std::move(trace);
        debugKeys.insert("trace");
    }
    if (const toml::node* node = table.get("dump")) {
        if (!node->is_array()) {
            return Fail(error, file, node, "debug.dump", "expected array of string");
        }
        std::set<DumpCategory> dump;
        for (const toml::node& item : *node->as_array()) {
            std::string text;
            DumpCategory category = DumpCategory::Shaders;
            if (!ReadString(item, text) || !ParseDumpCategory(text, category)) {
                return Fail(error, file, &item, "debug.dump",
                            "expected one of: shaders, targets, textures, queue, rejected");
            }
            dump.insert(category);
        }
        out.debug.dump = std::move(dump);
        debugKeys.insert("dump");
    }
    if (const toml::node* node = table.get("dump_dir")) {
        if (!ReadString(*node, out.debug.dumpDir) || out.debug.dumpDir.empty()) {
            return Fail(error, file, node, "debug.dump_dir", "expected non-empty string");
        }
        debugKeys.insert("dump_dir");
    }
    if (const toml::node* node = table.get("profile")) {
        if (!node->is_array()) {
            return Fail(error, file, node, "debug.profile", "expected array of string");
        }
        std::set<ProfileCategory> profile;
        for (const toml::node& item : *node->as_array()) {
            std::string text;
            ProfileCategory category = ProfileCategory::Gpu;
            if (!ReadString(item, text) || !ParseProfileCategory(text, category)) {
                return Fail(error, file, &item, "debug.profile", "expected one of: gpu");
            }
            profile.insert(category);
        }
        out.debug.profile = std::move(profile);
        debugKeys.insert("profile");
    }
    if (const toml::node* node = table.get("validate")) {
        if (!node->is_array()) {
            return Fail(error, file, node, "debug.validate", "expected array of string");
        }
        std::set<ValidateCategory> validate;
        for (const toml::node& item : *node->as_array()) {
            std::string text;
            ValidateCategory category = ValidateCategory::Recipes;
            if (!ReadString(item, text) || !ParseValidateCategory(text, category)) {
                return Fail(error, file, &item, "debug.validate",
                            "expected one of: recipes, barriers, copies, shadows, indirect_args");
            }
            validate.insert(category);
        }
        out.debug.validate = std::move(validate);
        debugKeys.insert("validate");
    }
    if (const toml::node* node = table.get("watch")) {
        if (!node->is_array()) {
            return Fail(error, file, node, "debug.watch", "expected array of tables");
        }
        std::vector<WatchEntry> watch;
        for (const toml::node& item : *node->as_array()) {
            if (!item.is_table()) {
                return Fail(error, file, &item, "debug.watch",
                            "expected array of {addr, write} tables");
            }
            const toml::table& entry = *item.as_table();
            for (const auto& [key, value] : entry) {
                const std::string name(key.str());
                if (name != "addr" && name != "write") {
                    return Fail(error, file, &value, "debug.watch", "unknown key '" + name +
                                "'; expected addr and write");
                }
            }
            const toml::node* addrNode = entry.get("addr");
            const toml::node* writeNode = entry.get("write");
            if (addrNode == nullptr || writeNode == nullptr) {
                return Fail(error, file, &item, "debug.watch",
                            "each entry needs addr and write");
            }
            std::int64_t addr = 0;
            bool write = false;
            if (!ReadInt(*addrNode, addr) || addr < 0) {
                return Fail(error, file, addrNode, "debug.watch",
                            "expected addr as non-negative integer");
            }
            if (!ReadBool(*writeNode, write)) {
                return Fail(error, file, writeNode, "debug.watch",
                            "expected write as boolean");
            }
            watch.push_back({static_cast<std::uint64_t>(addr), write});
        }
        out.debug.watch = std::move(watch);
        debugKeys.insert("watch");
    }
    if (const toml::node* node = table.get("ignore_host_input")) {
        if (!ReadBool(*node, out.debug.ignoreHostInput)) {
            return Fail(error, file, node, "debug.ignore_host_input", "expected boolean");
        }
        debugKeys.insert("ignore_host_input");
    }
    if (const toml::node* node = table.get("gpu")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug.gpu", "expected table");
        }
        const toml::table& gpu = *node->as_table();
        for (const auto& [key, value] : gpu) {
            const std::string name(key.str());
            if (name != "host_import_mib" && name != "trace" && name != "dump_frames") {
                return Fail(error, file, &value, "debug.gpu." + name, "unknown key");
            }
        }
        if (const toml::node* mib = gpu.get("host_import_mib")) {
            std::int64_t value = 0;
            if (!ReadInt(*mib, value) || value <= 0) {
                return Fail(error, file, mib, "debug.gpu.host_import_mib",
                            "expected positive integer");
            }
            out.debug.gpu.hostImportMib = static_cast<int>(value);
            debugKeys.insert("gpu.host_import_mib");
        }
        if (const toml::node* trace = gpu.get("trace")) {
            if (!ReadBool(*trace, out.debug.gpu.trace)) {
                return Fail(error, file, trace, "debug.gpu.trace", "expected boolean");
            }
            debugKeys.insert("gpu.trace");
        }
        if (const toml::node* dump = gpu.get("dump_frames")) {
            if (!ReadBool(*dump, out.debug.gpu.dumpFrames)) {
                return Fail(error, file, dump, "debug.gpu.dump_frames", "expected boolean");
            }
            debugKeys.insert("gpu.dump_frames");
        }
    }
    if (const toml::node* node = table.get("memory")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug.memory", "expected table");
        }
        const toml::table& memory = *node->as_table();
        for (const auto& [key, value] : memory) {
            const std::string name(key.str());
            if (name != "heap_cache_mib") {
                return Fail(error, file, &value, "debug.memory." + name, "unknown key");
            }
        }
        if (const toml::node* mib = memory.get("heap_cache_mib")) {
            std::int64_t value = 0;
            if (!ReadInt(*mib, value) || value <= 0) {
                return Fail(error, file, mib, "debug.memory.heap_cache_mib",
                            "expected positive integer");
            }
            out.debug.memory.heapCacheMib = static_cast<int>(value);
            debugKeys.insert("memory.heap_cache_mib");
        }
    }
    if (const toml::node* node = table.get("recompiler")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug.recompiler", "expected table");
        }
        const toml::table& recompiler = *node->as_table();
        for (const auto& [key, value] : recompiler) {
            const std::string name(key.str());
            if (name != "dump_ir" && name != "single_lane" && name != "profile" &&
                name != "capture") {
                return Fail(error, file, &value, "debug.recompiler." + name, "unknown key");
            }
        }
        if (const toml::node* dumpIr = recompiler.get("dump_ir")) {
            if (!dumpIr->is_array()) {
                return Fail(error, file, dumpIr, "debug.recompiler.dump_ir",
                            "expected array of string");
            }
            std::vector<std::string> dump;
            for (const toml::node& item : *dumpIr->as_array()) {
                std::string text;
                if (!ReadString(item, text) || text.empty()) {
                    return Fail(error, file, &item, "debug.recompiler.dump_ir",
                                "expected array of non-empty string");
                }
                dump.push_back(text);
            }
            out.debug.recompiler.dumpIr = std::move(dump);
            debugKeys.insert("recompiler.dump_ir");
        }
        if (const toml::node* singleLane = recompiler.get("single_lane")) {
            if (!ReadBool(*singleLane, out.debug.recompiler.singleLane)) {
                return Fail(error, file, singleLane, "debug.recompiler.single_lane",
                            "expected boolean");
            }
            debugKeys.insert("recompiler.single_lane");
        }
        if (const toml::node* profile = recompiler.get("profile")) {
            if (!ReadBool(*profile, out.debug.recompiler.profile)) {
                return Fail(error, file, profile, "debug.recompiler.profile",
                            "expected boolean");
            }
            debugKeys.insert("recompiler.profile");
        }
        if (const toml::node* capture = recompiler.get("capture")) {
            if (!ReadBool(*capture, out.debug.recompiler.capture)) {
                return Fail(error, file, capture, "debug.recompiler.capture",
                            "expected boolean");
            }
            debugKeys.insert("recompiler.capture");
        }
    }
    if (const toml::node* node = table.get("threading")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug.threading", "expected table");
        }
        const toml::table& threading = *node->as_table();
        for (const auto& [key, value] : threading) {
            const std::string name(key.str());
            if (name != "dump_futex_owners") {
                return Fail(error, file, &value, "debug.threading." + name, "unknown key");
            }
        }
        if (const toml::node* dump = threading.get("dump_futex_owners")) {
            if (!ReadBool(*dump, out.debug.threading.dumpFutexOwners)) {
                return Fail(error, file, dump, "debug.threading.dump_futex_owners",
                            "expected boolean");
            }
            debugKeys.insert("threading.dump_futex_owners");
        }
    }
    if (const toml::node* node = table.get("pipeline_cache")) {
        std::string text;
        if (!ReadString(*node, text) ||
            !ParsePipelineCacheMode(text, out.debug.pipelineCache)) {
            return Fail(error, file, node, "debug.pipeline_cache",
                        "expected one of: on, off, readonly");
        }
        debugKeys.insert("pipeline_cache");
    }
    if (const toml::node* node = table.get("pipeline_cache_verify")) {
        if (!ReadBool(*node, out.debug.pipelineCacheVerify)) {
            return Fail(error, file, node, "debug.pipeline_cache_verify",
                        "expected boolean");
        }
        debugKeys.insert("pipeline_cache_verify");
    }
    if (const toml::node* node = table.get("relinker")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug.relinker", "expected table");
        }
        const toml::table& relinker = *node->as_table();
        for (const auto& [key, value] : relinker) {
            const std::string name(key.str());
            if (name != "trace_sse4a") {
                return Fail(error, file, &value, "debug.relinker." + name, "unknown key");
            }
        }
        if (const toml::node* trace = relinker.get("trace_sse4a")) {
            if (!ReadBool(*trace, out.debug.relinkerTraceSse4a)) {
                return Fail(error, file, trace, "debug.relinker.trace_sse4a",
                            "expected boolean");
            }
            debugKeys.insert("relinker.trace_sse4a");
        }
    }
    return true;
}

bool ApplyWorkarounds(const toml::table& table, const std::string& file, ResolvedConfig& out,
                      std::string& error) {
    std::lock_guard lock(RegistryMutex());
    for (const auto& [key, node] : table) {
        const std::string name(key.str());
        const auto registered = Registry().find(name);
        if (registered == Registry().end()) {
            return Fail(error, file, &node, "workarounds." + name,
                        "unregistered workaround key");
        }
        const WorkaroundInfo& info = registered->second;
        switch (info.type) {
            case WorkaroundType::Bool: {
                bool value = false;
                if (!ReadBool(node, value)) {
                    return Fail(error, file, &node, "workarounds." + name,
                                "expected boolean");
                }
                out.workarounds[name] = value;
                break;
            }
            case WorkaroundType::Int: {
                std::int64_t value = 0;
                if (!ReadInt(node, value)) {
                    return Fail(error, file, &node, "workarounds." + name,
                                "expected integer");
                }
                out.workarounds[name] = value;
                break;
            }
            case WorkaroundType::Double: {
                double value = 0.0;
                if (!ReadDouble(node, value)) {
                    return Fail(error, file, &node, "workarounds." + name,
                                "expected float");
                }
                out.workarounds[name] = value;
                break;
            }
            case WorkaroundType::String: {
                std::string value;
                if (!ReadString(node, value)) {
                    return Fail(error, file, &node, "workarounds." + name,
                                "expected string");
                }
                out.workarounds[name] = value;
                break;
            }
        }
    }
    return true;
}

bool ApplyDocument(const toml::table& root, const std::string& file, bool isGlobal,
                   const std::string& expectedTitle, ResolvedConfig& out,
                   std::set<std::string>& debugKeys, std::string& error) {
    static const char* const kKnown[] = {
        "schema", "title_id", "display", "input", "workarounds", "debug",
    };
    for (const auto& [key, node] : root) {
        const std::string name(key.str());
        bool known = false;
        for (const char* candidate : kKnown) {
            if (name == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            return Fail(error, file, &node, name, "unknown key");
        }
    }

    const toml::node* schema = root.get("schema");
    if (schema == nullptr) {
        return Fail(error, file, nullptr, "schema", "missing required key 'schema = 1'");
    }
    std::int64_t schemaVersion = 0;
    if (!ReadInt(*schema, schemaVersion)) {
        return Fail(error, file, schema, "schema", "expected integer 1");
    }
    if (schemaVersion != 1) {
        return Fail(error, file, schema, "schema",
                    "unsupported schema version (runtime supports 1)");
    }

    if (const toml::node* title = root.get("title_id")) {
        std::string titleId;
        if (!ReadString(*title, titleId)) {
            return Fail(error, file, title, "title_id", "expected string");
        }
        if (isGlobal) {
            return Fail(error, file, title, "title_id",
                        "title_id is only allowed in game files");
        }
        if (titleId != expectedTitle) {
            return Fail(error, file, title, "title_id",
                        "title_id '" + titleId + "' does not match '" + expectedTitle + "'");
        }
        out.titleId = titleId;
    } else if (!isGlobal) {
        return Fail(error, file, nullptr, "title_id", "missing required key 'title_id'");
    }

    if (const toml::node* node = root.get("display")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "display", "expected table");
        }
        if (!ApplyDisplay(*node->as_table(), file, out, error)) {
            return false;
        }
    }
    if (const toml::node* node = root.get("input")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "input", "expected table");
        }
        if (!ApplyInput(*node->as_table(), file, out, error)) {
            return false;
        }
    }
    if (const toml::node* node = root.get("workarounds")) {
        if (isGlobal) {
            return Fail(error, file, node, "workarounds",
                        "workarounds are only allowed in game files");
        }
        if (!node->is_table()) {
            return Fail(error, file, node, "workarounds", "expected table");
        }
        if (!ApplyWorkarounds(*node->as_table(), file, out, error)) {
            return false;
        }
    }
    if (const toml::node* node = root.get("debug")) {
        if (!node->is_table()) {
            return Fail(error, file, node, "debug", "expected table");
        }
        if (!ApplyDebug(*node->as_table(), file, out, debugKeys, error)) {
            return false;
        }
    }
    return true;
}

// --- PORTPS5_DEBUG layer -----------------------------------------------------

std::vector<std::string> SplitOn(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

bool ApplyDebugEnvEntry(ResolvedConfig& out, std::set<std::string>& debugKeys,
                        std::string_view key, std::string_view value, std::string& error) {
    const std::string file = "PORTPS5_DEBUG";
    const toml::node* noNode = nullptr;
    const auto fail = [&](const std::string& k, const std::string& reason) {
        return Fail(error, file, noNode, k, reason);
    };

    if (key == "log_level") {
        if (!ParseLogLevel(value, out.debug.logLevel)) {
            return fail("log_level", "expected one of: error, warn, info, debug");
        }
        debugKeys.insert("log_level");
        return true;
    }
    if (key == "trace" || key == "dump" || key == "profile" || key == "validate") {
        if (value.empty()) {
            return fail(std::string(key), "expected non-empty comma-separated list");
        }
        for (const std::string& item : SplitOn(value, ',')) {
            if (key == "trace") {
                TraceCategory category = TraceCategory::Audio;
                if (!ParseTraceCategory(item, category)) {
                    return fail("trace", "unknown trace category '" + item + "'");
                }
                out.debug.trace.insert(category);
            } else if (key == "dump") {
                DumpCategory category = DumpCategory::Shaders;
                if (!ParseDumpCategory(item, category)) {
                    return fail("dump", "unknown dump category '" + item + "'");
                }
                out.debug.dump.insert(category);
            } else if (key == "profile") {
                ProfileCategory category = ProfileCategory::Gpu;
                if (!ParseProfileCategory(item, category)) {
                    return fail("profile", "unknown profile category '" + item + "'");
                }
                out.debug.profile.insert(category);
            } else {
                ValidateCategory category = ValidateCategory::Recipes;
                if (!ParseValidateCategory(item, category)) {
                    return fail("validate", "unknown validate category '" + item + "'");
                }
                out.debug.validate.insert(category);
            }
        }
        debugKeys.insert(std::string(key));
        return true;
    }
    if (key == "dump_dir") {
        if (value.empty()) {
            return fail("dump_dir", "expected non-empty string");
        }
        out.debug.dumpDir = std::string(value);
        debugKeys.insert("dump_dir");
        return true;
    }
    if (key == "watch") {
        return fail("watch", "watch cannot be set through PORTPS5_DEBUG; use a TOML file");
    }
    if (key == "ignore_host_input" || key == "gpu.trace" || key == "gpu.dump_frames" ||
        key == "recompiler.single_lane" || key == "recompiler.profile" ||
        key == "recompiler.capture" || key == "threading.dump_futex_owners" ||
        key == "pipeline_cache_verify" || key == "relinker.trace_sse4a") {
        bool parsed = false;
        if (!ParseBoolText(value, parsed)) {
            return fail(std::string(key), "expected boolean (true/false)");
        }
        if (key == "ignore_host_input") {
            out.debug.ignoreHostInput = parsed;
        } else if (key == "gpu.trace") {
            out.debug.gpu.trace = parsed;
        } else if (key == "gpu.dump_frames") {
            out.debug.gpu.dumpFrames = parsed;
        } else if (key == "recompiler.single_lane") {
            out.debug.recompiler.singleLane = parsed;
        } else if (key == "recompiler.profile") {
            out.debug.recompiler.profile = parsed;
        } else if (key == "recompiler.capture") {
            out.debug.recompiler.capture = parsed;
        } else if (key == "threading.dump_futex_owners") {
            out.debug.threading.dumpFutexOwners = parsed;
        } else if (key == "pipeline_cache_verify") {
            out.debug.pipelineCacheVerify = parsed;
        } else {
            out.debug.relinkerTraceSse4a = parsed;
        }
        debugKeys.insert(std::string(key));
        return true;
    }
    if (key == "gpu.host_import_mib" || key == "memory.heap_cache_mib") {
        std::int64_t parsed = 0;
        if (!ParseIntText(value, parsed) || parsed <= 0) {
            return fail(std::string(key), "expected positive integer");
        }
        if (key == "gpu.host_import_mib") {
            out.debug.gpu.hostImportMib = static_cast<int>(parsed);
        } else {
            out.debug.memory.heapCacheMib = static_cast<int>(parsed);
        }
        debugKeys.insert(std::string(key));
        return true;
    }
    if (key == "recompiler.dump_ir") {
        if (value.empty()) {
            return fail("recompiler.dump_ir", "expected non-empty comma-separated list");
        }
        out.debug.recompiler.dumpIr = SplitOn(value, ',');
        debugKeys.insert("recompiler.dump_ir");
        return true;
    }
    if (key == "pipeline_cache") {
        if (!ParsePipelineCacheMode(value, out.debug.pipelineCache)) {
            return fail("pipeline_cache", "expected one of: on, off, readonly");
        }
        debugKeys.insert("pipeline_cache");
        return true;
    }
    return fail(std::string(key), "unknown [debug] key");
}

bool ApplyDebugEnv(ResolvedConfig& out, std::set<std::string>& debugKeys,
                   std::string_view env, std::string& error) {
    for (const std::string& entry : SplitOn(env, ';')) {
        if (entry.empty()) {
            continue;
        }
        const std::size_t split = entry.find('=');
        if (split == std::string::npos || split == 0) {
            return Fail(error, "PORTPS5_DEBUG", nullptr, entry,
                        "expected key=value pairs separated by ';'");
        }
        if (!ApplyDebugEnvEntry(out, debugKeys, std::string_view(entry).substr(0, split),
                                std::string_view(entry).substr(split + 1), error)) {
            return false;
        }
    }
    return true;
}

// --- Environment scan (no getenv: this module owns env access) ---------------

struct HostEnv {
    const char* debugValue = nullptr;
    std::vector<std::string> staleAps5;
};

HostEnv ScanHostEnv() {
    HostEnv found;
#ifdef _WIN32
    char** block = _environ;
#else
    char** block = environ;
#endif
    if (block == nullptr) {
        return found;
    }
    for (; *block != nullptr; ++block) {
        const char* entry = *block;
        const char* separator = std::strchr(entry, '=');
        if (separator == nullptr || separator == entry) {
            continue;
        }
        const std::string_view name(entry, static_cast<std::size_t>(separator - entry));
        if (name == "PORTPS5_DEBUG") {
            found.debugValue = separator + 1;
        } else if (name.size() > 5 && name.substr(0, 5) == "APS5_") {
            found.staleAps5.emplace_back(name);
        }
    }
    return found;
}

void FinishResolve(ResolvedConfig& out, std::set<std::string>& debugKeys,
                   const HostEnv& hostEnv, std::vector<std::string>& warnings) {
    if (!hostEnv.staleAps5.empty()) {
        std::string warning = "ignored stale APS5_* environment variables:";
        for (const std::string& name : hostEnv.staleAps5) {
            warning += " " + name;
        }
        warning += " (they have no effect; use [debug] keys instead)";
        warnings.push_back(std::move(warning));
    }
    out.debugKeysSet.assign(debugKeys.begin(), debugKeys.end());
}

bool ParseFileLayer(const std::string& path, bool isGlobal, const std::string& expectedTitle,
                    ResolvedConfig& out, std::set<std::string>& debugKeys, std::string& error) {
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& parseError) {
        error = path + ":" + std::to_string(parseError.source().begin.line) + ": " +
                std::string(parseError.description());
        return false;
    } catch (const std::exception& other) {
        error = path + ":1: " + std::string(other.what());
        return false;
    }
    return ApplyDocument(root, path, isGlobal, expectedTitle, out, debugKeys, error);
}

bool ParseMemoryLayer(const std::string& text, const std::string& label, bool isGlobal,
                      const std::string& expectedTitle, ResolvedConfig& out,
                      std::set<std::string>& debugKeys, std::string& error) {
    toml::table root;
    try {
        root = toml::parse(text, label);
    } catch (const toml::parse_error& parseError) {
        error = label + ":" + std::to_string(parseError.source().begin.line) + ": " +
                std::string(parseError.description());
        return false;
    } catch (const std::exception& other) {
        error = label + ":1: " + std::string(other.what());
        return false;
    }
    return ApplyDocument(root, label, isGlobal, expectedTitle, out, debugKeys, error);
}

}  // namespace

void RegisterWorkaround(const char* key, WorkaroundType type, WorkaroundValue defaultValue,
                        const char* mechanism) {
    std::lock_guard lock(RegistryMutex());
    Registry()[key] = {type, std::move(defaultValue), mechanism != nullptr ? mechanism : ""};
}

bool Loader::Initialize(const std::string& installDir, const std::string& titleId,
                        std::string& error, std::vector<std::string>& warnings) {
    std::lock_guard lock(InitMutex());
    if (Instance().has_value()) {
        error = "config: already initialized";
        return false;
    }
    ResolvedConfig out;
    out.installDir = installDir;
    out.titleId = titleId;
    FillDefaultBindings(out.input);
    out.debug.dumpDir = installDir + "/dumps";

    const HostEnv hostEnv = ScanHostEnv();
    std::set<std::string> debugKeys;
    const std::string globalPath = installDir + "/config/global.toml";
    if (std::filesystem::exists(globalPath)) {
        if (!ParseFileLayer(globalPath, true, titleId, out, debugKeys, error)) {
            return false;
        }
    } else {
        warnings.push_back("missing " + globalPath + "; using built-in defaults");
    }
    const std::string gamePath = installDir + "/config/games/" + titleId + ".toml";
    if (!titleId.empty() && std::filesystem::exists(gamePath)) {
        if (!ParseFileLayer(gamePath, false, titleId, out, debugKeys, error)) {
            return false;
        }
    }
    if (hostEnv.debugValue != nullptr && *hostEnv.debugValue != '\0') {
        if (!ApplyDebugEnv(out, debugKeys, hostEnv.debugValue, error)) {
            return false;
        }
    }
    FinishResolve(out, debugKeys, hostEnv, warnings);
    Instance() = std::move(out);
    return true;
}

bool Loader::InitializeForTesting(const std::string& globalToml, const std::string& gameToml,
                                  const std::string& debugEnv, const std::string& installDir,
                                  const std::string& titleId, std::string& error,
                                  std::vector<std::string>& warnings) {
    std::lock_guard lock(InitMutex());
    if (Instance().has_value()) {
        error = "config: already initialized";
        return false;
    }
    ResolvedConfig out;
    out.installDir = installDir;
    out.titleId = titleId;
    FillDefaultBindings(out.input);
    out.debug.dumpDir = installDir + "/dumps";

    // Testing reads the real environment for PORTPS5_DEBUG fallback and stale
    // variables, so those paths stay covered without file fixtures.
    const HostEnv hostEnv = ScanHostEnv();
    std::set<std::string> debugKeys;
    if (!globalToml.empty()) {
        if (!ParseMemoryLayer(globalToml, "global.toml", true, titleId, out, debugKeys, error)) {
            return false;
        }
    } else {
        warnings.push_back("missing global.toml; using built-in defaults");
    }
    if (!gameToml.empty()) {
        if (!ParseMemoryLayer(gameToml, titleId + ".toml", false, titleId, out, debugKeys,
                              error)) {
            return false;
        }
    }
    const char* envText = debugEnv.empty() ? hostEnv.debugValue : debugEnv.c_str();
    if (envText != nullptr && *envText != '\0') {
        if (!ApplyDebugEnv(out, debugKeys, envText, error)) {
            return false;
        }
    }
    FinishResolve(out, debugKeys, hostEnv, warnings);
    Instance() = std::move(out);
    return true;
}

const ResolvedConfig& Loader::Get() {
    std::lock_guard lock(InitMutex());
    // Startup must Initialize once before guest threads start; reading the
    // config earlier is a bug, and aborting beats returning a half object.
    if (!Instance().has_value()) {
        std::abort();
    }
    return *Instance();
}

bool Loader::IsInitialized() {
    std::lock_guard lock(InitMutex());
    return Instance().has_value();
}

void Loader::ResetForTesting() {
    std::lock_guard lock(InitMutex());
    Instance().reset();
}

std::optional<WorkaroundValue> Loader::GetWorkaround(const std::string& key) {
    std::lock_guard initLock(InitMutex());
    std::lock_guard registryLock(RegistryMutex());
    const auto registered = Registry().find(key);
    if (registered == Registry().end()) {
        return std::nullopt;
    }
    if (Instance().has_value()) {
        const auto set = Instance()->workarounds.find(key);
        if (set != Instance()->workarounds.end()) {
            return set->second;
        }
    }
    return registered->second.defaultValue;
}

bool Loader::GetWorkaroundBool(const std::string& key, bool fallback) {
    const std::optional<WorkaroundValue> value = GetWorkaround(key);
    if (!value.has_value() || !std::holds_alternative<bool>(*value)) {
        return fallback;
    }
    return std::get<bool>(*value);
}

std::int64_t Loader::GetWorkaroundInt(const std::string& key, std::int64_t fallback) {
    const std::optional<WorkaroundValue> value = GetWorkaround(key);
    if (!value.has_value() || !std::holds_alternative<std::int64_t>(*value)) {
        return fallback;
    }
    return std::get<std::int64_t>(*value);
}

double Loader::GetWorkaroundDouble(const std::string& key, double fallback) {
    const std::optional<WorkaroundValue> value = GetWorkaround(key);
    if (!value.has_value() || !std::holds_alternative<double>(*value)) {
        return fallback;
    }
    return std::get<double>(*value);
}

std::string Loader::GetWorkaroundString(const std::string& key, const std::string& fallback) {
    const std::optional<WorkaroundValue> value = GetWorkaround(key);
    if (!value.has_value() || !std::holds_alternative<std::string>(*value)) {
        return fallback;
    }
    return std::get<std::string>(*value);
}

}  // namespace Config
}  // namespace PortPS5
