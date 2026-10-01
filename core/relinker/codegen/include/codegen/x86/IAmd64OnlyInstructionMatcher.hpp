// Interface that classifies AMD-only instructions for `--to-intel`.
// Subsystem: relinker codegen. Implementations are stateless and thread-safe.
#ifndef CODEGEN_X86_IAMD64ONLYINSTRUCTIONMATCHER_HPP
#define CODEGEN_X86_IAMD64ONLYINSTRUCTIONMATCHER_HPP

#include <codegen/x86/Amd64OnlySubstitutionTypes.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace Codegen {

/** @brief Matches machine-code bytes against the AMD-only instruction set. */
class IAmd64OnlyInstructionMatcher {
public:
    virtual ~IAmd64OnlyInstructionMatcher() = default;

    /**
     * @brief Classifies a single instruction.
     * @param data First byte of the instruction.
     * @param length Instruction length as decoded by the caller.
     * @return std::nullopt for an ordinary instruction; otherwise the
     *         substitution (InPlace, Trampoline or Unsupported). EXTRQ and
     *         INSERTQ register forms are always Trampoline.
     * @throws CodegenException for an encoding that is invalid on hardware or
     *         that cannot be lowered faithfully.
     */
    [[nodiscard]] virtual std::optional<Amd64OnlyMatch> Match(
        const std::uint8_t* data,
        std::size_t length
    ) const = 0;

    /**
     * @brief Lowers consecutive SSE4a instructions, plus moved bytes, to one stub.
     *
     * Used when a lone instruction is shorter than the 5-byte jump that
     * replaces it: the converter extends the site over the following bytes and
     * the stub re-executes them after the lowering.
     *
     * @param instructions Bytes of each SSE4a instruction, in program order.
     * @param trailing Bytes of non-AMD-only instructions moved into the stub
     *        after the run; the caller guarantees they are position independent.
     * @return A Trampoline match whose Length is the first instruction's
     *         length, or std::nullopt if any entry is not EXTRQ/INSERTQ.
     * @throws CodegenException on an SSE4a encoding that cannot be lowered.
     */
    [[nodiscard]] virtual std::optional<Amd64OnlyMatch> MatchSequence(
        std::span<const std::span<const std::uint8_t>> instructions,
        std::span<const std::uint8_t> trailing
    ) const = 0;
};

std::unique_ptr<IAmd64OnlyInstructionMatcher> MakeAmd64OnlyInstructionMatcher();

}

#endif
