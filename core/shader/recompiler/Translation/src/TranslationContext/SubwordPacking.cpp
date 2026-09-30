// core/shader/recompiler/Translation/src/TranslationContext/SubwordPacking.cpp
// Dword-pair and 16-bit lane helpers for TranslationContext: 64-bit operand reads (constant widening rules),
// f16/u16 lane extraction with op_sel, half-precision inline-constant encoding and packed half writes.
// The f16 <-> f32 conversion opcodes are direction-specific: ConvertF32F16 widens, ConvertF16F32 narrows.
#include "Translation/TranslationContext.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ShaderRecompiler {

IrU32 TranslationContext::readU32(const RdnaOperand& operand) {
    return IrU32(*readOperand(operand, IrType::U32));
}

// Reads a 64-bit source operand as {low, high} dwords.
//
// Constants are a single dword in the instruction stream (or the inline-constant encoding), so the high half
// cannot be "the next register" and must not repeat the low half either. RDNA2 ISA widening rules:
//   literal constant        -> zero-extended;
//   integer inline constant -> sign-extended (-16..-1 widen to 0xffffffff_xxxxxxxx);
//   float inline constant   -> the same value as an IEEE double (so 1.0 is 0x3ff00000_00000000).
// Registers and everything else read the operand and its successor register.
std::array<IrU32, 2> TranslationContext::readU32Pair(const RdnaOperand& operand) {
    switch (operand.kind) {
        case RdnaOperandKind::LiteralConstant: return {readU32(operand), IrU32(ir.Constant(0u))};
        case RdnaOperandKind::IntegerInlineConstant: return {readU32(operand), IrU32(ir.Constant(operand.signedVal < 0 ? 0xffffffffu : 0u))};
        case RdnaOperandKind::FloatInlineConstant: {
            // 1/(2*pi) is decoded as a float; hardware widens the exact double constant, which differs
            // from converting the rounded float, so name it explicitly.
            constexpr std::uint32_t kInvTwoPiF32 = 0x3e22f983u;
            constexpr std::uint64_t kInvTwoPiF64 = 0x3fc45f306dc9c883ull;
            const std::uint64_t bits = operand.value == kInvTwoPiF32 ? kInvTwoPiF64 : std::bit_cast<std::uint64_t>(static_cast<double>(std::bit_cast<float>(operand.value)));
            return {IrU32(ir.Constant(static_cast<std::uint32_t>(bits))), IrU32(ir.Constant(static_cast<std::uint32_t>(bits >> 32u)))};
        }
        default: return {readU32(operand), readU32(offsetOperand(operand, 1u))};
    }
}

IrU64 TranslationContext::readU64(const RdnaOperand& operand) {
    const std::array<IrU32, 2> pair = readU32Pair(operand);
    return IrU64(ir.ConstructU64(pair[0].Value(), pair[1].Value()));
}

std::array<IrU32, 2> TranslationContext::extractU64(IrU64 value) {
    return {IrU32(ir.CompositeExtract(value.Value(), 0u)), IrU32(ir.CompositeExtract(value.Value(), 1u))};
}

void TranslationContext::writeU32Pair(const RdnaOperand& operand, const std::array<IrU32, 2>& value) {
    writeRawU32(operand, value[0]);
    writeRawU32(offsetOperand(operand, 1u), value[1]);
}

IrF32 TranslationContext::readF16LaneAsF32(const RdnaOperand& operand, bool highLane, bool packed) {
    const bool selectHigh = packed ? (highLane ? operand.opSelHi : operand.opSel) : highLane;
    const IrU32 source = readF16SourceBits(operand);
    const IrU32 raw(ir.Emit(IrOpcode::BitFieldUExtract, IrType::U32, {&source.Value(), &ir.Constant(selectHigh ? 16u : 0u), &ir.Constant(16u)}));
    const IrU16 bits(ir.Emit(IrOpcode::ConvertU16U32, IrType::U16, {&raw.Value()}));
    const IrF16 half(ir.Emit(IrOpcode::BitCastF16U16, IrType::F16, {&bits.Value()}));
    // ConvertF32F16 widens (F32 result from an F16 argument); ConvertF16F32 is the narrowing direction.
    IrF32 value(ir.Emit(IrOpcode::ConvertF32F16, IrType::F32, {&half.Value()}));
    if (operand.absolute) {
        value = IrF32(ir.Emit(IrOpcode::FPAbs32, IrType::F32, {&value.Value()}));
    }
    const bool negate = packed && highLane ? operand.negateHi : operand.negate;
    if (negate) {
        value = IrF32(ir.Emit(IrOpcode::FPNeg32, IrType::F32, {&value.Value()}));
    }
    return value;
}

// Raw dword whose 16-bit lanes hold an f16 source operand.
//
// A float inline constant read by a 16-bit float operation is the *half* encoding of the constant in the
// low 16 bits (LLVM getInlineEncodingV216), not the low half of its f32 bits (which would be 0 for every
// inline constant). Every other operand is read as a register/literal dword.
IrU32 TranslationContext::readF16SourceBits(const RdnaOperand& operand) {
    if (operand.kind != RdnaOperandKind::FloatInlineConstant) {
        return applyBitSourceModifiers(operand, readRawU32(operand));
    }
    std::uint32_t half = 0u;
    switch (operand.value) {
        case 0x3f000000u: half = 0x3800u; break;  //  0.5
        case 0xbf000000u: half = 0xb800u; break;  // -0.5
        case 0x3f800000u: half = 0x3c00u; break;  //  1.0
        case 0xbf800000u: half = 0xbc00u; break;  // -1.0
        case 0x40000000u: half = 0x4000u; break;  //  2.0
        case 0xc0000000u: half = 0xc000u; break;  // -2.0
        case 0x40800000u: half = 0x4400u; break;  //  4.0
        case 0xc0800000u: half = 0xc400u; break;  // -4.0
        case 0x3e22f983u: half = 0x3118u; break;  //  1/(2*pi) in half precision
        default: throw std::invalid_argument("TranslationContext::readF16SourceBits: unknown float inline constant bits " + std::to_string(operand.value));
    }
    return applyBitSourceModifiers(operand, IrU32(ir.Constant(half)));
}

// Reads a one-lane f16 operand (VOP1/VOP2/VOP3 16-bit ops). VOP3 op_sel for this source picks the high
// half of the dword instead of the low half.
IrF32 TranslationContext::readF16AsF32(const RdnaOperand& operand) {
    const IrU32 source = readF16SourceBits(operand);
    const IrU32 raw = operand.opSel ? IrU32(ir.Emit(IrOpcode::BitFieldUExtract, IrType::U32, {&source.Value(), &ir.Constant(16u), &ir.Constant(16u)})) : source;
    const IrU16 bits(ir.Emit(IrOpcode::ConvertU16U32, IrType::U16, {&raw.Value()}));
    const IrF16 half(ir.Emit(IrOpcode::BitCastF16U16, IrType::F16, {&bits.Value()}));
    IrF32 value(ir.Emit(IrOpcode::ConvertF32F16, IrType::F32, {&half.Value()}));
    if (operand.absolute) {
        value = IrF32(ir.Emit(IrOpcode::FPAbs32, IrType::F32, {&value.Value()}));
    }
    if (operand.negate) {
        value = IrF32(ir.Emit(IrOpcode::FPNeg32, IrType::F32, {&value.Value()}));
    }
    return value;
}

IrF32 TranslationContext::readMixF32(const RdnaOperand& operand) {
    if (!operand.opSel) {
        return IrF32(*readOperand(operand, IrType::F32));
    }
    return readF16LaneAsF32(operand, operand.opSelHi, false);
}

IrU32 TranslationContext::readF16LaneBits(const RdnaOperand& operand, bool highLane) {
    return readU16LaneRaw(operand, highLane ? operand.opSelHi : operand.opSel);
}

IrU32 TranslationContext::readU16LaneRaw(const RdnaOperand& operand, bool highLane) {
    const IrU32 raw = applyBitSourceModifiers(operand, readRawU32(operand));
    return IrU32(ir.Emit(IrOpcode::BitFieldUExtract, IrType::U32, {&raw.Value(), &ir.Constant(highLane ? 16u : 0u), &ir.Constant(16u)}));
}

IrU32 TranslationContext::readU16LaneAsU32(const RdnaOperand& operand, bool highLane, bool signExtend) {
    const IrU32 raw = readU16LaneRaw(operand, highLane ? operand.opSelHi : operand.opSel);
    if (!signExtend) {
        return raw;
    }
    return IrU32(ir.Emit(IrOpcode::BitFieldSExtract, IrType::U32, {&raw.Value(), &ir.Constant(0u), &ir.Constant(16u)}));
}

IrU32 TranslationContext::readU16AsU32(const RdnaOperand& operand, bool signExtend) {
    const IrU32 raw = readU16LaneRaw(operand, operand.opSel);
    if (!signExtend) {
        return raw;
    }
    return IrU32(ir.Emit(IrOpcode::BitFieldSExtract, IrType::U32, {&raw.Value(), &ir.Constant(0u), &ir.Constant(16u)}));
}

IrU32 TranslationContext::packHalf2x16(IrF32 low, IrF32 high) {
    IrValue& pair = ir.Emit(IrOpcode::CompositeConstructF32x2, IrType::F32x2, {&low.Value(), &high.Value()});
    return IrU32(ir.Emit(IrOpcode::PackHalf2x16, IrType::U32, {&pair}));
}

void TranslationContext::write16Bits(const RdnaOperand& operand, IrU32 value) {
    if (operand.sdwaSel != 6u) {
        writeRawU32(operand, value);
        return;
    }
    const IrU32 masked(ir.BitwiseAnd(value.Value(), ir.Constant(0xffffu)));
    const IrU32 old = readRawU32(plainOperand(operand));
    const IrU32 cleared(ir.BitwiseAnd(old.Value(), ir.Constant(0xffff0000u)));
    writeRawU32(operand, IrU32(ir.BitwiseOr(cleared.Value(), masked.Value())));
}

void TranslationContext::writeF16(const RdnaOperand& operand, IrF32 value) {
    // Narrowing: F16 result from an F32 argument is ConvertF16F32 (the widening direction is ConvertF32F16).
    const IrF16 half(ir.Emit(IrOpcode::ConvertF16F32, IrType::F16, {&value.Value()}));
    const IrU16 bits(ir.Emit(IrOpcode::BitCastU16F16, IrType::U16, {&half.Value()}));
    write16Bits(operand, IrU32(ir.Emit(IrOpcode::ConvertU32U16, IrType::U32, {&bits.Value()})));
}

}
