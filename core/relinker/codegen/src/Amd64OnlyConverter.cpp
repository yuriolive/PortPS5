// Rewrites AMD-only instructions at proven instruction starts for `--to-intel`.
// Subsystem: relinker codegen. Stateless between calls (const, thread-safe);
// reads the executable segments of one image plus its shared Domain::CodeMap
// and returns the patched bytes, the stub sites and a report. Bytes outside
// proven starts are never patched. Same-length rewrites happen in place; the
// rest become a `jmp rel32` into a stub that the PE or ELF writer appends.
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTable.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <domain/CodeMap.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

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

    // Result of extending a too-short trampoline site over following code.
    struct Absorption {
        bool Ok = false;
        // Why the site could not be extended; set when Ok is false.
        std::string Reason;
        std::size_t SiteLength = 0;
        Amd64OnlyMatch Stub{};
        // Proven starts of AMD-only instructions folded into the stub.
        std::vector<Domain::VirtualAddress> Consumed;
    };

    std::unique_ptr<IAmd64OnlyInstructionMatcher> _matcher = MakeAmd64OnlyInstructionMatcher();

    [[nodiscard]] Absorption _absorbFollowing(
        const std::vector<std::uint8_t>& fileBytes,
        const Domain::ProgramHeader& ph,
        const Domain::CodeMap& codeMap,
        const Pending& item
    ) const;

    void _convertSegment(
        std::vector<std::uint8_t>& fileBytes,
        const Domain::ProgramHeader& ph,
        const Domain::CodeMap& codeMap,
        ConvertResult& result,
        std::vector<std::pair<std::string, Domain::FileByteOffset>>& unsupportedSites
    ) const;
};

// Extends a trampoline site shorter than the 5-byte `jmp rel32` over the
// instructions that follow it, so the jump fits and the stub re-executes the
// moved instructions. Only straight-line, position-independent code may move:
//  - the next bytes must be a proven instruction start (otherwise the site's
//    successor is data or unreached and cannot be reasoned about);
//  - an AMD-only SSE4a successor is lowered in the same stub, but only before
//    any ordinary instruction (the stub emits lowered code first, moved bytes
//    after, so a later AMD-only instruction would be reordered);
//  - an ordinary successor must be sequential (no branch, call, return, trap),
//    have no RIP-relative operand (the displacement would be wrong at the stub
//    address) and no FS/GS override (WindowsTlsBuilder rewrites those in place
//    and cannot see an instruction that has moved into the stub);
//  - no branch target may land inside the moved range.
// On any violation the site is returned with Ok == false and a reason; the
// caller decides the fallback. Malformed AMD-only encodings still throw.
Amd64OnlyConverter::Absorption Amd64OnlyConverter::_absorbFollowing(
    const std::vector<std::uint8_t>& fileBytes,
    const Domain::ProgramHeader& ph,
    const Domain::CodeMap& codeMap,
    const Pending& item
) const {
    using Amd64OnlySubstitutionTable::kJmpRel32;
    constexpr std::uint8_t kPrefixFs = 0x64;
    constexpr std::uint8_t kPrefixGs = 0x65;
    Absorption out;
    const X64InstructionDecoder decoder;
    const auto segOffset = static_cast<std::size_t>(ph.Offset);
    const auto segSize = static_cast<std::size_t>(ph.FileSize);
    const auto siteRel = static_cast<std::size_t>(item.Address - ph.MappedAddress);
    const auto fail = [&out](std::string reason) {
        out.Ok = false;
        out.Reason = std::move(reason);
        return out;
    };
    const auto span = [&](const std::size_t rel, const std::size_t length) {
        return std::span<const std::uint8_t>(fileBytes.data() + segOffset + rel, length);
    };

    std::vector<std::span<const std::uint8_t>> sequence{span(siteRel, item.Length)};
    std::size_t siteLength = item.Length;
    std::size_t trailingBytes = 0;
    while (siteLength < kJmpRel32.Size) {
        const auto nextAddress = item.Address + siteLength;
        const auto nextRel = siteRel + siteLength;
        if (nextRel >= segSize || !codeMap.Contains(nextAddress))
            return fail("the following bytes are not a proven instruction start");
        std::size_t nextLength = 0;
        DecodedInstructionInfo info{};
        try {
            nextLength = decoder.Decode(fileBytes.data() + segOffset + nextRel, segSize - nextRel);
            info = decoder.DecodeInstruction(fileBytes.data() + segOffset + nextRel, segSize - nextRel);
        } catch (const CodegenException&) {
            return fail("the following instruction cannot be decoded");
        }
        if (nextLength == 0 || nextLength > segSize - nextRel)
            return fail("the following instruction cannot be decoded");
        const auto bytes = span(nextRel, nextLength);
        const auto amdOnly = _atFileOffset(ph.Offset + nextRel, [&] { return _matcher->Match(bytes.data(), bytes.size()); });
        if (amdOnly.has_value()) {
            if (trailingBytes != 0)
                return fail("an AMD-only instruction follows an instruction that has to move");
            sequence.push_back(bytes);
            out.Consumed.push_back(nextAddress);
        } else {
            if (info.FlowKind != ControlFlowKind::Sequential || info.HasBranchTarget)
                return fail("the following instruction is a control transfer");
            if (info.HasRipRelativeDisp)
                return fail("the following instruction has a RIP-relative operand");
            if (info.SegmentPrefix == kPrefixFs || info.SegmentPrefix == kPrefixGs)
                return fail("the following instruction has an FS/GS segment override");
            trailingBytes += nextLength;
        }
        siteLength += nextLength;
        if (codeMap.BranchEntersSite(item.Address, siteLength))
            return fail("a branch enters the bytes that would move");
    }
    const auto trailing = span(siteRel + siteLength - trailingBytes, trailingBytes);
    auto stub = _atFileOffset(item.FileOffset, [&] { return _matcher->MatchSequence(sequence, trailing); });
    if (!stub.has_value() || stub->Lowering != Amd64OnlyLowering::Trampoline)
        return fail("a following AMD-only instruction has no out-of-line lowering");
    out.Ok = true;
    out.SiteLength = siteLength;
    out.Stub = std::move(*stub);
    return out;
}

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

    // Proven starts that were folded into an earlier site's stub; they must not
    // be patched or reported again.
    std::set<Domain::VirtualAddress> consumed;
    for (const auto& item : pending) {
        if (consumed.contains(item.Address))
            continue;
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
            if (codeMap.BranchEntersSite(address, item.Length))
                throw CodegenException("Branch enters an AMD-only instruction", fileOffset);
            std::size_t siteLength = item.Length;
            const Amd64OnlyMatch* stub = &substitution;
            Absorption absorbed;
            if (item.Length < Amd64OnlySubstitutionTable::kJmpRel32.Size) {
                absorbed = _absorbFollowing(fileBytes, ph, codeMap, item);
                if (!absorbed.Ok) {
                    // Why Residual and not a failure: the relinker spec keeps the
                    // runtime SSE4a trap as the documented fallback for register
                    // forms. The site is logged and listed in the conversion
                    // report, never skipped silently.
                    std::cout << "Intel conversion: " << substitution.InstructionName << " at 0x" << std::hex << fileOffset
                              << std::dec << " is shorter than a jump and cannot be extended: " << absorbed.Reason
                              << "; left for the runtime trap\n";
                    result.Residuals.push_back({fileOffset, address, substitution.InstructionName, item.Length});
                    result.Reports.push_back({substitution.InstructionName, fileOffset, item.Length,
                                              item.Length, Amd64OnlyLowering::Residual});
                    break;
                }
                siteLength = absorbed.SiteLength;
                stub = &absorbed.Stub;
                consumed.insert(absorbed.Consumed.begin(), absorbed.Consumed.end());
            }
            const auto begin = fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset + segRel);
            result.Trampolines.push_back({
                fileOffset,
                address,
                siteLength,
                std::vector<std::uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(siteLength)),
                stub->StubBody,
                stub->ReturnBranchOffset
            });
            result.Reports.push_back({substitution.InstructionName, fileOffset, siteLength,
                                      stub->StubBody.size(), Amd64OnlyLowering::Trampoline});
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
