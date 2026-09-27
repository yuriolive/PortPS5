#include <relinker/analysis/UnusedNidFilter.hpp>
#include <relinker/analysis/UnusedNidFilter/IEntryPointCollector.hpp>
#include <relinker/analysis/UnusedNidFilter/IControlFlowGraph.hpp>
#include <relinker/analysis/UnusedNidFilter/IGotAccessIndex.hpp>
#include <relinker/analysis/UnusedNidFilter/IRelativeRelocationIndex.hpp>
#include <relinker/analysis/CodeMap.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <cstring>
#include <unordered_set>

namespace Relinker {

namespace {

bool _readsGotPointer(const Codegen::DecodedInstructionInfo& info) {
    if (info.IsTwoByteOpcode)
        return false;
    if (info.Opcode == 0x8A || info.Opcode == 0x8B)
        return true;
    if (info.Opcode == Codegen::X64OpcodeConstants::OneByteGrp5Rm)
        return info.ModRmRegField == 2 || info.ModRmRegField == 4;
    return false;
}

}

class CfgBackedNidFilter : public IUnusedNidFilter {
public:
    std::vector<NidReference> Filter(
        const std::vector<NidReference>& nidRefs,
        const std::vector<std::uint8_t>& elfBytes,
        const std::vector<std::uint8_t>& textSection,
        VirtualAddress textVAddr
    ) override {
        if (textSection.empty()) throw RelinkerException("Cannot filter NIDs: text section is empty");

        // Reuse the shared CodeMap reachability: GOT accesses are collected
        // from proven Starts instead of a second CFG run.
        try {
            const ElfReader reader(elfBytes);
            const auto headers = reader.ReadProgramHeaders();
            const auto codeMap = BuildCodeMap(elfBytes, headers);
            const Codegen::X64InstructionDecoder decoder;
            std::unordered_set<VirtualAddress> accessed;
            for (const auto start : codeMap.Starts) {
                const Domain::ProgramHeader* seg = nullptr;
                for (const auto& header : headers) {
                    if (header.Type != 1 || start < header.MappedAddress)
                        continue;
                    const auto offset = start - header.MappedAddress;
                    if (offset >= header.FileSize)
                        continue;
                    seg = &header;
                    break;
                }
                if (seg == nullptr)
                    continue;
                const auto fileOffset = static_cast<std::size_t>(seg->Offset + (start - seg->MappedAddress));
                const std::size_t available = elfBytes.size() - fileOffset;
                if (available == 0)
                    continue;
                Codegen::DecodedInstructionInfo info;
                try {
                    info = decoder.DecodeInstruction(elfBytes.data() + fileOffset, available);
                } catch (const Codegen::CodegenException&) {
                    continue;
                }
                if (!info.HasRipRelativeDisp || !_readsGotPointer(info))
                    continue;
                std::int32_t disp = 0;
                std::memcpy(&disp, elfBytes.data() + fileOffset + info.RipRelativeDispOffset, 4);
                const auto target = static_cast<VirtualAddress>(
                    static_cast<std::int64_t>(start + info.Length) + disp);
                accessed.insert(target);
            }
            std::vector<NidReference> result;
            for (const auto& ref : nidRefs)
                if (accessed.find(ref.RelocationAddress) != accessed.end())
                    result.push_back(ref);
            return result;
        } catch (const RelinkerException&) {
            // Fall back to the single-segment CFG path for images where
            // CodeMap cannot be built (e.g. synthetic unit fixtures).
        }

        auto collector = UnusedNidFilter::MakeEntryPointCollector();
        auto entries = collector->Collect(elfBytes, textVAddr, textSection.size());

        if (entries.empty()) throw RelinkerException("Cannot filter NIDs: no entry points found");

        VirtualAddress primary = entries[0];
        std::vector<VirtualAddress> extra(entries.begin() + 1, entries.end());

        auto relativeRelocations = UnusedNidFilter::BuildRelativeRelocationIndex(elfBytes);
        auto cfg = UnusedNidFilter::BuildControlFlowGraph(textSection, textVAddr, primary, extra, *relativeRelocations);
        auto index = UnusedNidFilter::BuildGotAccessIndex(*cfg, textSection, textVAddr);

        std::vector<NidReference> result;
        for (const auto& ref : nidRefs)
            if (index->IsGotSlotAccessed(ref.RelocationAddress))
                result.push_back(ref);
        return result;
    }
};

std::shared_ptr<IUnusedNidFilter> MakeUnusedNidFilter() {
    return std::make_shared<CfgBackedNidFilter>();
}

}
