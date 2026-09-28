// Relinker command-line interface — argument struct shared by CliArgs parsing
// and the pipeline driver in main.cpp. Plain data; validation lives in ParseArgs.
#ifndef CORE_RELINKER_CLI_INCLUDE_CLI_HPP
#define CORE_RELINKER_CLI_INCLUDE_CLI_HPP

#include <string>
#include <cstdint>

namespace Cli {

struct Args {
    bool skipSyscallCheck = false;
    bool skipSceModule = false;
    bool toIntel = false;
    bool writeRegistry = false;
    bool toWindows = false;
    bool lazyBinding = false;
    bool autorun = false;
    bool windowsDiagnostics = false;
    // Opt-in GUI subsystem for the emitted PE (no console window on launch).
    bool windowsGui = false;
    std::uint32_t unusedFilterLevel = 0;
    std::string inputPath;
    std::string outputPath;
    std::string runPath = "$ORIGIN/libs";
};

Args ParseArgs(int argc, char* argv[]);

int Autorun(const std::string& absPath, bool toWindows);

}

#endif
