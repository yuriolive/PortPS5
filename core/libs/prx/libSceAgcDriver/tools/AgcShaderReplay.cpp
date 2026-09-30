// core/libs/prx/libSceAgcDriver/tools/AgcShaderReplay.cpp
// Offline replayer for serialized shader compile requests (M1 `recompiler-golden` gate).
// Reads RequestSerializer base64 payloads (`.req` files: local-only game-derived captures
// or the checked-in synthetic corpus), recompiles each with the shader recompiler, and
// optionally prints RDNA disassembly (`--dis`), SPIR-V assembly (`--asm`) or the guest
// memory snapshot summary (`--mem`). `--golden <dir>` replays every `.req` in a directory
// and diffs disassembled SPIR-V against the sibling `.spvasm` goldens; `--update-goldens`
// rewrites those goldens from current output (deliberate regeneration only, e.g. after a
// SPIRV-Tools bump). `--dump-corpus <dir>` writes the synthetic corpus as `.req` files.
// Differences from upstream PR #5 ShaderReplay: no APS5_* environment switches (the
// policy job bans them; diagnostics are explicit flags), failures return codes instead of
// throwing across boundaries, and validation rides on Recompile (spirv-val pre/post-opt
// when SPIRV-Tools are enabled). Host-only dev tool: built for dev/ci presets, never
// shipped. Exit codes: 0 all requests replayed, 1 usage error, 2 replay/diff failure.
// Subsystem owner: shader recompiler. Single-threaded; no guest interaction.
#include "Recompiler.hpp"
#include "ControlFlow/RequestSerializer.hpp"
#include "RdnaDecoder/RdnaInstructionDecoder.hpp"
#include "SyntheticCorpus.hpp"
#if ANYPS5_ENABLE_SPIRV_TOOLS
#include <spirv-tools/libspirv.hpp>
#endif

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using ShaderRecompiler::DeserializedRequest;
using ShaderRecompiler::Golden::AllSyntheticCases;
using ShaderRecompiler::Golden::MakeRequest;
using ShaderRecompiler::Recompile;
using ShaderRecompiler::RecompileRequest;
using ShaderRecompiler::RecompileResult;
using ShaderRecompiler::RequestSerializer;

struct Options {
    bool dis = false;
    bool asmOut = false;
    bool mem = false;
    bool updateGoldens = false;
    std::string goldenDir;
    std::string dumpCorpusDir;
    std::vector<std::string> inputs;
};

int Usage(const char* program) {
    std::cerr << "usage: " << program << " [--dis] [--asm] [--mem] <request.req>...\n"
              << "       " << program << " --golden <dir> [--update-goldens]\n"
              << "       " << program << " --dump-corpus <dir>\n";
    return 1;
}

// First line of a Recompile failure; the serialized request trailer is skipped so golden
// logs stay readable (same truncation the shader-memory test uses for its diagnostics).
std::string ShortFailure(const std::string& what) {
    const auto pos = what.find("RecompileRequest:");
    return pos == std::string::npos ? what : what.substr(0, pos);
}

std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    if (!file.good() && !file.eof()) {
        throw std::runtime_error("failed to read file: " + path);
    }
    return stream.str();
}

void WriteFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to open file for writing: " + path);
    }
    file << text;
    // Flush and close before checking: operator<< buffers, so a delayed write error
    // (e.g. disk full) surfaces only here. Without this, --update-goldens would print
    // `updated` and count a truncated .spvasm as passed. close() sets failbit on
    // failure, so the existing state check catches it.
    file.flush();
    file.close();
    if (!file) {
        throw std::runtime_error("failed to write file: " + path);
    }
}

std::string TrimTrailingSpace(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

#if ANYPS5_ENABLE_SPIRV_TOOLS
// Disassembles validated SPIR-V for golden diffing. No header: the magic/version/ids are
// covered by validation, and omitting them keeps diffs focused on emitted code.
std::string DisassembleSpirv(const std::vector<std::uint32_t>& spirv) {
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
    if (!tools.IsValid()) {
        throw std::runtime_error("agc_shader_replay: cannot create SPIR-V disassembler");
    }
    std::string text;
    if (!tools.Disassemble(spirv, &text, SPV_BINARY_TO_TEXT_OPTION_NO_HEADER | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES)) {
        throw std::runtime_error("agc_shader_replay: SPIR-V disassembly failed");
    }
    return text;
}
#endif

void PrintMemorySummary(const RecompileRequest& request) {
    std::cout << "userData dwords: " << request.context.userData.size() << " (base s" << request.context.userDataBaseRegister << ")\n";
    std::cout << "waveSize: " << request.context.waveSize << "\n";
    std::cout << "memory regions: " << request.context.memory.size() << "\n";
    for (const auto& region : request.context.memory) {
        std::cout << "  addr=0x" << std::hex << region.guestAddress << std::dec << " size=" << region.bytes.size() << "\n";
    }
}

// Replays one deserialized request: fresh full recompile (cache disabled) so golden runs
// never pass from a warm in-memory cache. Returns the compiled result for --asm/--golden.
RecompileResult ReplayRequest(RecompileRequest request) {
    request.useCache = false;
    return Recompile(request);
}

int ReplayFile(const std::string& path, const Options& options) {
    try {
        const std::string payload = TrimTrailingSpace(ReadFile(path));
        if (payload.empty()) {
            throw std::runtime_error("payload is empty");
        }
        RequestSerializer serializer;
        DeserializedRequest deserialized = serializer.Deserialize(payload);
        const RecompileRequest& request = deserialized.request;
        std::cout << "== " << path << ": code words=" << request.shader.code.size() << " stage=" << static_cast<int>(request.shader.stage) << "\n";
        if (options.mem) {
            PrintMemorySummary(request);
        }
        if (options.dis) {
            ShaderRecompiler::RdnaInstructionDecoder decoder;
            std::cout << ShaderRecompiler::RdnaProgramToString(decoder.Decode(request.shader.code));
        }
        const RecompileResult result = ReplayRequest(request);
        std::cout << "recompiled: " << result.spirv.size() << " SPIR-V words, " << result.bindings.size() << " bindings\n";
#if ANYPS5_ENABLE_SPIRV_TOOLS
        if (options.asmOut) {
            std::cout << DisassembleSpirv(result.spirv);
        }
#else
        if (options.asmOut) {
            throw std::runtime_error("--asm needs a SPIRV-Tools build (dev/ci preset)");
        }
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << path << ": " << ShortFailure(e.what()) << "\n";
        return 2;
    }
}

int RunGolden(const Options& options) {
#if !ANYPS5_ENABLE_SPIRV_TOOLS
    std::cerr << "--golden needs a SPIRV-Tools build (dev/ci preset)\n";
    return 1;
#else
    std::error_code ec;
    if (!fs::is_directory(options.goldenDir, ec)) {
        std::cerr << "--golden is not a directory: " << options.goldenDir << "\n";
        return 1;
    }
    std::uint32_t passed = 0;
    std::uint32_t failed = 0;
    // The traversal itself runs under no-throw error_code overloads plus a catch-all:
    // a directory_iterator increment or status query that throws (removed directory,
    // permission change mid-iteration) must surface as the documented exit code 2,
    // never as an uncaught filesystem_error escaping main.
    try {
        const fs::directory_iterator end;
        // NOTE: a failing increment(ec) leaves the iterator equal to end, so `it != end`
        // alone would terminate the loop before the body reports ec. The `|| ec` keeps
        // the failure branch reachable; each successful filesystem op clears ec, so no
        // stale error can leak across iterations.
        for (fs::directory_iterator it(options.goldenDir, ec); it != end || ec; it.increment(ec)) {
            if (ec) {
                std::cerr << "FAILED traversal of " << options.goldenDir << ": " << ec.message() << "\n";
                ++failed;
                break;
            }
            std::error_code fileEc;
            const bool isReq = it->is_regular_file(fileEc) && !fileEc && it->path().extension() == ".req";
            if (!isReq) {
                continue;
            }
            const std::string reqPath = it->path().string();
            fs::path goldenPath = it->path();
            goldenPath.replace_extension(".spvasm");
            try {
                const std::string payload = TrimTrailingSpace(ReadFile(reqPath));
                RequestSerializer serializer;
                DeserializedRequest deserialized = serializer.Deserialize(payload);
                const RecompileResult result = ReplayRequest(deserialized.request);
                const std::string disasm = DisassembleSpirv(result.spirv);
                if (options.updateGoldens) {
                    WriteFile(goldenPath.string(), disasm);
                    std::cout << "updated " << goldenPath.string() << "\n";
                    ++passed;
                    continue;
                }
                // Golden files may check out with CRLF line endings on Windows while the
                // disassembler always emits LF; normalize before comparing so identical
                // content compares equal on every checkout style.
                std::string expected = ReadFile(goldenPath.string());
                expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
                if (disasm != expected) {
                    std::cerr << "MISMATCH " << reqPath << " (golden " << goldenPath.string() << " differs; rerun with --update-goldens after review)\n";
                    ++failed;
                    continue;
                }
                std::cout << "ok " << reqPath << " (" << result.spirv.size() << " words)\n";
                ++passed;
            } catch (const std::exception& e) {
                std::cerr << "FAILED " << reqPath << ": " << ShortFailure(e.what()) << "\n";
                ++failed;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "FAILED traversal of " << options.goldenDir << ": " << e.what() << "\n";
        ++failed;
    }
    std::cout << "golden: " << passed << " passed, " << failed << " failed\n";
    // An empty corpus dir replays nothing: success requires at least one request, so CI
    // can never pass vacuously on a missing or wiped corpus.
    if (passed == 0u) {
        std::cerr << "FAILED: no .req requests replayed in " << options.goldenDir << "\n";
        return 2;
    }
    return failed == 0u ? 0 : 2;
#endif
}

int RunDumpCorpus(const Options& options) {
    try {
        std::error_code ec;
        fs::create_directories(options.dumpCorpusDir, ec);
        if (ec) {
            throw std::runtime_error("failed to create directory: " + options.dumpCorpusDir);
        }
        RequestSerializer serializer;
        for (const auto& testCase : AllSyntheticCases()) {
            for (const std::uint32_t waveSize : {32u, 64u}) {
                auto owned = MakeRequest(testCase, waveSize, waveSize);
                const std::string name = std::string(testCase.name) + ".wave" + std::to_string(waveSize);
                const std::string out = (fs::path(options.dumpCorpusDir) / (name + ".req")).string();
                WriteFile(out, serializer.Serialize(owned.request));
                std::cout << "wrote " << out << "\n";
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED --dump-corpus: " << e.what() << "\n";
        return 2;
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--dis") {
            options.dis = true;
        } else if (arg == "--asm") {
            options.asmOut = true;
        } else if (arg == "--mem") {
            options.mem = true;
        } else if (arg == "--update-goldens") {
            options.updateGoldens = true;
        } else if (arg == "--golden") {
            if (++i >= argc) {
                return Usage(argv[0]);
            }
            options.goldenDir = argv[i];
        } else if (arg == "--dump-corpus") {
            if (++i >= argc) {
                return Usage(argv[0]);
            }
            options.dumpCorpusDir = argv[i];
        } else if (arg == "--help" || arg == "-h") {
            return Usage(argv[0]);
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown flag: " << arg << "\n";
            return Usage(argv[0]);
        } else {
            options.inputs.emplace_back(arg);
        }
    }
    if (!options.dumpCorpusDir.empty()) {
        if (!options.goldenDir.empty() || !options.inputs.empty() || options.updateGoldens) {
            return Usage(argv[0]);
        }
        return RunDumpCorpus(options);
    }
    if (!options.goldenDir.empty()) {
        if (!options.inputs.empty()) {
            return Usage(argv[0]);
        }
        return RunGolden(options);
    }
    if (options.updateGoldens) {
        std::cerr << "--update-goldens needs --golden <dir>\n";
        return Usage(argv[0]);
    }
    if (options.inputs.empty()) {
        return Usage(argv[0]);
    }
    int worst = 0;
    for (const auto& input : options.inputs) {
        const int status = ReplayFile(input, options);
        if (status != 0) {
            worst = status;
        }
    }
    return worst;
}
