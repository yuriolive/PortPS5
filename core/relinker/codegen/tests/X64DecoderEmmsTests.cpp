// Regression tests: EMMS (0F 77) has no ModRM byte.
// Ported from AnyPS5 ee57519c. Opcode 0F 77 lies inside the decoder's
// "two-byte opcode takes ModRM" range G (0x54..0x7F) but is a bare 2-byte
// instruction; decoding it as ModRM-bearing swallowed the next instruction's
// first byte and desynchronised every linear/recursive sweep after it.
// Synthetic byte sequences only. Single-threaded.
#include <codegen/x86/X64InstructionDecoder.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// Invariant: EMMS decodes to exactly 2 bytes even when followed by more code.
TEST(X64DecoderEmms, LengthIsTwoBytes) {
    const Codegen::X64InstructionDecoder decoder;
    const Bytes code = {0x0f, 0x77, 0xc3};  // EMMS ; ret
    EXPECT_EQ(decoder.Decode(code.data(), code.size()), 2u);
}

// Invariant: a prefixed EMMS (operand-size prefix is ignored by hardware) is
// prefix + 2 bytes; the following byte is the next instruction.
TEST(X64DecoderEmms, PrefixedLength) {
    const Codegen::X64InstructionDecoder decoder;
    const Bytes code = {0x66, 0x0f, 0x77, 0x90};
    EXPECT_EQ(decoder.Decode(code.data(), code.size()), 3u);
}

// Invariant: DecodeInstruction agrees with Decode and does not report a ModRM
// byte for EMMS, so rewriting passes never treat the next opcode as ModRM.
TEST(X64DecoderEmms, DecodeInstructionHasNoModRm) {
    const Codegen::X64InstructionDecoder decoder;
    const Bytes code = {0x0f, 0x77, 0xc3};
    const auto info = decoder.DecodeInstruction(code.data(), code.size());
    EXPECT_EQ(info.Length, 2u);
    EXPECT_FALSE(info.HasModRm);
}

// Invariant: neighbours of EMMS in range G keep their ModRM handling
// (0F 6F = MOVQ mm, mm/m64; 0F 7F = MOVQ mm/m64, mm; both carry ModRM).
TEST(X64DecoderEmms, NeighbouringOpcodesStillTakeModRm) {
    const Codegen::X64InstructionDecoder decoder;
    const Bytes movqLoad = {0x0f, 0x6f, 0xc1};
    const Bytes movqStore = {0x0f, 0x7f, 0xc1};
    EXPECT_EQ(decoder.Decode(movqLoad.data(), movqLoad.size()), 3u);
    EXPECT_EQ(decoder.Decode(movqStore.data(), movqStore.size()), 3u);
    EXPECT_TRUE(decoder.DecodeInstruction(movqLoad.data(), movqLoad.size()).HasModRm);
}

}  // namespace
