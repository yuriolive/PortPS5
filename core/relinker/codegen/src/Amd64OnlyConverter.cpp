#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTable.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <domain/CodeMap.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace Codegen {

namespace {

template<typename TOperation>
auto _atFileOffset(const Domain::FileByteOffset base, const TOperation& operation) {
    try {
        return operation();
    } catch (const CodegenException& e) {
        throw CodegenException(e.what(), base + e.FailureOffset);
    }
}

class Amd64OnlyConverter : public IAmd64OnlyConverter {
public:
    [[nodiscard]] ConvertResult Convert(
        std::vector<std::uint8_t> fileBytes,
        const std::vector<Domain::ProgramHeader>& codeSegments,
        const Domain::CodeMap& codeMap
    ) const override;

private:
    struct Pending {
        Domain::FileByteOffset FileOffset;
        Domain::VirtualAddress Address;
        std::size_t Length;
        Amd64OnlyMatch Substitution;
    };

    std::unique_ptr<IAmd64OnlyInstructionMatcher> _matcher = MakeAmd64OnlyInstructionMatcher();

    void _convertSegment(
        std::vector<std::uint8_t>& fileBytes,
        const Domain::ProgramHeader& ph,
        const Domain::CodeMap& codeMap,
        ConvertResult& result,
        std::vector<std::pair<std::string, Domain::FileByteOffset>>& unsupportedSites
    ) const;
};

void Amd64OnlyConverter::_convertSegment(
    std::vector<std::uint8_t>& fileBytes,
    const Domain::ProgramHeader& ph,
    const Domain::CodeMap& codeMap,
    ConvertResult& result,
    std::vector<std::pair<std::string, Domain::FileByteOffset>>& unsupportedSites
) const {
    const auto segOffset = static_cast<std::size_t>(ph.Offset);
    const auto segSize = static_cast<std::size_t>(ph.FileSize);
    if (segOffset > fileBytes.size() || segSize > fileBytes.size() - segOffset)
        throw CodegenException("Code segment exceeds the file", ph.Offset);

    const X64InstructionDecoder decoder;

    // Only proven starts are matched. Bytes in Unproven are never patched.
    std::vector<Pending> pending;
    for (const auto start : codeMap.Starts) {
        if (start < ph.MappedAddress || start - ph.MappedAddress >= ph.FileSize)
            continue;
        const auto segRel = static_cast<std::size_t>(start - ph.MappedAddress);
        const auto fileOffset = static_cast<Domain::FileByteOffset>(ph.Offset + segRel);
        const std::size_t available = segSize - segRel;
        const std::size_t length = _atFileOffset(fileOffset, [&] {
            return decoder.Decode(fileBytes.data() + segOffset + segRel, available);
        });
        auto substitution = _atFileOffset(fileOffset, [&] {
            return _matcher->Match(fileBytes.data() + segOffset + segRel, length);
        });
        if (substitution.has_value())
            pending.push_back({fileOffset, start, length, std::move(*substitution)});
    }

    // Unproven bytes are counted and logged, never patched.
    std::size_t unprovenInSegment = 0;
    for (const auto& range : codeMap.Unproven) {
        const auto segBegin = ph.MappedAddress;
        const auto segEnd = ph.MappedAddress + ph.FileSize;
        if (range.End <= segBegin || range.Begin >= segEnd)
            continue;
        const auto begin = range.Begin < segBegin ? segBegin : range.Begin;
        const auto end = range.End > segEnd ? segEnd : range.End;
        unprovenInSegment += static_cast<std::size_t>(end - begin);
        ++result.UnprovenRanges;
    }
    result.UnprovenBytes += unprovenInSegment;
    if (unprovenInSegment > 0)
        std::cout << "Intel conversion: " << unprovenInSegment << " unproven bytes in segment 0x"
                  << std::hex << ph.MappedAddress << std::dec << " left unpatched\n";

    for (const auto& item : pending) {
        const auto fileOffset = item.FileOffset;
        const auto address = item.Address;
        const auto& substitution = item.Substitution;
        const auto segRel = static_cast<std::size_t>(address - ph.MappedAddress);

        switch (substitution.Lowering) {
        case Amd64OnlyLowering::InPlace: {
            if (codeMap.BranchEntersSite(address, item.Length))
                throw CodegenException("Branch enters an AMD-only instruction", fileOffset);
            if (substitution.ReplacementBytes.size() != item.Length)
                throw CodegenException("Intel substitution changes the instruction length", fileOffset);
            // Why memcpy and not the shifting rewriter: InPlace replacements
            // are same-length by construction, so no RIP-relative fixup is needed.
            std::copy(substitution.ReplacementBytes.begin(), substitution.ReplacementBytes.end(),
                      fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset + segRel));
            ++result.ReplacedCount;
            result.Reports.push_back({substitution.InstructionName, fileOffset, item.Length,
                                      substitution.ReplacementBytes.size(), substitution.Lowering});
            break;
        }
        case Amd64OnlyLowering::Trampoline: {
            if (item.Length < Amd64OnlySubstitutionTable::kJmpRel32.Size)
                throw CodegenException("AMD-only instruction too short for a jump", fileOffset);
            if (codeMap.BranchEntersSite(address, item.Length))
                throw CodegenException("Branch enters an AMD-only instruction", fileOffset);
            const auto begin = fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset + segRel);
            result.Trampolines.push_back({
                fileOffset,
                address,
                item.Length,
                std::vector<std::uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(item.Length)),
                substitution.StubBody,
                substitution.ReturnBranchOffset
            });
            result.Reports.push_back({substitution.InstructionName, fileOffset, item.Length,
                                      substitution.StubBody.size(), substitution.Lowering});
            break;
        }
        case Amd64OnlyLowering::Residual: {
            // Why leave bytes: register forms trap at runtime via the libc
            // SSE4a emulator instead of failing the relink.
            result.Residuals.push_back({fileOffset, address, substitution.InstructionName, item.Length});
            result.Reports.push_back({substitution.InstructionName, fileOffset, item.Length,
                                      item.Length, substitution.Lowering});
            break;
        }
        case Amd64OnlyLowering::Unsupported: {
            std::ostringstream site;
            site << substitution.InstructionName << " at 0x" << std::hex << fileOffset;
            unsupportedSites.emplace_back(site.str(), fileOffset);
            result.Reports.push_back({substitution.InstructionName, fileOffset, item.Length,
                                      0, substitution.Lowering});
            break;
        }
        }
    }
}

ConvertResult Amd64OnlyConverter::Convert(
    std::vector<std::uint8_t> fileBytes,
    const std::vector<Domain::ProgramHeader>& codeSegments,
    const Domain::CodeMap& codeMap
) const {
    ConvertResult result{{}, 0, {}, {}, {}, 0, 0};
    std::vector<std::pair<std::string, Domain::FileByteOffset>> unsupportedSites;
    const auto originalSize = fileBytes.size();
    for (const auto& ph : codeSegments)
        _convertSegment(fileBytes, ph, codeMap, result, unsupportedSites);
    if (!unsupportedSites.empty()) {
        std::ostringstream message;
        message << "AMD-only instructions without Intel lowering (" << unsupportedSites.size() << " sites): ";
        for (std::size_t i = 0; i < unsupportedSites.size(); ++i) {
            if (i > 0)
                message << ", ";
            message << unsupportedSites[i].first;
        }
        throw CodegenException(message.str(), unsupportedSites.front().second);
    }
    if (fileBytes.size() != originalSize)
        throw CodegenException("Code segment size changed during Intel conversion", 0);
    result.Bytes = std::move(fileBytes);
    return result;
}

}

std::unique_ptr<IAmd64OnlyConverter> MakeAmd64OnlyConverter() {
    return std::make_unique<Amd64OnlyConverter>();
}

}
