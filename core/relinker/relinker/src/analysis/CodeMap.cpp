#include <relinker/analysis/CodeMap.hpp>
#include <relinker/analysis/CodeInstructionCollector.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/CodegenException.hpp>

namespace Relinker {

Domain::CodeMap BuildCodeMap(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Domain::ProgramHeader>& headers) {
    const CodeInstructionCollector collector;
    const auto collected = collector.CollectDetailed(bytes, headers);

    Domain::CodeMap map;
    map.Starts = collected.Starts;
    map.Functions = collected.Functions;

    const Codegen::X64InstructionDecoder decoder;

    const auto fileOffsetOf = [&](Domain::VirtualAddress address, std::size_t size) -> std::size_t {
        for (const auto& header : headers) {
            if (header.Type != 1 || address < header.MappedAddress)
                continue;
            const auto offset = address - header.MappedAddress;
            if (offset >= header.FileSize || size > header.FileSize - offset)
                continue;
            return static_cast<std::size_t>(header.Offset + offset);
        }
        throw Domain::RelinkerException("CodeMap: unmapped code address", address);
    };

    // Direct branch targets from proven starts. Relocation roots are
    // already in Starts (collector seeds from RELA/init arrays), so they
    // are covered as potential indirect targets via Functions/Starts.
    for (const auto start : map.Starts) {
        const auto fileOffset = fileOffsetOf(start, 1);
        // Length is bounded by the executable segment; decode up to 15 bytes
        // (max x86-64 length) clamped to the file.
        const std::size_t available = bytes.size() - fileOffset;
        Codegen::DecodedInstructionInfo info;
        try {
            info = decoder.DecodeInstruction(bytes.data() + fileOffset, available);
        } catch (const Codegen::CodegenException&) {
            continue;
        }
        if (!info.HasBranchTarget || info.HasRipRelativeDisp)
            continue;
        const auto target = static_cast<std::int64_t>(start + info.Length) + info.BranchDisp;
        if (target < 0)
            continue;
        const auto targetAddr = static_cast<Domain::VirtualAddress>(target);
        // Only record targets inside executable segments.
        for (const auto& header : headers) {
            if (header.Type != 1 || (header.Flags & 1) == 0)
                continue;
            if (targetAddr >= header.MappedAddress && targetAddr - header.MappedAddress < header.FileSize) {
                map.BranchTargets.insert(targetAddr);
                break;
            }
        }
    }

    // Roots themselves are valid indirect targets (relocation/init entries).
    for (const auto& [begin, end] : map.Functions) {
        (void)end;
        map.BranchTargets.insert(begin);
    }

    // Unproven: executable file-backed bytes not covered by [start, start+len).
    struct Covered {
        Domain::VirtualAddress Begin;
        Domain::VirtualAddress End;
    };
    std::vector<Covered> covered;
    covered.reserve(map.Starts.size());
    for (const auto start : map.Starts) {
        const auto fileOffset = fileOffsetOf(start, 1);
        const std::size_t available = bytes.size() - fileOffset;
        std::size_t length = 0;
        try {
            length = decoder.Decode(bytes.data() + fileOffset, available);
        } catch (const Codegen::CodegenException&) {
            continue;
        }
        if (length == 0)
            continue;
        covered.push_back({start, start + length});
    }

    for (const auto& header : headers) {
        if (header.Type != 1 || (header.Flags & 1) == 0 || header.FileSize == 0)
            continue;
        const auto segBegin = header.MappedAddress;
        const auto segEnd = header.MappedAddress + header.FileSize;
        Domain::VirtualAddress cursor = segBegin;
        // Covered intervals are sorted because Starts is ordered.
        for (const auto& interval : covered) {
            if (interval.End <= segBegin || interval.Begin >= segEnd)
                continue;
            const auto begin = interval.Begin < segBegin ? segBegin : interval.Begin;
            const auto end = interval.End > segEnd ? segEnd : interval.End;
            if (cursor < begin)
                map.Unproven.push_back({cursor, begin});
            if (cursor < end)
                cursor = end;
        }
        if (cursor < segEnd)
            map.Unproven.push_back({cursor, segEnd});
    }

    return map;
}

}
