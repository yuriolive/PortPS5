// Runtime configuration startup for libc. Called once on the host startup thread,
// after PRX loading and before guest initializers, so config is immutable during flips.
#include "prx/libc/include/config/Config.hpp"
#include "prx/libc/include/general/LogMacros.hpp"
#include "prx/libkernel/AppMetadata/include/ParamJsonParser.hpp"

#include <exception>
#include <filesystem>

/**
 * Resolve paths from the executable, independent of the shell's working directory.
 * Reuse the metadata parser without importing libkernel (which depends on libc).
 * No exception may escape into the hand-emitted Windows entry stub.
 */
extern "C" bool PortPS5_Config_Startup_nid_no_patch(const char* executablePath) noexcept {
    try {
        if (executablePath == nullptr || *executablePath == '\0') {
            APS5_LOG_CHARS_ERR("config: executable path is missing");
            return false;
        }
        const auto installDir = std::filesystem::path(executablePath).parent_path();
        const auto metadata = parseParamJson(installDir / "app0/sce_sys/param.json");
        // The title ID becomes a filename; reject separators before loading any config.
        if (metadata.titleId.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos) {
            APS5_LOG_CHARS_ERR("param.json: invalid titleId for configuration");
            return false;
        }
        std::string error;
        std::vector<std::string> warnings;
        const bool loaded = PortPS5::Config::Loader::Initialize(
            installDir.string(), metadata.titleId, error, warnings);
        for (const auto& warning : warnings) APS5_LOG_WARN("%s", warning.c_str());
        if (!loaded) APS5_LOG_ERR("%s", error.c_str());
        return loaded;
    } catch (const std::exception& error) {
        APS5_LOG_ERR("config startup: %s", error.what());
    } catch (...) {
        APS5_LOG_CHARS_ERR("config startup: unknown failure");
    }
    return false;
}
