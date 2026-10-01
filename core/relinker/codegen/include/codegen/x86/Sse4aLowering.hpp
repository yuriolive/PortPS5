// Interface of the SSE4a (EXTRQ/INSERTQ) Intel lowering used by `--to-intel`.
// Subsystem: relinker codegen. Stateless: every method is const and may be
// called concurrently. The produced byte sequences are position independent
// except for the trailing `jmp rel32` placeholder that the stub builders patch.
#ifndef CODEGEN_X86_SSE4ALOWERING_HPP
#define CODEGEN_X86_SSE4ALOWERING_HPP

#include <codegen/x86/Sse4aOperands.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Codegen {

/**
 * @brief An out-of-line stub body.
 *
 * The body ends with a `jmp rel32` placeholder (opcode 0xE9 followed by four
 * zero bytes) at ReturnBranchOffset; constants referenced by RIP-relative
 * operands follow it, 16-byte aligned.
 */
struct LoweredBody {
    /// Stub bytes: lowered instructions, the return branch, then constants.
    std::vector<std::uint8_t> Bytes;
    /// Offset of the placeholder return branch inside Bytes.
    std::size_t ReturnBranchOffset;
};

/** @brief Lowers SSE4a EXTRQ/INSERTQ to instructions available on Intel hosts. */
class Sse4aLowering {
public:
    /**
     * @brief Lowers an immediate-form instruction to a same-length sequence.
     * @param operands Decoded operands of the original instruction.
     * @param originalLength Length in bytes of the original instruction.
     * @return The replacement padded with NOPs to originalLength, or
     *         std::nullopt for register forms and for fields that need a
     *         stub (the replacement would not fit).
     */
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> LowerInPlace(const Sse4aOperands& operands, std::size_t originalLength) const;

    /**
     * @brief Lowers one instruction (any form) to an out-of-line stub body.
     * @param operands Decoded operands of the original instruction.
     * @return The stub body. Preserves flags and every XMM register except
     *         the destination.
     */
    [[nodiscard]] LoweredBody LowerOutOfLine(const Sse4aOperands& operands) const;

    /**
     * @brief Lowers a run of instructions plus moved bytes to one stub body.
     * @param sequence Operands of consecutive SSE4a instructions, in program order.
     * @param trailing Raw bytes of non-AMD-only instructions that followed the
     *        run in the original code and were moved into the stub. The caller
     *        guarantees they are position independent (no RIP-relative
     *        operand, no branch, no FS/GS segment override).
     * @return The stub body: the lowered sequence, then trailing, then the
     *         return branch placeholder.
     */
    [[nodiscard]] LoweredBody LowerOutOfLine(std::span<const Sse4aOperands> sequence, std::span<const std::uint8_t> trailing) const;
};

}

#endif
