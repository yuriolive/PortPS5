// Classifies AMD-only instructions and produces their Intel substitution:
// same-length in-place rewrites (MOVNTSS/MOVNTSD, small SSE4a fields),
// out-of-line stub bodies (large SSE4a fields and all register forms) or
// Unsupported markers (MONITORX family). Stateless and thread-safe; it never
// reads outside the instruction bytes it is handed.
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTable.hpp>
#include <codegen/x86/Sse4aLowering.hpp>
#include <codegen/x86/Sse4aOperands.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <codegen/CodegenException.hpp>
#include <memory>
#include <span>
#include <vector>

namespace Codegen {

namespace {

using namespace Amd64OnlySubstitutionTable;

Amd64OnlyMatch _unsupported(const Entry& entry, const std::size_t length) {
    return Amd64OnlyMatch{entry.Name, length, Amd64OnlyLowering::Unsupported, {}, {}, 0};
}

class Amd64OnlyInstructionMatcher : public IAmd64OnlyInstructionMatcher {
public:
    [[nodiscard]] std::optional<Amd64OnlyMatch> Match(
        const std::uint8_t* data,
        std::size_t length
    ) const override;

    [[nodiscard]] std::optional<Amd64OnlyMatch> MatchSequence(
        std::span<const std::span<const std::uint8_t>> instructions,
        std::span<const std::uint8_t> trailing
    ) const override;

private:
    Sse4aLowering _lowering;

    [[nodiscard]] Amd64OnlyMatch _matchMovnts(const DecodedInstruction& instr, const Entry& entry) const;
    [[nodiscard]] Amd64OnlyMatch _matchSse4a(const DecodedInstruction& instr, const Entry& entry, const Entry& registerFormEntry) const;
    [[nodiscard]] static const char* _sse4aName(const Sse4aOperands& operands);
};

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchMovnts(const DecodedInstruction& instr, const Entry& entry) const {
    using namespace X64OpcodeConstants;
    const auto opcodeOffset = instr.OpcodeOffset();
    if (opcodeOffset + 2 >= instr.Length)
        throw CodegenException("MOVNTSS/MOVNTSD truncated before its ModRM byte");
    const auto modrm = instr.Data[opcodeOffset + 2];
    if (((modrm >> ModRmModShift) & ModRmModMask) == ModRmModRegister)
        throw CodegenException("MOVNTSS/MOVNTSD with a register operand");
    std::vector<std::uint8_t> replacement(instr.Data, instr.Data + instr.Length);
    replacement[opcodeOffset + 1] = kMovsStoreOpcode;
    return Amd64OnlyMatch{entry.Name, instr.Length, Amd64OnlyLowering::InPlace, std::move(replacement), {}, 0};
}

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchSse4a(const DecodedInstruction& instr, const Entry& entry, const Entry& registerFormEntry) const {
    const auto operands = DecodeSse4a(instr.Data, instr.Length);
    // Register forms (0F 79) take length/index from a register, so they have
    // no same-length rewrite and always go out of line. They are 4 or 5 bytes,
    // shorter than the 5-byte jump to the stub for the REX-less encoding; the
    // converter then moves the following instructions (see MatchSequence).
    if (!operands.RegisterForm) {
        if (auto inPlace = _lowering.LowerInPlace(operands, instr.Length))
            return Amd64OnlyMatch{entry.Name, instr.Length, Amd64OnlyLowering::InPlace, std::move(*inPlace), {}, 0};
    }
    auto body = _lowering.LowerOutOfLine(operands);
    const auto& name = operands.RegisterForm ? registerFormEntry.Name : entry.Name;
    return Amd64OnlyMatch{name, instr.Length, Amd64OnlyLowering::Trampoline, {}, std::move(body.Bytes), body.ReturnBranchOffset};
}

const char* Amd64OnlyInstructionMatcher::_sse4aName(const Sse4aOperands& operands) {
    if (operands.RegisterForm)
        return operands.Insertq ? kInsertqRegisterForm.Name : kExtrqRegisterForm.Name;
    return operands.Insertq ? kInsertq.Name : kExtrq.Name;
}

std::optional<Amd64OnlyMatch> Amd64OnlyInstructionMatcher::MatchSequence(
    const std::span<const std::span<const std::uint8_t>> instructions,
    const std::span<const std::uint8_t> trailing
) const {
    if (instructions.empty())
        return std::nullopt;
    std::vector<Sse4aOperands> sequence;
    for (const auto& bytes : instructions) {
        const DecodedInstruction instr{bytes.data(), bytes.size()};
        if (!instr.IsExtrq() && !instr.IsInsertq())
            return std::nullopt;
        sequence.push_back(DecodeSse4a(instr.Data, instr.Length));
    }
    auto body = _lowering.LowerOutOfLine(std::span<const Sse4aOperands>(sequence), trailing);
    return Amd64OnlyMatch{_sse4aName(sequence.front()), instructions.front().size(), Amd64OnlyLowering::Trampoline, {}, std::move(body.Bytes), body.ReturnBranchOffset};
}

std::optional<Amd64OnlyMatch> Amd64OnlyInstructionMatcher::Match(
    const std::uint8_t* data,
    std::size_t length
) const {
    const DecodedInstruction instr{data, length};

    if (instr.IsMovntss())
        return _matchMovnts(instr, kMovntss);

    if (instr.IsMovntsd())
        return _matchMovnts(instr, kMovntsd);

    if (instr.IsExtrq())
        return _matchSse4a(instr, kExtrq, kExtrqRegisterForm);

    if (instr.IsInsertq())
        return _matchSse4a(instr, kInsertq, kInsertqRegisterForm);

    if (instr.IsMonitorx())
        return _unsupported(kMonitorx, length);

    if (instr.IsMwaitx())
        return _unsupported(kMwaitx, length);

    if (instr.IsClzero())
        return _unsupported(kClzero, length);

    if (instr.IsRdpru())
        return _unsupported(kRdpru, length);

    if (instr.IsMcommit())
        return _unsupported(kMcommit, length);

    return std::nullopt;
}

}

std::unique_ptr<IAmd64OnlyInstructionMatcher> MakeAmd64OnlyInstructionMatcher() {
    return std::make_unique<Amd64OnlyInstructionMatcher>();
}

}
