// Data model and builder for `sce_module` guest modules (prx files shipped
// beside the executable). Subsystem: relinker. GuestImage is a plain value
// owned by the builder and consumed by GuestModuleWriter; nothing here is
// shared across threads.
#ifndef RELINKER_GUESTIMAGE_HPP
#define RELINKER_GUESTIMAGE_HPP

#include <codegen/CodegenTypes.hpp>
#include <domain/Types.hpp>
#include <relinker/domain/ISyscallScanner.hpp>
#include <filesystem>

namespace Relinker {

inline constexpr char GuestModuleSuffix[] = ".guest.prx";

struct GuestSymbol {
    std::string Name;
    std::uint8_t Info;
    std::uint8_t Visibility;
    std::uint16_t Section;
    std::uint64_t Value;
    std::uint64_t Size;
};

struct GuestImage {
    std::filesystem::path SourcePath;
    std::string OutputName;
    std::vector<std::uint8_t> Bytes;
    // `--to-intel` stub sites in Bytes; the writers append the stub bodies.
    std::vector<Codegen::TrampolineSite> Trampolines;
    std::vector<Domain::ProgramHeader> Headers;
    std::vector<GuestSymbol> Symbols;
    std::vector<std::string> Dependencies;
    Domain::SysVDynamicSection Dynamic;
    std::uint64_t Init = 0;
    std::uint64_t Fini = 0;
    std::uint64_t Got = 0;
    bool UsePlatformTlsResolver = true;
};

class GuestImageReader {
public:
    GuestImage Read(const std::filesystem::path& path, std::vector<std::uint8_t> bytes) const;
};

struct GuestArtifact {
    std::filesystem::path Path;
    std::vector<std::uint8_t> Bytes;
};

class GuestModuleBuilder {
public:
    std::vector<GuestArtifact> Build(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath, Domain::SysVDynamicSection& dynamic, bool windows, bool toIntel, ISyscallScanner& syscallScanner, bool lazyBinding, const std::string& runPath) const;
};

}

#endif
