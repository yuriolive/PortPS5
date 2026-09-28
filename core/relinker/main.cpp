// Relinker driver — host CLI tool converting decrypted guest ELFs to PE images.
// Parses args, runs the pipeline, reports NID/conversion stats. Host-only;
// usage errors throw, conversion errors return Domain::RelinkerException text.
#include <Cli.hpp>
#include <domain/Types.hpp>
#include <io/FileReader.hpp>
#include <io/FileWriter.hpp>
#include <elfpatcher/linux/LinuxElfPatcher.hpp>
#include <elfpatcher/general/SegmentFilter.hpp>
#include <elfpatcher/general/EntryStubBuilder.hpp>
#include <elfpatcher/general/ProgramHeaderLayoutBuilder.hpp>
#include <elfpatcher/general/SectionHeaderTableBuilder.hpp>
#include <elfpatcher/windows/WindowsElfPatcher.hpp>
#include <io/ByteWriter.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/analysis/ValidationPolicy.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <relinker/analysis/CallSiteResolver.hpp>
#include <relinker/analysis/UnusedNidFilter.hpp>
#include <relinker/analysis/CodeMap.hpp>
#include <relinker/output/SysVDynamicSectionBuilder.hpp>
#include <relinker/output/CallRegistryWriter.hpp>
#include <relinker/output/ConversionReport.hpp>
#include <relinker/pipeline/RelinkerPipeline.hpp>
#include <relinker/guest/GuestImage.hpp>
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/CodegenException.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(const int argc, char* argv[]) {
    Cli::Args args;
    try {
        args = Cli::ParseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }

    try {
        Io::FileReader fileReader;
        Io::FileWriter fileWriter;

        auto sourceBytes = fileReader.Read(args.inputPath);
        const std::string absPath = std::filesystem::absolute(args.outputPath).string();

        // Shared CodeMap is the only instruction-discovery engine. It is
        // built once per image from CodeInstructionCollector and reused by
        // --to-intel, the syscall scan and TLS/NID filtering.
        Domain::CodeMap codeMap;
        bool haveCodeMap = false;
        const auto ensureCodeMap = [&](const std::vector<std::uint8_t>& bytes) -> const Domain::CodeMap& {
            if (!haveCodeMap) {
                const Relinker::ElfReader reader(bytes);
                codeMap = Relinker::BuildCodeMap(bytes, reader.ReadProgramHeaders());
                haveCodeMap = true;
            }
            return codeMap;
        };

        std::vector<Codegen::TrampolineSite> trampolines;
        std::vector<Codegen::ResidualSite> residuals;
        std::size_t inPlaceCount = 0;
        std::size_t unprovenBytes = 0;

        if (args.toIntel) {
            std::cout << "Mode: Intel instruction conversion; system unchanged; unused-filter=" << args.unusedFilterLevel << " (not applied)\n";

            const Relinker::ElfReader elfReader(sourceBytes);
            const auto converter = Codegen::MakeAmd64OnlyConverter();

            auto codeSegments = elfReader.ReadCodeSegments();
            const auto& map = ensureCodeMap(sourceBytes);
            auto converted = converter->Convert(std::move(sourceBytes), codeSegments, map);

            sourceBytes = std::move(converted.Bytes);
            trampolines = std::move(converted.Trampolines);
            residuals = std::move(converted.Residuals);
            inPlaceCount = converted.ReplacedCount;
            unprovenBytes = converted.UnprovenBytes;
            haveCodeMap = false;

            for (const auto& report : converted.Reports) {
                const char* kind = report.Lowering == Codegen::Amd64OnlyLowering::InPlace ? "in place " :
                    report.Lowering == Codegen::Amd64OnlyLowering::Trampoline ? "stub " :
                    report.Lowering == Codegen::Amd64OnlyLowering::Residual ? "residual " : "unsupported ";
                std::cout << "Intel substitution: " << report.InstructionName << " at 0x" << std::hex << report.Offset << std::dec
                          << " (" << report.OriginalLength << " bytes) -> " << kind << report.ReplacementLength << " bytes\n";
            }
            std::cout << "Intel conversion: " << inPlaceCount << " in place, " << trampolines.size()
                      << " stubs, " << residuals.size() << " residual\n";
            for (const auto& site : residuals)
                std::cout << "Intel residual: " << site.Mnemonic << " at 0x" << std::hex << site.FileOffset << std::dec
                          << " left for the runtime trap\n";
        }

        auto elfReader = std::make_shared<Relinker::ElfReader>(sourceBytes);
        const std::shared_ptr<Relinker::ISyscallScanner> syscallScanner = args.skipSyscallCheck ? Relinker::MakeNullSyscallScanner() : Relinker::MakeSyscallScanner();

        const auto pipeline = std::make_shared<Relinker::RelinkerPipeline>(
            elfReader,
            syscallScanner,
            Relinker::MakeCallSiteResolver(),
            std::make_shared<Relinker::ValidationPolicy>(),
            std::make_shared<Relinker::SysVDynamicSectionBuilder>(),
            args.unusedFilterLevel == 2 ? Relinker::MakeStrictUnusedNidFilter() : Relinker::MakeUnusedNidFilter(),
            args.unusedFilterLevel
        );

        std::cout << "System: " << (args.toWindows ? "Windows" : "Linux") << "; unused-filter=" << args.unusedFilterLevel << "\n";
        std::cout << "sce_module/sce_modules processing: " << (args.skipSceModule ? "disabled (--skip-sce-module)" : "enabled") << '\n';
        auto result = pipeline->Relink(sourceBytes);
        for (const auto& patch : result.Patches) {
            if (patch.Offset > sourceBytes.size() || patch.Bytes.size() > sourceBytes.size() - patch.Offset)
                throw Domain::RelinkerException("Relinker patch exceeds source image", patch.Offset);
            for (std::size_t index = 0; index < patch.Bytes.size(); ++index) sourceBytes[patch.Offset + index] = patch.Bytes[index];
        }

        std::vector<Relinker::GuestArtifact> guestArtifacts;
        if (!args.skipSceModule) {
            guestArtifacts = Relinker::GuestModuleBuilder().Build(args.inputPath, absPath, result.DynamicSection, args.toWindows, args.toIntel, *syscallScanner, args.lazyBinding, args.runPath);
        }

        const std::filesystem::path outFsPath(absPath);
        if (args.writeRegistry) {
            const std::string registryPath = (outFsPath.parent_path() / (outFsPath.stem().string() + ".registry.json")).string();
            fileWriter.Write(registryPath, std::make_shared<Relinker::CallRegistryWriter>()->WriteCallRegistry(result.RegistryEntries));
        }

        // Conversion report feeds the M1 import inventory: NIDs in/out,
        // stub count and residual sites for the runtime trap.
        Relinker::ConversionReport report;
        report.NidsIn = result.NidsIn;
        report.NidsOut = result.NidsOut;
        report.InPlaceCount = inPlaceCount;
        report.StubCount = trampolines.size();
        report.Residuals = residuals;
        report.UnprovenBytes = unprovenBytes;
        const std::string conversionPath = (outFsPath.parent_path() / (outFsPath.stem().string() + ".conversion.json")).string();
        fileWriter.Write(conversionPath, Relinker::ConversionReportWriter().Write(report));
        std::cout << "Conversion: NIDs " << report.NidsIn << " -> " << report.NidsOut
                  << "; stubs " << report.StubCount << "; residual " << report.Residuals.size() << '\n';

        auto byteWriter = std::make_shared<Io::ByteWriter>();

        std::shared_ptr<Elfpatcher::IElfPatcher> patcher;
        if (args.toWindows) {
            patcher = std::make_shared<Elfpatcher::Windows::WindowsPePatcher>(args.windowsGui);
        } else {
            patcher = std::make_shared<Elfpatcher::Linux::LinuxElfPatcher>(
                std::make_shared<Elfpatcher::EntryStubBuilder>(),
                std::make_shared<Elfpatcher::ProgramHeaderLayoutBuilder>(
                    std::make_shared<Elfpatcher::SegmentFilter>(),
                    byteWriter
                ),
                std::make_shared<Elfpatcher::SectionHeaderTableBuilder>(byteWriter),
                byteWriter
            );
        }

        const auto executableBytes = patcher->Patch(sourceBytes, result.OriginalHeaders, result.DynamicSection, result.OriginalPltGotVaddr, args.runPath, args.lazyBinding, args.windowsDiagnostics, trampolines);
        for (const auto& artifact : guestArtifacts) {
            std::filesystem::create_directories(artifact.Path.parent_path());
            fileWriter.Write(artifact.Path.string(), artifact.Bytes);
            std::cout << "Guest module: " << artifact.Path.string() << '\n';
        }
        fileWriter.Write(absPath, executableBytes);
        std::cout << "External prx references: " << result.RegistryEntries.size() << "\nOutput file: " << absPath << '\n';

        if (args.autorun) return Cli::Autorun(absPath, args.toWindows);

    } catch (const Domain::RelinkerException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        return 2;
    } catch (const Codegen::CodegenException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
