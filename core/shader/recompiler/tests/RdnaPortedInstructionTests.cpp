// core/shader/recompiler/tests/RdnaPortedInstructionTests.cpp
// Semantic tests for the RDNA2 scalar/vector instruction and operand fixes ported from upstream AnyPS5
// (see docs/spec/shader-recompiler.md, "Upstream ports, lane E"). Each test hand-assembles one project-written
// instruction (no game bytecode), decodes it with the real decoder, translates it with the real
// TranslationContext and then *executes the emitted IR* with a tiny reference interpreter, comparing the result
// with an independent C++ statement of the RDNA2 ISA pseudo-code. That checks operand order, sign/zero
// extension, shift-count masking and SCC/EXEC effects rather than only the IR shape.
//
// Threading: single-threaded, no shared state. Preconditions: encodings follow the RDNA2 ISA field layouts
// (SOP1/SOP2/SOPC/SOPP/VOP1/VOP3), cited per helper below. Failure modes: an opcode that is not decodable
// throws from DecodeRdnaInstruction (reported as a test failure), an unsupported IR opcode in the interpreter
// yields an unknown value that the asserting test rejects.

#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBlock.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrBuilder.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrOpcode.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrProgram.hpp"
#include "IntermediateRepresentation/include/IntermediateRepresentation/IrValue.hpp"
#include "ControlFlow/include/ControlFlow/GraphBuilder.hpp"
#include "ControlFlow/include/ControlFlow/Structurizer.hpp"
#include "RdnaDecoder/include/RdnaDecoder/RdnaInstruction.hpp"
#include "RdnaDecoder/include/RdnaDecoder/RdnaInstructionDecoder.hpp"
#include "Translation/include/Translation/TranslationContext.hpp"
#include "BdaAbi.hpp"
#include "Optimization/include/Optimization/ShaderStageInputInfo.hpp"
#include "Translation/include/Translation/ShaderInputInfoBuilder.hpp"
#include "Recompiler.hpp"
#include "Optimization/include/Optimization/BindingAllocator.hpp"
#include "Optimization/include/Optimization/DeadCodeEliminator.hpp"
#include "Optimization/include/Optimization/DescriptorBindingBuilder.hpp"
#include "Optimization/include/Optimization/ShaderInfoCollector.hpp"
#include "SpirvBackend/include/SpirvBackend/SpirvEmitter.hpp"
#include "SyntheticCorpus.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <cstring>
#include <span>
#include <string_view>
#include <thread>
#include <string>
#include <vector>

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.hpp>

#include <gtest/gtest.h>

namespace ShaderRecompiler {
// Defined in Recompiler.cpp with external linkage but not declared in Recompiler.hpp; the emit-only test helper uses it.
IrProgram PrepareResourceProgram(const RecompileRequest& request);
namespace {

// ---------------------------------------------------------------------------------------------------------
// Encoders (RDNA2 ISA instruction formats). Register source codes: 0..105 SGPR, 106/107 VCC, 126/127 EXEC,
// 128 = 0, 129..192 = 1..64, 193..208 = -1..-16, 240..247 = +-0.5/1/2/4, 248 = 1/(2*pi), 255 = literal,
// 256..511 VGPR.
// ---------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kLiteral = 255u;
constexpr std::uint32_t kVgpr = 256u;

// SOP1: [31:23]=0b101111101, sdst[22:16], op[15:8], ssrc0[7:0].
constexpr std::uint32_t Sop1(std::uint32_t op, std::uint32_t sdst, std::uint32_t ssrc0) {
    return 0xBE800000u | (sdst << 16u) | (op << 8u) | ssrc0;
}
// SOP2: [31:30]=0b10, op[29:23], sdst[22:16], ssrc1[15:8], ssrc0[7:0].
constexpr std::uint32_t Sop2(std::uint32_t op, std::uint32_t sdst, std::uint32_t ssrc0, std::uint32_t ssrc1) {
    return 0x80000000u | (op << 23u) | (sdst << 16u) | (ssrc1 << 8u) | ssrc0;
}
// SOPC: [31:23]=0b101111110, op[22:16], ssrc1[15:8], ssrc0[7:0].
constexpr std::uint32_t Sopc(std::uint32_t op, std::uint32_t ssrc0, std::uint32_t ssrc1) {
    return 0xBF000000u | (op << 16u) | (ssrc1 << 8u) | ssrc0;
}
// SOPP: [31:23]=0b101111111, op[22:16], simm16[15:0].
constexpr std::uint32_t Sopp(std::uint32_t op, std::uint32_t simm16) {
    return 0xBF800000u | (op << 16u) | simm16;
}
// VOP2: [31]=0, op[30:25], vdst[24:17], vsrc1[16:9], src0[8:0]. src0 = 250 (0xfa) selects the DPP16 modifier dword.
constexpr std::uint32_t Vop2(std::uint32_t op, std::uint32_t vdst, std::uint32_t src0, std::uint32_t vsrc1) {
    return (op << 25u) | (vdst << 17u) | (vsrc1 << 9u) | src0;
}
// DPP16 dword: src0 VGPR[7:0], dpp_ctrl[16:8], fi[18], bound_ctrl[19], bank_mask[27:24], row_mask[31:28].
constexpr std::uint32_t Dpp16(std::uint32_t src0Vgpr, std::uint32_t control, bool boundCtrl, std::uint32_t bankMask, std::uint32_t rowMask) {
    return src0Vgpr | (control << 8u) | (boundCtrl ? 1u << 19u : 0u) | (bankMask << 24u) | (rowMask << 28u);
}
// VOP3 word 0: [31:26]=0b110101, op[25:16], clamp[15], op_sel[14:11], abs[10:8], vdst[7:0].
constexpr std::uint32_t Vop3W0(std::uint32_t op, std::uint32_t vdst, std::uint32_t opSel = 0u) {
    return 0xD4000000u | (op << 16u) | (opSel << 11u) | vdst;
}
// VOP3 word 1: src2[26:18], src1[17:9], src0[8:0].
constexpr std::uint32_t Vop3W1(std::uint32_t src0, std::uint32_t src1, std::uint32_t src2) {
    return (src2 << 18u) | (src1 << 9u) | src0;
}

// ---------------------------------------------------------------------------------------------------------
// Reference interpreter over the raw (pre-SSA) IR of one block. Register reads/writes are sequential, exactly
// like the translated instruction stream, so ordering bugs (read-after-write) are observable.
// ---------------------------------------------------------------------------------------------------------
using Value = std::optional<std::uint64_t>;

// RDNA2 DPP source lane for the controls modelled here: quad_perm (0x000-0x0ff), row_shl:n (0x101-0x10f) and
// row_shr:n (0x111-0x11f). Rows are 16 lanes; a shift that leaves the row yields an invalid source. Any other control
// is not modelled and returns nullopt.
struct DppSource {
    std::uint32_t lane;
    bool valid;
};
std::optional<DppSource> DppSourceLane(std::uint32_t control, std::uint32_t lane) {
    if (control <= 0xffu) return DppSource{(lane & ~3u) | ((control >> (2u * (lane & 3u))) & 3u), true};
    if (control >= 0x101u && control <= 0x10fu) {
        const std::uint32_t n = control - 0x100u;
        return DppSource{lane + n, (lane % 16u) + n < 16u};
    }
    if (control >= 0x111u && control <= 0x11fu) {
        const std::uint32_t n = control - 0x110u;
        return DppSource{lane - n, (lane % 16u) >= n};
    }
    return std::nullopt;
}

struct Machine {
    std::array<std::uint32_t, 128> sgpr{};
    std::array<std::uint32_t, 64> vgpr{};
    bool scc = false;
    bool vccBit = false;     // this lane's VCC bit
    bool execBit = true;     // this lane's EXEC bit
    std::uint32_t execLo = 0u;
    std::uint32_t execHi = 0u;
    std::uint32_t lane = 0u;
    // Observed effects.
    bool sccWritten = false;
    bool execBitWritten = false;
    bool execLoWritten = false;
    bool execHiWritten = false;
    std::array<bool, 128> sgprWritten{};
    std::array<bool, 64> vgprWritten{};
    // Writes of unknown (uninterpretable) values are test failures unless a test opts out because it checks only
    // other outputs (e.g. the carry-out mask write, which needs a ballot the interpreter does not model).
    bool allowUnknownWrites = false;
    // Optional wave-wide VGPR file. When set, GetVectorRegister reads the current lane's row from it, which lets the
    // DPP ops below fetch another lane's source value. waveExec is the wave's EXEC mask (bit i = lane i active).
    const std::array<std::array<std::uint32_t, 64>, 64>* waveVgpr = nullptr;
    std::uint64_t waveExec = ~0ull;
    std::map<const IrValue*, Value> memo;

    Value Eval(const IrValue* raw) {
        const IrValue* v = raw->Resolve();
        if (v->HasImmediate()) {
            switch (v->Type()) {
            case IrType::U64: return v->ImmediateU64();
            case IrType::Bool: return v->ImmediateBool() ? 1u : 0u;
            case IrType::U16: return v->ImmediateU16();
            case IrType::U8: return v->ImmediateU8();
            case IrType::F16: return v->ImmediateF16Bits();
            default: return v->ImmediateU32();
            }
        }
        const auto found = memo.find(v);
        if (found != memo.end()) return found->second;
        const Value result = Compute(v);
        memo[v] = result;
        return result;
    }

    // Executes every instruction of the block in order.
    void Run(const IrBlock& block) {
        memo.clear();
        for (const IrValue* inst : block.Instructions()) {
            if (inst == nullptr) continue;
            Step(inst);
        }
    }

private:
    void Unknown(const char* what, std::uint32_t index, bool known) {
        if (!allowUnknownWrites) ADD_FAILURE() << (known ? "out-of-range " : "uninterpretable value written to ") << what << ' ' << index;
    }

    void Step(const IrValue* inst) {
        switch (inst->Opcode()) {
        case IrOpcode::SetScalarRegister: {
            const auto value = Eval(inst->Argument(1));
            const auto index = inst->Argument(0)->Register().index;
            if (!value || index >= sgpr.size()) { Unknown("SGPR", index, value.has_value()); return; }
            sgpr[index] = static_cast<std::uint32_t>(*value); sgprWritten[index] = true;
            return;
        }
        case IrOpcode::SetVectorRegister: {
            const auto value = Eval(inst->Argument(1));
            const auto index = inst->Argument(0)->Register().index;
            if (!value || index >= vgpr.size()) { Unknown("VGPR", index, value.has_value()); return; }
            vgpr[index] = static_cast<std::uint32_t>(*value); vgprWritten[index] = true;
            return;
        }
        case IrOpcode::SetScc: {
            const auto value = Eval(inst->Argument(0));
            if (!value) { Unknown("SCC", 0u, false); return; }
            scc = *value != 0u; sccWritten = true;
            return;
        }
        case IrOpcode::SetExec: {
            const auto value = Eval(inst->Argument(0));
            if (!value) { Unknown("EXEC bit", 0u, false); return; }
            execBit = *value != 0u; execBitWritten = true;
            return;
        }
        case IrOpcode::SetExecLo: {
            const auto value = Eval(inst->Argument(0));
            if (!value) { Unknown("EXEC_LO", 0u, false); return; }
            execLo = static_cast<std::uint32_t>(*value); execLoWritten = true;
            return;
        }
        case IrOpcode::SetExecHi: {
            const auto value = Eval(inst->Argument(0));
            if (!value) { Unknown("EXEC_HI", 0u, false); return; }
            execHi = static_cast<std::uint32_t>(*value); execHiWritten = true;
            return;
        }
        default:
            // Register reads must observe the state at their program position, so evaluate them now.
            (void)Eval(inst);
            return;
        }
    }

    Value Compute(const IrValue* v) {
        const auto arg = [&](std::size_t i) { return Eval(v->Argument(i)); };
        const auto u32 = [](Value x) { return static_cast<std::uint32_t>(*x); };
        // Every operand must be known; otherwise the result is unknown.
        for (std::size_t i = 0; i < v->ArgumentCount(); ++i) {
            const IrValue* a = v->Argument(i);
            if (a->Type() == IrType::ScalarReg || a->Type() == IrType::VectorReg) continue;
            if (!Eval(a)) return std::nullopt;
        }
        switch (v->Opcode()) {
        case IrOpcode::GetScalarRegister: return sgpr[v->Argument(0)->Register().index];
        case IrOpcode::GetVectorRegister:
            return waveVgpr != nullptr ? (*waveVgpr)[lane][v->Argument(0)->Register().index] : vgpr[v->Argument(0)->Register().index];
        case IrOpcode::DppMoveU32: {
            // Independent statement of the RDNA2 DPP source fetch for the controls this harness models. A control the
            // harness does not model is unknown (the asserting test fails) instead of silently treated as identity.
            const auto flags = v->Flags<DppMoveFlags>();
            const auto source = DppSourceLane(flags.control, lane);
            if (!source || waveVgpr == nullptr) return std::nullopt;
            if (!source->valid) return 0u;  // an invalid source lane reads as zero (bound_ctrl) / the write is gated off
            if (!flags.fetchInactive && ((waveExec >> source->lane) & 1ull) == 0u) return 0u;
            Machine other = *this;
            other.lane = source->lane;
            other.memo.clear();
            return other.Eval(v->Argument(0));
        }
        case IrOpcode::DppUpdateU32: {
            // D keeps its old value unless EXEC, ROW_MASK[lane/16], BANK_MASK[(lane/4)%4] enable the lane and (without
            // BOUND_CTRL) the source lane is valid. arg0 = new value, arg1 = old value, arg2 = this lane's EXEC bit.
            const auto flags = v->Flags<DppMoveFlags>();
            const auto source = DppSourceLane(flags.control, lane);
            if (!source) return std::nullopt;
            const bool rowOk = ((flags.rowMask >> (lane / 16u)) & 1u) != 0u;
            const bool bankOk = ((flags.bankMask >> ((lane / 4u) % 4u)) & 1u) != 0u;
            const bool write = *arg(2) != 0u && rowOk && bankOk && (flags.boundControl || source->valid);
            return write ? *arg(0) : *arg(1);
        }
        case IrOpcode::GetScc: return scc ? 1u : 0u;
        case IrOpcode::GetExec: return execBit ? 1u : 0u;
        case IrOpcode::GetExecLo: return execLo;
        case IrOpcode::GetExecHi: return execHi;
        case IrOpcode::GetVcc: return vccBit ? 1u : 0u;
        case IrOpcode::GetScalarMaskTag: return 0u;
        case IrOpcode::GetThreadBitScalarRegister: return 0u;
        case IrOpcode::LaneId: return lane;
        case IrOpcode::IAdd32: return static_cast<std::uint32_t>(u32(arg(0)) + u32(arg(1)));
        case IrOpcode::ISub32: return static_cast<std::uint32_t>(u32(arg(0)) - u32(arg(1)));
        case IrOpcode::IMul32: return static_cast<std::uint32_t>(u32(arg(0)) * u32(arg(1)));
        case IrOpcode::IAddCarry32: {
            const std::uint64_t sum = static_cast<std::uint64_t>(u32(arg(0))) + u32(arg(1));
            return (sum & 0xffffffffull) | ((sum >> 32u) << 32u);
        }
        case IrOpcode::CompositeExtractU32x2: return *arg(1) == 0u ? (*arg(0) & 0xffffffffull) : (*arg(0) >> 32u);
        case IrOpcode::BitwiseAnd32: return u32(arg(0)) & u32(arg(1));
        case IrOpcode::BitwiseOr32: return u32(arg(0)) | u32(arg(1));
        case IrOpcode::BitwiseXor32: return u32(arg(0)) ^ u32(arg(1));
        case IrOpcode::BitwiseNot32: return ~u32(arg(0));
        // The backend emits SPIR-V shifts for the 32-bit ops directly (undefined for count >= 32), so the translator must
        // mask ISA counts itself; the interpreter therefore refuses out-of-range counts instead of masking them.
        case IrOpcode::ShiftLeftLogical32: if (u32(arg(1)) >= 32u) return std::nullopt; return u32(arg(0)) << u32(arg(1));
        case IrOpcode::ShiftRightLogical32: if (u32(arg(1)) >= 32u) return std::nullopt; return u32(arg(0)) >> u32(arg(1));
        case IrOpcode::ShiftRightArithmetic32: if (u32(arg(1)) >= 32u) return std::nullopt; return static_cast<std::uint32_t>(static_cast<std::int32_t>(u32(arg(0))) >> u32(arg(1)));
        case IrOpcode::ULessThan32: return u32(arg(0)) < u32(arg(1)) ? 1u : 0u;
        case IrOpcode::UGreaterThan32: return u32(arg(0)) > u32(arg(1)) ? 1u : 0u;
        case IrOpcode::IEqual32: return u32(arg(0)) == u32(arg(1)) ? 1u : 0u;
        case IrOpcode::INotEqual32: return u32(arg(0)) != u32(arg(1)) ? 1u : 0u;
        case IrOpcode::SelectU32:
        case IrOpcode::SelectU1: return *arg(0) != 0u ? *arg(1) : *arg(2);
        case IrOpcode::LogicalOr: return (*arg(0) != 0u || *arg(1) != 0u) ? 1u : 0u;
        case IrOpcode::LogicalAnd: return (*arg(0) != 0u && *arg(1) != 0u) ? 1u : 0u;
        case IrOpcode::LogicalNot: return *arg(0) != 0u ? 0u : 1u;
        case IrOpcode::BitReverse32: {
            std::uint32_t x = u32(arg(0)), r = 0u;
            for (int i = 0; i < 32; ++i) r |= ((x >> i) & 1u) << (31 - i);
            return r;
        }
        case IrOpcode::BitCount32: return static_cast<std::uint32_t>(std::popcount(u32(arg(0))));
        case IrOpcode::FindILsb32: return u32(arg(0)) == 0u ? 0xffffffffu : static_cast<std::uint32_t>(std::countr_zero(u32(arg(0))));
        case IrOpcode::BitFieldUExtract: {
            const auto offset = u32(arg(1)), count = u32(arg(2));
            const std::uint64_t mask = count >= 32u ? 0xffffffffull : ((1ull << count) - 1u);
            return static_cast<std::uint32_t>((static_cast<std::uint64_t>(u32(arg(0))) >> offset) & mask);
        }
        case IrOpcode::BitFieldSExtract: {
            const auto offset = u32(arg(1)), count = u32(arg(2));
            const std::uint32_t field = static_cast<std::uint32_t>((static_cast<std::uint64_t>(u32(arg(0))) >> offset) & ((1ull << count) - 1u));
            const std::uint32_t signBit = 1u << (count - 1u);
            return static_cast<std::uint32_t>((field ^ signBit) - signBit);
        }
        case IrOpcode::ConvertU16U32: return u32(arg(0)) & 0xffffu;
        case IrOpcode::ConvertU32U16: return *arg(0) & 0xffffu;
        case IrOpcode::CompositeConstructU64: return static_cast<std::uint64_t>(u32(arg(0))) | (static_cast<std::uint64_t>(u32(arg(1))) << 32u);
        case IrOpcode::CompositeExtractU64: return *arg(1) == 0u ? (*arg(0) & 0xffffffffull) : (*arg(0) >> 32u);
        // Strict: the translator (s_bitcmp_b64, alignbyte) masks the count before this op, so a missing mask is caught.
        case IrOpcode::ShiftRightLogical64: if (u32(arg(1)) >= 64u) return std::nullopt; return *arg(0) >> u32(arg(1));
        // 64-bit left and arithmetic shifts are masked to 6 bits by the SPIR-V backend itself (EmitShift64 and
        // EmitConstantShift64 in SpirvAluEmitterMath.cpp), so the IR op is defined for any count and s_ashr_i64
        // intentionally passes S1 through; the interpreter models that contract.
        case IrOpcode::ShiftLeftLogical64: return *arg(0) << (u32(arg(1)) & 63u);
        case IrOpcode::ShiftRightArithmetic64: return static_cast<std::uint64_t>(static_cast<std::int64_t>(*arg(0)) >> (u32(arg(1)) & 63u));
        case IrOpcode::BitwiseAnd64: return *arg(0) & *arg(1);
        case IrOpcode::IEqual64: return *arg(0) == *arg(1) ? 1u : 0u;
        case IrOpcode::INotEqual64: return *arg(0) != *arg(1) ? 1u : 0u;
        default: return std::nullopt;
        }
    }
};

// One translated instruction. The program is heap-allocated because IrProgram is neither copyable nor movable.
struct Translated {
    std::unique_ptr<IrProgram> program = std::make_unique<IrProgram>();
    IrBlock* entry = nullptr;
    RdnaInstruction inst{};
};

Translated Translate(std::vector<std::uint32_t> code, std::uint32_t waveSize = 64u) {
    Translated t;
    t.entry = &t.program->CreateBlock();
    t.program->SetEntryBlock(*t.entry);
    t.program->SetWaveSize(waveSize);
    code.push_back(0xBF810000u);  // s_endpgm padding so multi-dword decode never reads past the span
    t.inst = DecodeRdnaInstruction(0u, std::span<const std::uint32_t>(code), 0u);
    TranslationContext context(*t.program, *t.entry, 256u);
    context.TranslateInstruction(t.inst);
    return t;
}

// Translate() for tests of *new* decode/translate support. This test target is built without asynchronous unwind
// tables, so an exception escaping a TEST body (an opcode the decoder rejects) terminates the whole binary instead of
// failing one test. This wrapper turns it into a normal test failure; the caller asserts `entry != nullptr`.
Translated TryTranslate(std::vector<std::uint32_t> code, std::uint32_t waveSize = 64u) {
    try {
        return Translate(std::move(code), waveSize);
    } catch (const std::exception& error) {
        ADD_FAILURE() << "decode/translate threw: " << error.what();
        return Translated{};
    }
}

// ---------------------------------------------------------------------------------------------------------
// V_PERM_B32
// ---------------------------------------------------------------------------------------------------------

// Independent statement of the RDNA2 ISA pseudo-code:
//   BYTE_PERMUTE(data, sel): sel>=13 -> 0xff; 12 -> 0x00; 11..8 -> sign of data byte (2*(sel-8)+1) replicated;
//   sel<=7 -> data byte sel.  D.u[8k+7:8k] = BYTE_PERMUTE({S0, S1}, S2[8k+7:8k]).
std::uint32_t ReferencePerm(std::uint32_t s0, std::uint32_t s1, std::uint32_t s2) {
    const std::uint64_t data = (static_cast<std::uint64_t>(s0) << 32u) | s1;
    std::uint32_t out = 0u;
    for (unsigned k = 0; k < 4; ++k) {
        const unsigned sel = (s2 >> (8u * k)) & 0xffu;
        std::uint32_t b;
        if (sel >= 13u) b = 0xffu;
        else if (sel == 12u) b = 0x00u;
        else if (sel >= 8u) b = ((data >> (8u * (2u * (sel - 8u) + 1u) + 7u)) & 1u) ? 0xffu : 0x00u;
        else b = static_cast<std::uint32_t>((data >> (8u * sel)) & 0xffu);
        out |= b << (8u * k);
    }
    return out;
}

// Invariant: every selector byte value (0..255) in every result-byte position maps exactly as the ISA
// pseudo-code says, including the sign selectors 8..11, the constant selectors 12 and >=13, and the
// S0-high / S1-low operand roles. Without the VPermB32 decode entry the opcode 0x344 is rejected.
TEST(RdnaPortedInstructionTests, VPermB32MatchesIsaByteSelection) {
    // v_perm_b32 v0, v1, v2, v3: S0 = v1 (bytes 4..7), S1 = v2 (bytes 0..3), S2 = v3 selectors.
    auto t = Translate({Vop3W0(0x344u, 0u), Vop3W1(kVgpr + 1u, kVgpr + 2u, kVgpr + 3u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::VPermB32);
    // Bytes with and without bit 7 set so the sign selectors are distinguishable.
    const std::uint32_t s0 = 0x7f80ff01u;  // bytes 4..7 = 01 ff 80 7f
    const std::uint32_t s1 = 0x8040c0e0u;  // bytes 0..3 = e0 c0 40 80
    for (std::uint32_t sel = 0u; sel < 256u; ++sel) {
        for (unsigned position = 0; position < 4u; ++position) {
            // Fill the other selector bytes with a fixed in-range pattern so positions stay independent.
            std::uint32_t selectors = 0x03020100u;
            selectors = (selectors & ~(0xffu << (8u * position))) | (sel << (8u * position));
            Machine m;
            m.vgpr[1] = s0;
            m.vgpr[2] = s1;
            m.vgpr[3] = selectors;
            m.Run(*t.entry);
            ASSERT_TRUE(m.vgprWritten[0]) << "sel=" << sel;
            ASSERT_EQ(m.vgpr[0], ReferencePerm(s0, s1, selectors)) << "sel=" << sel << " position=" << position;
        }
    }
    // Whole-word spot check with distinct selectors per byte (sign of byte 1, sign of byte 7, zero, all-ones).
    const std::uint32_t mixed = 0x0dU << 24u | 0x0cU << 16u | 0x0bU << 8u | 0x08u;
    Machine m;
    m.vgpr[1] = s0; m.vgpr[2] = s1; m.vgpr[3] = mixed;
    m.Run(*t.entry);
    EXPECT_EQ(m.vgpr[0], ReferencePerm(s0, s1, mixed));
    EXPECT_EQ(m.vgpr[0] & 0xffu, 0xffu);           // sel 8: byte 1 of S1 = 0xc0, sign set
    EXPECT_EQ((m.vgpr[0] >> 8u) & 0xffu, 0x00u);   // sel 11: byte 7 of S0 = 0x7f, sign clear
    EXPECT_EQ((m.vgpr[0] >> 16u) & 0xffu, 0x00u);  // sel 12: constant zero
    EXPECT_EQ(m.vgpr[0] >> 24u, 0xffu);            // sel 13: constant all ones
}

// ---------------------------------------------------------------------------------------------------------
// 32-bit scalar ops
// ---------------------------------------------------------------------------------------------------------

// s_cmov_b32 (SOP1 0x05): D = SCC ? S0 : D. SCC is an input only and must not be rewritten.
TEST(RdnaPortedInstructionTests, SCmovB32MovesOnlyWhenSccSet) {
    auto t = Translate({Sop1(0x05u, 4u, 6u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::SCmovB32);
    for (const bool scc : {false, true}) {
        Machine m;
        m.scc = scc;
        m.sgpr[4] = 0x11111111u;
        m.sgpr[6] = 0x22222222u;
        m.Run(*t.entry);
        EXPECT_EQ(m.sgpr[4], scc ? 0x22222222u : 0x11111111u) << "scc=" << scc;
        EXPECT_FALSE(m.sccWritten);
    }
}

// s_bcnt0_i32_b32 (SOP1 0x0d): D = count of zero bits, SCC = (D != 0).
// s_ff0_i32_b32  (SOP1 0x11): D = index of the lowest zero bit, -1 if S0 is all ones; SCC untouched.
TEST(RdnaPortedInstructionTests, SBcnt0AndSFf0CountZeroBits) {
    auto bcnt = Translate({Sop1(0x0du, 4u, 6u)});
    auto ff0 = Translate({Sop1(0x11u, 4u, 6u)});
    EXPECT_EQ(bcnt.inst.op, RdnaOpcode::SBcnt0I32B32);
    EXPECT_EQ(ff0.inst.op, RdnaOpcode::SFf0I32B32);
    for (const std::uint32_t x : {0x00000000u, 0xffffffffu, 0x00000001u, 0x7fffffffu, 0x80000000u, 0xfffffff7u, 0xf0f0f0f0u, 0xdeadbeefu}) {
        Machine a;
        a.sgpr[6] = x;
        a.scc = (x == 0xffffffffu);  // pre-set to the opposite of the expected outcome for the all-ones case
        a.Run(*bcnt.entry);
        const auto zeros = static_cast<std::uint32_t>(std::popcount(~x));
        EXPECT_EQ(a.sgpr[4], zeros) << std::hex << x;
        EXPECT_EQ(a.scc, zeros != 0u);

        Machine b;
        b.sgpr[6] = x;
        b.scc = true;
        b.Run(*ff0.entry);
        EXPECT_EQ(b.sgpr[4], x == 0xffffffffu ? 0xffffffffu : static_cast<std::uint32_t>(std::countr_zero(~x))) << std::hex << x;
        EXPECT_FALSE(b.sccWritten);
    }
}

// s_sext_i32_i8 (SOP1 0x19) / s_sext_i32_i16 (0x1a): D = sign-extended low byte / halfword; SCC untouched.
TEST(RdnaPortedInstructionTests, SSextExtendsLowByteAndShortWithSign) {
    auto i8 = Translate({Sop1(0x19u, 4u, 6u)});
    auto i16 = Translate({Sop1(0x1au, 4u, 6u)});
    EXPECT_EQ(i8.inst.op, RdnaOpcode::SSextI32I8);
    EXPECT_EQ(i16.inst.op, RdnaOpcode::SSextI32I16);
    for (const std::uint32_t x : {0x00000000u, 0x0000007fu, 0x00000080u, 0x000000ffu, 0x00007fffu, 0x00008000u, 0xabcd1234u, 0x1234ab80u, 0xffff7f7fu}) {
        Machine a;
        a.sgpr[6] = x;
        a.Run(*i8.entry);
        EXPECT_EQ(a.sgpr[4], static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(x & 0xffu)))) << std::hex << x;
        EXPECT_FALSE(a.sccWritten);
        Machine b;
        b.sgpr[6] = x;
        b.Run(*i16.entry);
        EXPECT_EQ(b.sgpr[4], static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(x & 0xffffu)))) << std::hex << x;
        EXPECT_FALSE(b.sccWritten);
    }
}

// ---------------------------------------------------------------------------------------------------------
// 64-bit scalar ops
// ---------------------------------------------------------------------------------------------------------

// s_brev_b64 (SOP1 0x0c): D = bit-reverse of the 64-bit source, so the dwords swap and each is reversed.
TEST(RdnaPortedInstructionTests, SBrevB64ReversesAcrossBothDwords) {
    auto t = Translate({Sop1(0x0cu, 4u, 2u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::SBrevB64);
    for (const std::uint64_t x : {0x0000000000000001ull, 0x8000000000000000ull, 0x00000000ffffffffull, 0x0123456789abcdefull}) {
        Machine m;
        m.sgpr[2] = static_cast<std::uint32_t>(x);
        m.sgpr[3] = static_cast<std::uint32_t>(x >> 32u);
        m.Run(*t.entry);
        std::uint64_t expected = 0u;
        for (int i = 0; i < 64; ++i) expected |= ((x >> i) & 1ull) << (63 - i);
        EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[5]) << 32u) | m.sgpr[4], expected) << std::hex << x;
        EXPECT_FALSE(m.sccWritten);
    }
}

// s_ashr_i64 (SOP2 0x23): D = S0.i64 >> S1[5:0] with sign fill, SCC = (D != 0). Counts >= 64 use only 6 bits.
TEST(RdnaPortedInstructionTests, SAshrI64SignFillsAndMasksCount) {
    auto t = Translate({Sop2(0x23u, 4u, 2u, 6u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::SAshrI64);
    for (const std::uint64_t x : {0x8000000000000000ull, 0x7fffffffffffffffull, 0xffffffff00000000ull, 0x0000000000000001ull, 0ull}) {
        for (const std::uint32_t count : {0u, 1u, 31u, 32u, 33u, 63u, 64u, 65u, 0xffffffc1u}) {
            Machine m;
            m.sgpr[2] = static_cast<std::uint32_t>(x);
            m.sgpr[3] = static_cast<std::uint32_t>(x >> 32u);
            m.sgpr[6] = count;
            m.Run(*t.entry);
            const auto expected = static_cast<std::uint64_t>(static_cast<std::int64_t>(x) >> (count & 63u));
            EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[5]) << 32u) | m.sgpr[4], expected) << std::hex << x << " >> " << count;
            EXPECT_EQ(m.scc, expected != 0u);
            EXPECT_TRUE(m.sccWritten);
        }
    }
}

// s_ashr_i64 with a literal source: the operand is a signed 64-bit value, so the 32-bit literal is sign-extended
// (0x80000000 becomes 0xffffffff80000000), unlike B64 bitwise ops where a literal zero-extends. Behaviour cross-checked
// against hardware-validated expectations in the KytyPS5 suite (ShaderRecompilerComputeTests, ScalarAshrI64OperandsAndAliases).
TEST(RdnaPortedInstructionTests, SAshrI64SignExtendsLiteralSource) {
    for (const std::uint32_t literal : {0u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        for (const std::uint32_t count : {0u, 1u, 31u, 32u, 63u, 64u}) {
            // s_ashr_i64 s[4:5], literal, s6
            auto t = Translate({Sop2(0x23u, 4u, kLiteral, 6u), literal});
            Machine m;
            m.sgpr[6] = count;
            m.Run(*t.entry);
            const std::int64_t value = static_cast<std::int64_t>(static_cast<std::int32_t>(literal));
            const auto expected = static_cast<std::uint64_t>(value >> (count & 63u));
            EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[5]) << 32u) | m.sgpr[4], expected) << std::hex << literal << " >> " << count;
            EXPECT_EQ(m.scc, expected != 0u);
        }
    }
}

// All sources are read before the destination pair and SCC are written: the shift count may alias the low half of
// the destination (s_ashr_i64 s[4:5], s[2:3], s4) and the source pair may be the destination (in-place).
TEST(RdnaPortedInstructionTests, SAshrI64SourcesReadBeforeDestinationWrite) {
    const std::uint64_t value = 0xfedcba9889abcdefull;
    {
        auto t = Translate({Sop2(0x23u, 4u, 2u, 4u)});  // count register aliases D.lo
        Machine m;
        m.sgpr[2] = static_cast<std::uint32_t>(value);
        m.sgpr[3] = static_cast<std::uint32_t>(value >> 32u);
        m.sgpr[4] = 33u;
        m.Run(*t.entry);
        const auto expected = static_cast<std::uint64_t>(static_cast<std::int64_t>(value) >> 33u);
        EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[5]) << 32u) | m.sgpr[4], expected);
    }
    {
        auto t = Translate({Sop2(0x23u, 2u, 2u, 6u)});  // in place: s[2:3] >>= s6
        Machine m;
        m.sgpr[2] = static_cast<std::uint32_t>(value);
        m.sgpr[3] = static_cast<std::uint32_t>(value >> 32u);
        m.sgpr[6] = 65u;
        m.Run(*t.entry);
        const auto expected = static_cast<std::uint64_t>(static_cast<std::int64_t>(value) >> 1u);
        EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[3]) << 32u) | m.sgpr[2], expected);
    }
}

// s_bitcmp0_b64 / s_bitcmp1_b64 (SOPC 0x0e / 0x0f): SCC = (S0.u64[S1[5:0]] == 0 / 1). Bits above 31 live in the
// second dword and the index is masked to six bits.
TEST(RdnaPortedInstructionTests, SBitcmpB64TestsBitOfSixtyFourBitSource) {
    auto zero = Translate({Sopc(0x0eu, 2u, 6u)});
    auto one = Translate({Sopc(0x0fu, 2u, 6u)});
    EXPECT_EQ(zero.inst.op, RdnaOpcode::SBitcmp0B64);
    EXPECT_EQ(one.inst.op, RdnaOpcode::SBitcmp1B64);
    const std::uint64_t x = 0x8000000100000001ull | (1ull << 40u);
    for (std::uint32_t index = 0u; index < 140u; ++index) {
        const bool bit = ((x >> (index & 63u)) & 1ull) != 0u;
        Machine a;
        a.sgpr[2] = static_cast<std::uint32_t>(x); a.sgpr[3] = static_cast<std::uint32_t>(x >> 32u); a.sgpr[6] = index;
        a.Run(*zero.entry);
        EXPECT_EQ(a.scc, !bit) << "bitcmp0 index=" << index;
        Machine b;
        b.sgpr[2] = a.sgpr[2]; b.sgpr[3] = a.sgpr[3]; b.sgpr[6] = index;
        b.Run(*one.entry);
        EXPECT_EQ(b.scc, bit) << "bitcmp1 index=" << index;
    }
}

// 64-bit constants: s_mov_b64 with a literal, integer inline constants and float inline constants must widen per
// the ISA (literal zero-extends, integer inline sign-extends, float inline becomes the matching double) instead of
// repeating the low dword into the high dword.
TEST(RdnaPortedInstructionTests, SixtyFourBitOperandsWidenConstants) {
    const auto mov = [](std::uint32_t srcCode, std::vector<std::uint32_t> tail = {}) {
        std::vector<std::uint32_t> code{Sop1(0x04u, 4u, srcCode)};
        code.insert(code.end(), tail.begin(), tail.end());
        auto t = Translate(code);
        Machine m;
        m.Run(*t.entry);
        return (static_cast<std::uint64_t>(m.sgpr[5]) << 32u) | m.sgpr[4];
    };
    EXPECT_EQ(mov(kLiteral, {0x89abcdefu}), 0x0000000089abcdefull);  // literal: zero-extended
    EXPECT_EQ(mov(129u), 1ull);                                        // inline +1
    EXPECT_EQ(mov(192u), 64ull);                                       // inline +64 (max)
    EXPECT_EQ(mov(193u), 0xffffffffffffffffull);                       // inline -1: sign-extended
    EXPECT_EQ(mov(208u), 0xfffffffffffffff0ull);                       // inline -16
    EXPECT_EQ(mov(242u), 0x3ff0000000000000ull);                       // inline 1.0 -> double 1.0
    EXPECT_EQ(mov(243u), 0xbff0000000000000ull);                       // inline -1.0
    EXPECT_EQ(mov(240u), 0x3fe0000000000000ull);                       // inline 0.5
    EXPECT_EQ(mov(247u), 0xc010000000000000ull);                       // inline -4.0
    // 1/(2*pi) must be the exact double constant, not the float constant widened (which ends ...60000000).
    EXPECT_EQ(mov(248u), 0x3fc45f306dc9c883ull);
}

// ---------------------------------------------------------------------------------------------------------
// saveexec family
// ---------------------------------------------------------------------------------------------------------

enum class SaveexecOp { Or, Xor, Andn2, Orn2, And };

// Runs s_<op>_saveexec_b64 sdst, ssrc (64-bit) on a wave64 machine and checks the ISA semantics:
//   D = EXEC (old); EXEC = f(S0, EXEC); SCC = (EXEC != 0); the per-lane exec bit is the lane's own bit.
void CheckSaveexec(std::uint32_t sop1Opcode, RdnaOpcode expected, SaveexecOp kind, std::uint32_t sdst, std::uint32_t ssrc) {
    auto t = Translate({Sop1(sop1Opcode, sdst, ssrc)});
    EXPECT_EQ(t.inst.op, expected);
    const std::uint64_t source = 0xf0f0f0f00ff00ff0ull;
    const std::uint64_t oldExec = 0xffff00000000ffffull;
    std::uint64_t expectedExec = 0u;
    switch (kind) {
    case SaveexecOp::Or: expectedExec = source | oldExec; break;
    case SaveexecOp::Xor: expectedExec = source ^ oldExec; break;
    case SaveexecOp::Andn2: expectedExec = source & ~oldExec; break;  // S0 & ~EXEC
    case SaveexecOp::Orn2: expectedExec = source | ~oldExec; break;  // S0 | ~EXEC
    case SaveexecOp::And: expectedExec = source & oldExec; break;
    }
    for (std::uint32_t lane = 0u; lane < 64u; ++lane) {
        Machine m;
        m.lane = lane;
        m.execLo = static_cast<std::uint32_t>(oldExec);
        m.execHi = static_cast<std::uint32_t>(oldExec >> 32u);
        m.sgpr[ssrc] = static_cast<std::uint32_t>(source);
        m.sgpr[ssrc + 1u] = static_cast<std::uint32_t>(source >> 32u);
        m.Run(*t.entry);
        ASSERT_TRUE(m.execLoWritten && m.execHiWritten);
        EXPECT_EQ((static_cast<std::uint64_t>(m.execHi) << 32u) | m.execLo, expectedExec);
        EXPECT_EQ((static_cast<std::uint64_t>(m.sgpr[sdst + 1u]) << 32u) | m.sgpr[sdst], oldExec) << "destination receives the old EXEC";
        EXPECT_EQ(m.scc, expectedExec != 0u);
        ASSERT_TRUE(m.execBitWritten);
        // The lane predicate is this lane's bit of the new mask, not "mask != 0".
        EXPECT_EQ(m.execBit, ((expectedExec >> lane) & 1ull) != 0u) << "lane " << lane;
    }
}

TEST(RdnaPortedInstructionTests, SOrSaveexecB64) { CheckSaveexec(0x25u, RdnaOpcode::SOrSaveexecB64, SaveexecOp::Or, 4u, 6u); }
TEST(RdnaPortedInstructionTests, SXorSaveexecB64) { CheckSaveexec(0x26u, RdnaOpcode::SXorSaveexecB64, SaveexecOp::Xor, 4u, 6u); }
TEST(RdnaPortedInstructionTests, SAndn2SaveexecB64) { CheckSaveexec(0x27u, RdnaOpcode::SAndn2SaveexecB64, SaveexecOp::Andn2, 4u, 6u); }
// s_orn2_saveexec_b64 (SOP1 0x28) predates this lane; its semantics (EXEC = S0 | ~EXEC) are pinned here because the
// per-lane predicate and operand-order fixes share its lowering (cross-checked against the KytyPS5 suite's
// ScalarOrn2SaveexecUsesSourceOrNotExec expectations).
TEST(RdnaPortedInstructionTests, SOrn2SaveexecB64) { CheckSaveexec(0x28u, RdnaOpcode::SOrn2SaveexecB64, SaveexecOp::Orn2, 4u, 6u); }
// Regression: the lane predicate used to be "new mask != 0" for every lane, and the aliased destination was
// written before the source was read (see next test).
TEST(RdnaPortedInstructionTests, SAndSaveexecB64PerLaneExecBit) { CheckSaveexec(0x24u, RdnaOpcode::SAndSaveexecB64, SaveexecOp::And, 4u, 6u); }

// Hardware reads S0 before it writes the old EXEC into D, so `s_and_saveexec_b64 s[6:7], s[6:7]` computes
// EXEC & old_S0 (the aliased form compilers use to mask EXEC by VCC). Writing D first would give EXEC & EXEC.
TEST(RdnaPortedInstructionTests, SAndSaveexecB64AliasedSourceReadsBeforeWrite) {
    auto t = Translate({Sop1(0x24u, 6u, 6u)});
    Machine m;
    m.execLo = 0xffff0000u;
    m.execHi = 0x0000ffffu;
    m.sgpr[6] = 0xf0f0f0f0u;
    m.sgpr[7] = 0x0f0f0f0fu;
    m.Run(*t.entry);
    EXPECT_EQ(m.execLo, 0xf0f00000u);   // old S0 & old EXEC, not EXEC & EXEC (0xffff0000)
    EXPECT_EQ(m.execHi, 0x00000f0fu);
    EXPECT_EQ(m.sgpr[6], 0xffff0000u);  // D now holds the old EXEC
    EXPECT_EQ(m.sgpr[7], 0x0000ffffu);
}

// Same ordering rule for the 32-bit form (s_and_saveexec_b32, SOP1 0x3c) on wave32.
TEST(RdnaPortedInstructionTests, SAndSaveexecB32AliasedSourceReadsBeforeWrite) {
    auto t = Translate({Sop1(0x3cu, 6u, 6u)}, 32u);
    Machine m;
    m.execLo = 0xffff0000u;
    m.sgpr[6] = 0xf0f0f0f0u;
    m.Run(*t.entry);
    EXPECT_EQ(m.execLo, 0xf0f00000u);
    EXPECT_EQ(m.sgpr[6], 0xffff0000u);
}

// ---------------------------------------------------------------------------------------------------------
// SOPP: S_CLAUSE and the conditional debug branches
// ---------------------------------------------------------------------------------------------------------

// s_clause (SOPP 0x21) is a scheduling hint. It must decode and translate without touching registers/SCC/EXEC.
TEST(RdnaPortedInstructionTests, SClauseIsSchedulingHintOnly) {
    auto t = Translate({Sopp(0x21u, 3u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::SClause);
    Machine m;
    m.Run(*t.entry);
    EXPECT_FALSE(m.sccWritten);
    EXPECT_FALSE(m.execBitWritten);
    for (std::size_t i = 0; i < m.sgprWritten.size(); ++i) EXPECT_FALSE(m.sgprWritten[i]);
}

// s_cbranch_cdbgsys / _cdbguser / _cdbgsys_or_user / _cdbgsys_and_user (SOPP 0x17..0x1a) branch only while a
// debugger sets the wave's conditional-debug bits, which never happens on a retail wave: all four decode to one
// "never taken" opcode, leave no state, and must not split the control-flow graph.
TEST(RdnaPortedInstructionTests, ConditionalDebugBranchesAreNeverTaken) {
    for (std::uint32_t op = 0x17u; op <= 0x1au; ++op) {
        auto t = Translate({Sopp(op, 1u)});
        EXPECT_EQ(t.inst.op, RdnaOpcode::SCbranchCdbg) << "opcode " << op;
        Machine m;
        m.Run(*t.entry);
        EXPECT_FALSE(m.sccWritten);
        EXPECT_TRUE(t.entry->Instructions().empty()) << "a never-taken branch emits no IR";
    }
    // s_cbranch_cdbgsys +1 ; s_nop ; s_endpgm: a real conditional branch would create a second block.
    const std::vector<std::uint32_t> code{Sopp(0x17u, 1u), Sopp(0x00u, 0u), Sopp(0x01u, 0u)};
    const RdnaProgram program = RdnaInstructionDecoder().Decode(code);
    ASSERT_EQ(program.instructions.size(), 3u);
    const ControlFlowGraph cfg = GraphBuilder().Build(program);
    EXPECT_EQ(cfg.blocks.size(), 1u);
}

// Debugger stubs are commonly placed past s_endpgm and reached only through these branches (seen in retail code
// as s_cbranch_cdbgsys over s_endpgm into a devkit-only stub). Treating the branch as never taken means the
// stub is unreachable: the graph must end at s_endpgm and must not follow the branch target. Behaviour cross-checked
// against SharpEmu's Gen5ShaderDecoderBoundaryTests (DebuggerBranchPastEndPgm_DoesNotDecodeDebuggerStub).
TEST(RdnaPortedInstructionTests, DebugBranchOverEndpgmDoesNotReachStub) {
    // s_cbranch_cdbgsys +1 (skips s_endpgm) ; s_endpgm ; s_nop (would-be stub)
    const std::vector<std::uint32_t> code{Sopp(0x17u, 1u), Sopp(0x01u, 0u), Sopp(0x00u, 0u)};
    const RdnaProgram program = RdnaInstructionDecoder().Decode(code);
    const ControlFlowGraph cfg = GraphBuilder().Build(program);
    ASSERT_FALSE(cfg.blocks.empty());
    const BasicBlock& entry = cfg.blocks[cfg.entryBlock == InvalidControlFlowId ? 0u : cfg.entryBlock];
    EXPECT_EQ(entry.terminator.kind, TerminatorKind::Return);
    EXPECT_TRUE(entry.successors.empty()) << "falling through to s_endpgm, not branching over it";
}

// Finds the first value reachable from `root` (through arguments) with the given opcode, depth-limited.
const IrValue* FindUpstream(const IrValue* root, IrOpcode opcode, int depth = 10) {
    if (root == nullptr || depth < 0) return nullptr;
    root = root->Resolve();
    if (root->Opcode() == opcode) return root;
    for (const IrValue* argument : root->Arguments()) {
        if (const IrValue* found = FindUpstream(argument, opcode, depth - 1)) return found;
    }
    return nullptr;
}

const IrValue* FindVectorStore(const IrBlock& block, std::uint32_t reg) {
    const IrValue* found = nullptr;
    for (const IrValue* inst : block.Instructions()) {
        if (inst && inst->Opcode() == IrOpcode::SetVectorRegister && inst->Argument(0)->Register().index == reg) found = inst->Argument(1);
    }
    return found;
}

// ---------------------------------------------------------------------------------------------------------
// VOP1 SDWA on V_BFREV_B32 / V_FFBH_U32
// ---------------------------------------------------------------------------------------------------------

// VOP1 + SDWA: word0 = 0x7E000000 | vdst<<17 | op<<9 | 249; word1 = src0 | dst_sel<<8 | src0_sel<<16 | sext<<19.
// dst_sel 6 = DWORD (whole result), src0_sel 1 = BYTE_1, 5 = WORD_1; sext selects sign extension of the field.
std::vector<std::uint32_t> Vop1Sdwa(std::uint32_t op, std::uint32_t vdst, std::uint32_t vsrc, std::uint32_t srcSel, bool sext) {
    return {0x7E000000u | (vdst << 17u) | (op << 9u) | 249u, vsrc | (6u << 8u) | (srcSel << 16u) | (sext ? (1u << 19u) : 0u)};
}

// The SDWA source selector feeds the 32-bit operation: v_bfrev_b32 of BYTE_1 bit-reverses the zero-extended byte,
// and of a sign-extended WORD_1 bit-reverses the sign-extended halfword. Before the decoder rule existed these
// encodings were rejected with "VOP1 SDWA source selector is not supported".
TEST(RdnaPortedInstructionTests, Vop1SdwaSourceSelectorsOnBfrevAndFfbh) {
    auto reverse = [](std::uint32_t x) { std::uint32_t r = 0u; for (int i = 0; i < 32; ++i) r |= ((x >> i) & 1u) << (31 - i); return r; };
    const std::uint32_t input = 0x8ab1c2d3u;  // byte1 = 0xc2, word1 = 0x8ab1 (sign bit set)

    auto brevByte = Translate(Vop1Sdwa(0x38u, 0u, 1u, 1u, false));
    EXPECT_EQ(brevByte.inst.op, RdnaOpcode::VBfrevB32);
    EXPECT_EQ(brevByte.inst.source0.sdwaSel, 1u);
    Machine a;
    a.vgpr[1] = input;
    a.Run(*brevByte.entry);
    EXPECT_EQ(a.vgpr[0], reverse(0xc2u));

    auto brevWordSext = Translate(Vop1Sdwa(0x38u, 0u, 1u, 5u, true));
    Machine b;
    b.vgpr[1] = input;
    b.Run(*brevWordSext.entry);
    EXPECT_EQ(b.vgpr[0], reverse(0xffff8ab1u));

    auto ffbhByte = Translate(Vop1Sdwa(0x39u, 0u, 1u, 1u, false));
    EXPECT_EQ(ffbhByte.inst.op, RdnaOpcode::VFfbhU32);
    EXPECT_EQ(ffbhByte.inst.source0.sdwaSel, 1u);
    // The decode rule is the change under test for FFBH; the translation must still produce a result in v0.
    EXPECT_NE(FindVectorStore(*ffbhByte.entry, 0u), nullptr);
}

// ---------------------------------------------------------------------------------------------------------
// VOP3 carry-in
// ---------------------------------------------------------------------------------------------------------

// v_add_co_ci_u32 in VOP3 form (opcode 0x128): D = S0 + S1 + carry_in where carry_in is THIS LANE's bit of the SGPR
// pair named by src2 (here s[8:9]); it is not VCC. The interpreter's VCC bit is set opposite to the s[8:9] bit so
// a VCC-based carry-in is caught. Carry-out goes to sdst (s[10:11]).
TEST(RdnaPortedInstructionTests, Vop3AddCoCiReadsCarryInFromThirdSource) {
    auto t = Translate({Vop3W0(0x128u, 0u) | (10u << 8u), Vop3W1(kVgpr + 1u, kVgpr + 2u, 8u)});
    EXPECT_EQ(t.inst.op, RdnaOpcode::VAddcU32);
    EXPECT_GE(t.inst.sourceCount, 3u);
    const std::uint64_t carryMask = 0x00000000a5a5a5a5ull | (1ull << 40u);
    for (std::uint32_t lane = 0u; lane < 64u; ++lane) {
        const bool carry = ((carryMask >> lane) & 1ull) != 0u;
        Machine m;
        m.allowUnknownWrites = true;  // carry-out mask write needs a ballot; only v0 is checked here
        m.lane = lane;
        m.vccBit = !carry;  // the wrong source would give the opposite answer
        m.vgpr[1] = 100u;
        m.vgpr[2] = 23u;
        m.sgpr[8] = static_cast<std::uint32_t>(carryMask);
        m.sgpr[9] = static_cast<std::uint32_t>(carryMask >> 32u);
        m.Run(*t.entry);
        ASSERT_TRUE(m.vgprWritten[0]);
        EXPECT_EQ(m.vgpr[0], 123u + (carry ? 1u : 0u)) << "lane " << lane;
    }
}

// v_sub_co_ci_u32 (VOP3 0x129): D = S0 - S1 - borrow_in; v_subrev_co_ci_u32 (0x12a): D = S1 - S0 - borrow_in, with
// the borrow-in taken from this lane's bit of the SGPR pair in src2 (here s[8:9]), not VCC.
TEST(RdnaPortedInstructionTests, Vop3SubCoCiReadsBorrowInFromThirdSource) {
    auto sub = Translate({Vop3W0(0x129u, 0u) | (10u << 8u), Vop3W1(kVgpr + 1u, kVgpr + 2u, 8u)});
    auto subrev = Translate({Vop3W0(0x12au, 0u) | (10u << 8u), Vop3W1(kVgpr + 1u, kVgpr + 2u, 8u)});
    EXPECT_EQ(sub.inst.op, RdnaOpcode::VSubCoCiU32);
    EXPECT_EQ(subrev.inst.op, RdnaOpcode::VSubrevCoCiU32);
    const std::uint64_t borrowMask = 0x5a5a5a5a5a5a5a5aull;
    for (std::uint32_t lane = 0u; lane < 64u; ++lane) {
        const std::uint32_t borrow = ((borrowMask >> lane) & 1ull) != 0u ? 1u : 0u;
        Machine a;
        a.allowUnknownWrites = true;  // carry-out mask write needs a ballot; only v0 is checked here
        a.lane = lane;
        a.vccBit = borrow == 0u;  // opposite of the mask bit: a VCC-based borrow would be wrong
        a.vgpr[1] = 100u; a.vgpr[2] = 23u;
        a.sgpr[8] = static_cast<std::uint32_t>(borrowMask); a.sgpr[9] = static_cast<std::uint32_t>(borrowMask >> 32u);
        Machine b = a;
        a.Run(*sub.entry);
        b.Run(*subrev.entry);
        ASSERT_TRUE(a.vgprWritten[0] && b.vgprWritten[0]);
        EXPECT_EQ(a.vgpr[0], 100u - 23u - borrow) << "lane " << lane;
        EXPECT_EQ(b.vgpr[0], 23u - 100u - borrow) << "lane " << lane;
    }
}

// The VOP2 encoding has no explicit third source: VCC is its implicit carry-in (v_addc_co_u32, VOP2 0x28).
TEST(RdnaPortedInstructionTests, Vop2AddcStillUsesVccCarryIn) {
    // VOP2: [31]=0, op[30:25], vdst[24:17], vsrc1[16:9], src0[8:0].
    auto t = Translate({(0x28u << 25u) | (0u << 17u) | (2u << 9u) | (kVgpr + 1u)});
    // The VOP2 decode names VCC as the implicit third source, which is what the carry-in read resolves.
    EXPECT_EQ(t.inst.source2.kind, RdnaOperandKind::VccLo);
    for (const bool vcc : {false, true}) {
        Machine m;
        m.vccBit = vcc;
        m.vgpr[1] = 7u;
        m.vgpr[2] = 5u;
        m.Run(*t.entry);
        EXPECT_EQ(m.vgpr[0], 12u + (vcc ? 1u : 0u));
    }
}

// ---------------------------------------------------------------------------------------------------------
// f16 operand reads
// ---------------------------------------------------------------------------------------------------------

// A float inline constant read by an f16 operation is the half-precision encoding of the constant in the low
// 16 bits (1.0 -> 0x3c00), not the low half of the f32 bits (0). v_cvt_f32_f16 (VOP1 0x0b) with src0 = 1.0
// (inline code 242) must therefore feed the half bits 0x3c00 into the widening conversion. This also fails
// before the fix because the f16->f32 widening used the narrowing opcode and the IR builder rejected it.
TEST(RdnaPortedInstructionTests, F16InlineConstantIsHalfEncoding) {
    struct Case { std::uint32_t code; std::uint32_t half; };
    for (const Case c : {Case{240u, 0x3800u}, Case{241u, 0xb800u}, Case{242u, 0x3c00u}, Case{243u, 0xbc00u}, Case{244u, 0x4000u}, Case{245u, 0xc000u}, Case{246u, 0x4400u}, Case{247u, 0xc400u}, Case{248u, 0x3118u}}) {
        // VOP1: [31:25]=0b0111111, vdst[24:17], op[16:9], src0[8:0].
        auto t = Translate({0x7E000000u | (0u << 17u) | (0x0bu << 9u) | c.code});
        EXPECT_EQ(t.inst.op, RdnaOpcode::VCvtF32F16);
        const IrValue* stored = FindVectorStore(*t.entry, 0u);
        ASSERT_NE(stored, nullptr) << "inline " << c.code;
        const IrValue* convert = FindUpstream(stored, IrOpcode::ConvertF32F16);
        ASSERT_NE(convert, nullptr) << "f16 -> f32 must use ConvertF32F16";
        const IrValue* u16 = FindUpstream(convert, IrOpcode::ConvertU16U32);
        ASSERT_NE(u16, nullptr);
        Machine m;
        const auto bits = m.Eval(u16);
        ASSERT_TRUE(bits.has_value());
        EXPECT_EQ(*bits, c.half) << "inline constant code " << c.code;
    }
}

// VOP3 op_sel on an f16 operand selects the high half of the source dword. v_fma_f16 (VOP3 0x34b) with
// op_sel[0]=1 must extract bits [31:16] of src0 only; src1 (op_sel[1]=0) must read the low half.
TEST(RdnaPortedInstructionTests, Vop3F16OpSelPicksHighHalf) {
    const auto highHalfExtracts = [](const IrValue* root) {
        // Count BitFieldUExtract(offset=16,width=16) nodes reachable from a source operand.
        int count = 0;
        std::vector<const IrValue*> stack{root};
        int visited = 0;
        while (!stack.empty() && visited++ < 400) {
            const IrValue* v = stack.back()->Resolve();
            stack.pop_back();
            if (v->Opcode() == IrOpcode::BitFieldUExtract && v->Argument(1)->Resolve()->HasImmediate() && v->Argument(1)->Resolve()->ImmediateU32() == 16u) ++count;
            for (const IrValue* a : v->Arguments()) stack.push_back(a);
        }
        return count;
    };
    // op_sel = 0b0001 (src0 high), v_fma_f16 v0, v1, v2, v3.
    auto selHigh = Translate({Vop3W0(0x34bu, 0u, 0x1u), Vop3W1(kVgpr + 1u, kVgpr + 2u, kVgpr + 3u)});
    auto selLow = Translate({Vop3W0(0x34bu, 0u, 0x0u), Vop3W1(kVgpr + 1u, kVgpr + 2u, kVgpr + 3u)});
    EXPECT_EQ(selHigh.inst.op, RdnaOpcode::VFmaF16);
    const IrValue* highStore = FindVectorStore(*selHigh.entry, 0u);
    const IrValue* lowStore = FindVectorStore(*selLow.entry, 0u);
    ASSERT_NE(highStore, nullptr);
    ASSERT_NE(lowStore, nullptr);
    // Only the op_sel'd source contributes a high-half extraction (the result pack has its own low-half write).
    EXPECT_GT(highHalfExtracts(highStore), highHalfExtracts(lowStore));
    EXPECT_EQ(highHalfExtracts(lowStore), 0);
}

// ---------------------------------------------------------------------------------------------------------
// IR value equality
// ---------------------------------------------------------------------------------------------------------

// Two values with the same opcode, type and arguments but different instruction flags are different operations
// (flags carry cache/rounding modifiers) and must compare unequal; equal flags still compare equal.
TEST(RdnaPortedInstructionTests, IrValueEqualityDistinguishesFlags) {
    IrProgram program;
    IrBlock& block = program.CreateBlock();
    program.SetEntryBlock(block);
    IrBuilder ir(program);
    ir.SetInsertionPoint(block);
    IrValue& operand = ir.Constant(5u);
    IrValue& first = program.CreateValue(IrOpcode::BitwiseNot32, IrType::U32);
    IrValue& second = program.CreateValue(IrOpcode::BitwiseNot32, IrType::U32);
    IrValue& third = program.CreateValue(IrOpcode::BitwiseNot32, IrType::U32);
    first.AddArgument(&operand);
    second.AddArgument(&operand);
    third.AddArgument(&operand);
    first.SetFlags<std::uint32_t>(1u);
    second.SetFlags<std::uint32_t>(2u);
    third.SetFlags<std::uint32_t>(1u);
    EXPECT_FALSE(first == second);
    EXPECT_TRUE(first == third);
}

// ---------------------------------------------------------------------------------------------------------
// MIMG UNORM control bit
// ---------------------------------------------------------------------------------------------------------

// MIMG word 0 bit 12 is UNORM (unnormalized addressing): the RDNA2 ISA requires it to be set on image stores and
// atomics, and loads ignore it. The decoder used to reject every word with that bit as "reserved"; it must now
// accept it on non-sampling ops and keep rejecting it on sample/gather (where it changes coordinate semantics).
// Words: word0 = 0b111100<<26 | opcode<<18 | dmask<<8 | dim<<3 | unorm<<12, word1 = vaddr | vdata<<8 | srsrc<<16
// (| ssamp<<21 for samplers).
TEST(RdnaPortedInstructionTests, MimgUnormAcceptedOnStoreRejectedOnSample) {
    constexpr std::uint32_t kEncoding = 0xF0000000u;
    constexpr std::uint32_t kUnorm = 1u << 12u;
    const auto mimg = [](std::uint32_t opcode, std::uint32_t extraWord0, std::uint32_t word1) {
        const std::vector<std::uint32_t> code{kEncoding | (opcode << 18u) | (1u << 8u) | (1u << 3u) | extraWord0, word1, 0xBF810000u};
        return DecodeRdnaInstruction(0u, std::span<const std::uint32_t>(code), 0u);
    };
    constexpr std::uint32_t kWord1 = 1u | (2u << 8u);
    // image_store (0x08) with and without UNORM both decode to the same op.
    EXPECT_NO_THROW({ EXPECT_EQ(mimg(0x08u, 0u, kWord1).op, RdnaOpcode::ImageStore); });
    EXPECT_NO_THROW({ EXPECT_EQ(mimg(0x08u, kUnorm, kWord1).op, RdnaOpcode::ImageStore); });
    // image_load (0x00) with UNORM is also accepted (the bit is ignored by loads).
    EXPECT_NO_THROW(mimg(0x00u, kUnorm, kWord1));
    // Only UNORM was relaxed: other reserved word-0 bits (bit 14 here) are still rejected on a store, so the decoder did
    // not simply stop validating non-sampling ops.
    EXPECT_THROW((void)mimg(0x08u, 1u << 14u, kWord1), std::runtime_error);
    // image_sample (0x20, sampler in word1) with UNORM stays rejected, and the message names the words.
    constexpr std::uint32_t kSampleWord1 = kWord1 | (1u << 21u);
    EXPECT_NO_THROW(mimg(0x20u, 0u, kSampleWord1));
    try {
        (void)mimg(0x20u, kUnorm, kSampleWord1);
        ADD_FAILURE() << "image_sample with UNORM must be rejected";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("words"), std::string::npos) << error.what();
    }
}

// ---------------------------------------------------------------------------------------------------------
// BDA lookup function control
// ---------------------------------------------------------------------------------------------------------

// Returns the OpFunction control mask of the function named `name`, or nullopt when there is no such function.
std::optional<std::uint32_t> FunctionControlByName(const std::vector<std::uint32_t>& spirv, const std::string& name) {
    constexpr std::uint32_t kOpName = 5u;
    constexpr std::uint32_t kOpFunction = 54u;
    std::uint32_t targetId = 0u;
    for (std::size_t at = 5u; at < spirv.size();) {
        const std::uint32_t opcode = spirv[at] & 0xffffu;
        const std::uint32_t count = spirv[at] >> 16u;
        if (count == 0u) break;
        if (opcode == kOpName && at + 2u < spirv.size()) {
            const char* text = reinterpret_cast<const char*>(&spirv[at + 2u]);
            const std::size_t bytes = (count - 2u) * sizeof(std::uint32_t);
            if (std::strncmp(text, name.c_str(), bytes) == 0 && std::strlen(text) == name.size()) targetId = spirv[at + 1u];
        }
        if (opcode == kOpFunction && targetId != 0u && spirv[at + 2u] == targetId) return spirv[at + 3u];
        at += count;
    }
    return std::nullopt;
}

// get_bda_pointer is called from every guest-memory access and contains a binary-search loop; drivers that inline
// every call site compile shaders with hundreds of accesses orders of magnitude slower. The emitted function must
// carry the SPIR-V DontInline function control (bit 1, value 0x2). A flat load through a VGPR address is the
// smallest synthetic shader that goes through the BDA path.
TEST(RdnaPortedInstructionTests, BdaLookupFunctionIsMarkedDontInline) {
    // Vertex stage: flat_load_dword v0, v[1:2] ; s_waitcnt 0 ; exp pos0 v0..v3 done ; s_endpgm. The export keeps
    // the load alive (dead-code elimination drops an unused one). A FLAT store would be refused ("writable addresses
    // require GPU ownership tracking") and an LDS write inserts a workgroup barrier, which BDA fault termination forbids.
    const Golden::SyntheticCase testCase{Golden::SyntheticFamily::Flat, "flat_load_to_export",
        {0xDC300000u, 0x00000001u, 0xBF8C0000u, 0xF80008CFu, 0x03020100u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Vertex};
    const Golden::SyntheticCase* flat = &testCase;
    auto owned = Golden::MakeRequest(*flat, 64u, 64u);
    // The driver advertises the BDA page-table ABI version; without it DMA accesses are refused.
    owned.request.target.bdaAbiVersion = BdaAbi::Version;
    // Capabilities/extensions the BDA path requires (Int64, PhysicalStorageBufferAddresses, StorageBuffer8BitAccess).
    static constexpr std::array<std::uint32_t, 3> kCapabilities{11u, 5347u, 4448u};
    static constexpr std::array<std::string_view, 2> kExtensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    owned.request.target.supportedCapabilities = kCapabilities;
    owned.request.target.supportedExtensions = kExtensions;
    const RecompileResult result = Recompile(owned.request);
    ASSERT_FALSE(result.spirv.empty());
    const auto control = FunctionControlByName(result.spirv, "get_bda_pointer");
    ASSERT_TRUE(control.has_value()) << "flat load must use the BDA lookup helper";
    constexpr std::uint32_t kDontInline = 0x2u;
    EXPECT_NE(*control & kDontInline, 0u);
}

// ---------------------------------------------------------------------------------------------------------
// Thread-local scratch storage (HostThreadLocal)
// ---------------------------------------------------------------------------------------------------------

// BuildShaderStageInputInfo returns pointers into per-thread scratch storage. ShaderVertexInputInfo is not
// trivially destructible, so a plain `thread_local` would run its destructor from the C++ TLS-exit callback, which
// is unsafe on threads that host guest code on Windows; the storage is HostThreadLocal (FLS-backed) instead.
// Invariants checked: the storage is per thread (another thread's build does not overwrite ours), a thread keeps
// reusing its own slot, and short-lived threads that built every stage exit without crashing. This is a
// regression guard: the unsafe-destructor hazard itself only shows on guest-hosting threads and cannot be
// provoked on hosted CI.
TEST(RdnaPortedInstructionTests, StageInputInfoStorageIsPerThreadAndSurvivesThreadExit) {
    GuestContext main{};
    main.waveSize = 64u;
    main.vertex = ShaderVertexStageInfo{};
    main.vertex->fetchAttribReg = 4u;
    const ShaderStageInputInfo first = BuildShaderStageInputInfo(ShaderStageKind::Vertex, main);
    ASSERT_NE(first.vertex, nullptr);
    EXPECT_EQ(first.vertex->fetchAttribReg, 4);

    const ShaderVertexInputInfo* workerPointer = nullptr;
    int workerValue = 0;
    std::thread([&] {
        GuestContext other{};
        other.waveSize = 64u;
        other.vertex = ShaderVertexStageInfo{};
        other.vertex->fetchAttribReg = 9u;
        const ShaderStageInputInfo info = BuildShaderStageInputInfo(ShaderStageKind::Vertex, other);
        workerPointer = info.vertex;
        workerValue = info.vertex->fetchAttribReg;
    }).join();
    EXPECT_EQ(workerValue, 9);
    EXPECT_NE(workerPointer, first.vertex) << "each thread owns its scratch slot";
    EXPECT_EQ(first.vertex->fetchAttribReg, 4) << "another thread's build must not overwrite this thread's storage";

    const ShaderStageInputInfo second = BuildShaderStageInputInfo(ShaderStageKind::Vertex, main);
    EXPECT_EQ(second.vertex, first.vertex) << "a thread reuses its own slot";

    // Churn short-lived threads through all three stages so the FLS destructors run at thread exit.
    for (int i = 0; i < 16; ++i) {
        std::thread([] {
            GuestContext context{};
            context.waveSize = 64u;
            context.vertex = ShaderVertexStageInfo{};
            context.compute = ShaderComputeStageInfo{};
            context.pixel = ShaderPixelStageInfo{};
            EXPECT_NE(BuildShaderStageInputInfo(ShaderStageKind::Vertex, context).vertex, nullptr);
            EXPECT_NE(BuildShaderStageInputInfo(ShaderStageKind::Compute, context).compute, nullptr);
            EXPECT_NE(BuildShaderStageInputInfo(ShaderStageKind::Pixel, context).pixel, nullptr);
        }).join();
    }
}

// ---------------------------------------------------------------------------------------------------------
// DPP: row/bank masks and bound_ctrl gate the destination write
// ---------------------------------------------------------------------------------------------------------

// RDNA2 ISA (DPP): a lane's VGPR write happens only when EXEC, ROW_MASK[lane/16] and BANK_MASK[(lane/4)%4] enable
// the lane, and, with BOUND_CTRL = 0, only when the source lane exists (a row shift that leaves the row does not).
// With BOUND_CTRL = 1 an invalid source reads as 0 and the write goes ahead. The lanes a mask leaves out keep the
// destination's old value. The decoder puts the DPP fields on the source operand; the translator must hand them to the
// destination too (TranslateInstruction), otherwise every lane is written and a wave64 scan that adds one row's total
// into the odd rows only (row_mask 0xa) adds it into all of them.
// Test: v_add_nc_u32_dpp v1, v0, v1 over a wave64 for several controls and masks, each lane checked against the
// pseudo-code (D = (src ? S0[src_lane] : 0) + S1). Failure mode without the fix: a masked lane's v1 changes.
TEST(RdnaPortedInstructionTests, DppRowAndBankMasksGateTheVgprWrite) {
    struct Control {
        std::uint32_t code;
        bool boundCtrl;
    };
    const Control controls[] = {{0xe4u, false}, {0xb1u, false}, {0x111u, false}, {0x111u, true}, {0x101u, false}, {0x101u, true}};
    std::array<std::array<std::uint32_t, 64>, 64> wave{};
    for (std::uint32_t lane = 0; lane < 64u; ++lane) {
        wave[lane][0] = 0x100u + lane * 7u;      // S0 (read through DPP)
        wave[lane][1] = 0x10000u + lane * 5u;    // S1 and the old destination value
    }
    for (const auto& control : controls) {
        for (const std::uint32_t rowMask : {0xfu, 0xau, 0x5u, 0x1u, 0x0u}) {
            for (const std::uint32_t bankMask : {0xfu, 0x3u, 0xcu, 0x6u}) {
                auto t = TryTranslate({Vop2(0x25u, 1u, 0xfau, 1u), Dpp16(0u, control.code, control.boundCtrl, bankMask, rowMask)});
                ASSERT_NE(t.entry, nullptr);
                EXPECT_EQ(t.inst.op, RdnaOpcode::VAddNcU32);
                for (std::uint32_t lane = 0; lane < 64u; ++lane) {
                    Machine m;
                    m.lane = lane;
                    m.waveVgpr = &wave;
                    m.execBit = true;
                    m.Run(*t.entry);
                    const auto source = DppSourceLane(control.code, lane);
                    ASSERT_TRUE(source.has_value());
                    const bool write = ((rowMask >> (lane / 16u)) & 1u) != 0u && ((bankMask >> ((lane / 4u) % 4u)) & 1u) != 0u &&
                                       (control.boundCtrl || source->valid);
                    const std::uint32_t s0 = source->valid ? wave[source->lane][0] : 0u;
                    const std::uint32_t expected = write ? s0 + wave[lane][1] : wave[lane][1];
                    ASSERT_TRUE(m.vgprWritten[1]) << "ctrl=" << control.code;
                    EXPECT_EQ(m.vgpr[1], expected) << "ctrl=0x" << std::hex << control.code << " bound=" << control.boundCtrl << " row=" << rowMask
                                                   << " bank=" << bankMask << " lane=" << std::dec << lane;
                }
            }
        }
    }
}

// An accumulator form (v_fmac_f32) reads the destination as its third operand. Its DPP modifier moves the *sources*
// and gates the write; it must not also be applied to the accumulator read (that would permute the accumulator
// across lanes). The IR must therefore hold exactly one DppMoveU32 (the S0 fetch) and one DppUpdateU32 (the gated
// write). Without accumulatorOperand the destination carries the DPP fields and the accumulator read adds a second
// DppMoveU32.
TEST(RdnaPortedInstructionTests, DppDoesNotMoveTheMacAccumulator) {
    // v_fmac_f32_dpp v1, v0, v2 quad_perm:[1,0,3,2] row_mask:0xf bank_mask:0xf
    auto t = TryTranslate({Vop2(0x2bu, 1u, 0xfau, 2u), Dpp16(0u, 0xb1u, false, 0xfu, 0xfu)});
    ASSERT_NE(t.entry, nullptr);
    std::uint32_t moves = 0u;
    std::uint32_t updates = 0u;
    for (const IrValue* inst : t.entry->Instructions()) {
        if (inst == nullptr) continue;
        moves += inst->Opcode() == IrOpcode::DppMoveU32 ? 1u : 0u;
        updates += inst->Opcode() == IrOpcode::DppUpdateU32 ? 1u : 0u;
    }
    EXPECT_EQ(moves, 1u) << "only the S0 source is fetched through DPP";
    EXPECT_EQ(updates, 1u) << "the destination write is gated by the DPP masks";
}

// ---------------------------------------------------------------------------------------------------------
// 32-bit saveexec forms (wave32) and the 64-bit zero-bit scalar ops
// ---------------------------------------------------------------------------------------------------------

// Runs s_<op>_saveexec_b32 sdst, ssrc on a wave32 machine: D = EXEC; EXEC = f(S0, EXEC); SCC = (EXEC != 0); the lane's
// EXEC bit is its own bit of the new mask. Without the decoder entries the opcode throws "unsupported SOP1 opcode".
void CheckSaveexec32(std::uint32_t sop1Opcode, RdnaOpcode expected, SaveexecOp kind, std::uint32_t sdst, std::uint32_t ssrc) {
    auto t = TryTranslate({Sop1(sop1Opcode, sdst, ssrc)}, 32u);
    ASSERT_NE(t.entry, nullptr);
    EXPECT_EQ(t.inst.op, expected);
    const std::uint32_t source = 0xf0f00ff0u;
    const std::uint32_t oldExec = 0xffff00ffu;
    std::uint32_t expectedExec = 0u;
    switch (kind) {
    case SaveexecOp::Or: expectedExec = source | oldExec; break;
    case SaveexecOp::Xor: expectedExec = source ^ oldExec; break;
    case SaveexecOp::Andn2: expectedExec = source & ~oldExec; break;  // S0 & ~EXEC
    default: FAIL() << "unexpected kind";
    }
    for (std::uint32_t lane = 0u; lane < 32u; ++lane) {
        Machine m;
        m.lane = lane;
        m.execLo = oldExec;
        m.sgpr[ssrc] = source;
        m.Run(*t.entry);
        ASSERT_TRUE(m.execLoWritten);
        EXPECT_EQ(m.execLo, expectedExec);
        EXPECT_EQ(m.sgpr[sdst], oldExec) << "destination receives the old EXEC";
        EXPECT_EQ(m.scc, expectedExec != 0u);
        ASSERT_TRUE(m.execBitWritten);
        EXPECT_EQ(m.execBit, ((expectedExec >> lane) & 1u) != 0u) << "lane " << lane;
    }
}

// s_or_saveexec_b32 (SOP1 0x3d), s_xor_saveexec_b32 (0x3e), s_andn2_saveexec_b32 (0x3f): the wave32 counterparts of the
// 64-bit forms. ANDN2 negates EXEC (the "2" operand), not S0.
TEST(RdnaPortedInstructionTests, SOrSaveexecB32) { CheckSaveexec32(0x3du, RdnaOpcode::SOrSaveexecB32, SaveexecOp::Or, 4u, 6u); }
TEST(RdnaPortedInstructionTests, SXorSaveexecB32) { CheckSaveexec32(0x3eu, RdnaOpcode::SXorSaveexecB32, SaveexecOp::Xor, 4u, 6u); }
TEST(RdnaPortedInstructionTests, SAndn2SaveexecB32) { CheckSaveexec32(0x3fu, RdnaOpcode::SAndn2SaveexecB32, SaveexecOp::Andn2, 4u, 6u); }

// Same read-before-write ordering rule as the and-form: `s_or_saveexec_b32 s6, s6` is EXEC | old S0.
TEST(RdnaPortedInstructionTests, SOrSaveexecB32AliasedSourceReadsBeforeWrite) {
    auto t = TryTranslate({Sop1(0x3du, 6u, 6u)}, 32u);
    ASSERT_NE(t.entry, nullptr);
    Machine m;
    m.execLo = 0x0000ffffu;
    m.sgpr[6] = 0x00f000f0u;
    m.Run(*t.entry);
    EXPECT_EQ(m.execLo, 0x00f0fff0u | 0x0000000fu);  // old S0 | old EXEC, not EXEC | EXEC
    EXPECT_EQ(m.sgpr[6], 0x0000ffffu);
}

// s_bcnt0_i32_b64 (SOP1 0x0e): D = number of zero bits of the 64-bit source, SCC = (D != 0).
// s_ff0_i32_b64  (SOP1 0x12): D = index of the lowest zero bit, -1 if all 64 bits are ones; SCC untouched.
TEST(RdnaPortedInstructionTests, SBcnt0AndSFf0B64CountZeroBitsOfSixtyFourBitSource) {
    auto bcnt = TryTranslate({Sop1(0x0eu, 4u, 2u)});
    ASSERT_NE(bcnt.entry, nullptr);
    auto ff0 = TryTranslate({Sop1(0x12u, 4u, 2u)});
    ASSERT_NE(ff0.entry, nullptr);
    EXPECT_EQ(bcnt.inst.op, RdnaOpcode::SBcnt0I32B64);
    EXPECT_EQ(ff0.inst.op, RdnaOpcode::SFf0I32B64);
    for (const std::uint64_t x : {0x0000000000000000ull, 0xffffffffffffffffull, 0x0000000000000001ull, 0x00000000ffffffffull,
                                  0xffffffff00000000ull, 0xffffffff7fffffffull, 0x7fffffffffffffffull, 0xfffffffffffffff7ull,
                                  0xf0f0f0f0deadbeefull, 0x0123456789abcdefull}) {
        Machine a;
        a.sgpr[2] = static_cast<std::uint32_t>(x);
        a.sgpr[3] = static_cast<std::uint32_t>(x >> 32u);
        a.scc = (x == ~0ull);  // pre-set to the opposite of the expected outcome for the all-ones case
        a.Run(*bcnt.entry);
        const auto zeros = static_cast<std::uint32_t>(std::popcount(~x));
        EXPECT_EQ(a.sgpr[4], zeros) << std::hex << x;
        EXPECT_EQ(a.scc, zeros != 0u) << std::hex << x;
        EXPECT_TRUE(a.sccWritten);
        EXPECT_FALSE(a.sgprWritten[5]) << "the result is one dword";

        Machine b;
        b.sgpr[2] = static_cast<std::uint32_t>(x);
        b.sgpr[3] = static_cast<std::uint32_t>(x >> 32u);
        b.scc = true;
        b.Run(*ff0.entry);
        const std::uint32_t expected = x == ~0ull ? 0xffffffffu : static_cast<std::uint32_t>(std::countr_zero(~x));
        EXPECT_EQ(b.sgpr[4], expected) << std::hex << x;
        EXPECT_FALSE(b.sccWritten);
    }
}

// ---------------------------------------------------------------------------------------------------------
// SPIR-V inspection helpers (a minimal word-stream walker; there is no disassembler in the test binary)
// ---------------------------------------------------------------------------------------------------------

struct SpirvInstr {
    std::uint32_t opcode;
    const std::uint32_t* words;  // words[0] is the opcode/word-count word
    std::uint32_t count;
};

// Splits a SPIR-V module (5-word header, then instructions) into instructions. An instruction with a zero word count
// would loop forever, so it ends the walk (the validator rejects such a module before it can be returned).
std::vector<SpirvInstr> ParseSpirv(const std::vector<std::uint32_t>& spirv) {
    std::vector<SpirvInstr> out;
    for (std::size_t at = 5u; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        if (count == 0u || at + count > spirv.size()) break;
        out.push_back({spirv[at] & 0xffffu, &spirv[at], count});
        at += count;
    }
    return out;
}

// Recompile() that reports a thrown error (decode, validation or emission failure) as a test failure and returns an
// empty module, for the same reason as TryTranslate: an exception escaping a TEST body would end the whole binary.
std::vector<std::uint32_t> CompileOrFail(const RecompileRequest& request) {
    try {
        return Recompile(request).spirv;
    } catch (const std::exception& error) {
        ADD_FAILURE() << "Recompile threw: " << error.what();
        return {};
    }
}

// ---------------------------------------------------------------------------------------------------------
// GLC/DLC coherent buffer accesses
// ---------------------------------------------------------------------------------------------------------

// Compiles `v_mov_b32 v0, 1 ; buffer_store_dword v0, v1, s[8:11], null <flags> ; s_endpgm` as a compute shader and
// returns its SPIR-V. word0 flag bits: GLC = bit 14, DLC = bit 15 (RDNA2 MUBUF).
std::vector<std::uint32_t> CompileBufferStore(std::uint32_t word0Flags) {
    const Golden::SyntheticCase testCase{Golden::SyntheticFamily::Mubuf, "mubuf_store_cache_policy",
        {0x7E000281u, 0xE0700000u | word0Flags, 0x7D020001u, 0xBF810000u},
        {static_cast<std::uint32_t>(Golden::kMubufBase), 0u, 0xFFFFFFFFu, 0u}, 8u, {Golden::ZeroRegion(Golden::kMubufBase, 256u)}, ShaderStage::Compute};
    auto owned = Golden::MakeRequest(testCase, 64u, 64u);
    return CompileOrFail(owned.request);
}

// Counts the module's Coherent decorations and the OpStore/OpLoad instructions carrying the Volatile memory operand.
struct CoherenceStats {
    std::uint32_t coherentDecorations = 0u;
    std::uint32_t volatileStores = 0u;
    std::uint32_t volatileLoads = 0u;
};
CoherenceStats MeasureCoherence(const std::vector<std::uint32_t>& spirv) {
    CoherenceStats stats;
    for (const SpirvInstr& inst : ParseSpirv(spirv)) {
        if (inst.opcode == spv::OpDecorate && inst.count >= 3u && inst.words[2] == spv::DecorationCoherent) ++stats.coherentDecorations;
        // OpStore: pointer, object, [memory access mask]. OpLoad: result type, result id, pointer, [memory access mask].
        if (inst.opcode == spv::OpStore && inst.count >= 4u && (inst.words[3] & spv::MemoryAccessVolatileMask) != 0u) ++stats.volatileStores;
        if (inst.opcode == spv::OpLoad && inst.count >= 5u && (inst.words[4] & spv::MemoryAccessVolatileMask) != 0u) ++stats.volatileLoads;
    }
    return stats;
}

// RDNA2 ISA: a buffer access with GLC (L0) or DLC (GL1) bypasses the caches another workgroup's stores may be stale in,
// so a look-back that polls a flag published by another workgroup sees it. The recompiler maps that to a Volatile
// access to a Coherent storage buffer. Invariants: a GLC store, a DLC store, and both together are coherent; a plain
// store is not (no Coherent decoration and no Volatile access, so ordinary buffer traffic keeps its freedom to be
// cached and combined). Failure mode without the feature: every case is incoherent.
TEST(RdnaPortedInstructionTests, GlcAndDlcBufferStoresAreCoherentVolatileAccesses) {
    constexpr std::uint32_t kGlc = 1u << 14u;
    constexpr std::uint32_t kDlc = 1u << 15u;
    const CoherenceStats plain = MeasureCoherence(CompileBufferStore(0u));
    EXPECT_EQ(plain.coherentDecorations, 0u);
    EXPECT_EQ(plain.volatileStores, 0u);
    for (const std::uint32_t flags : {kGlc, kDlc, kGlc | kDlc}) {
        const CoherenceStats stats = MeasureCoherence(CompileBufferStore(flags));
        EXPECT_GE(stats.coherentDecorations, 1u) << "flags 0x" << std::hex << flags;
        EXPECT_GE(stats.volatileStores, 1u) << "flags 0x" << std::hex << flags;
    }
}

// The decoder must read DLC from MUBUF word 0 bit 15 (and GLC from bit 14) independently.
TEST(RdnaPortedInstructionTests, MubufDecoderReadsGlcAndDlcBits) {
    for (const std::uint32_t flags : {0u, 1u << 14u, 1u << 15u, (1u << 14u) | (1u << 15u)}) {
        const std::vector<std::uint32_t> code{0xE0700000u | flags, 0x7D020001u, 0xBF810000u};
        const RdnaInstruction inst = DecodeRdnaInstruction(0u, std::span<const std::uint32_t>(code), 0u);
        EXPECT_EQ(inst.glc, (flags & (1u << 14u)) != 0u);
        EXPECT_EQ(inst.dlc, (flags & (1u << 15u)) != 0u);
    }
}

// ---------------------------------------------------------------------------------------------------------
// MRT export formats (SPI_SHADER_COL_FORMAT)
// ---------------------------------------------------------------------------------------------------------

// Resolves a SPIR-V id to a 32-bit constant through OpBitcast, OpCompositeExtract of an OpCompositeConstruct or
// OpConstantComposite, and OpConstant (the optimizer may fold the first two away). Returns nullopt when the value is
// not a compile-time constant (for example a value loaded at run time).
class SpirvConstants {
public:
    explicit SpirvConstants(const std::vector<std::uint32_t>& spirv) : instrs_(ParseSpirv(spirv)) {
        for (const SpirvInstr& inst : instrs_) {
            // Result id position: types/constants/ops put (type, id, ...) in words 1-2, except OpVariable-like ids too.
            if (inst.count >= 3u && HasResultAndType(inst.opcode)) byId_[inst.words[2]] = &inst;
        }
    }
    std::optional<std::uint32_t> Scalar(std::uint32_t id) const {
        const auto found = byId_.find(id);
        if (found == byId_.end()) return std::nullopt;
        const SpirvInstr& inst = *found->second;
        switch (inst.opcode) {
        case spv::OpConstant: return inst.words[3];
        case spv::OpBitcast: return Scalar(inst.words[3]);
        case spv::OpCompositeExtract: return inst.count == 5u ? Element(inst.words[3], inst.words[4]) : std::nullopt;
        default: return std::nullopt;
        }
    }
    std::optional<std::uint32_t> Element(std::uint32_t vectorId, std::uint32_t index) const {
        const auto found = byId_.find(vectorId);
        if (found == byId_.end()) return std::nullopt;
        const SpirvInstr& inst = *found->second;
        switch (inst.opcode) {
        case spv::OpCompositeConstruct:
        case spv::OpConstantComposite: return 3u + index < inst.count ? Scalar(inst.words[3u + index]) : std::nullopt;
        case spv::OpBitcast: return Element(inst.words[3], index);
        default: return std::nullopt;
        }
    }
    const std::vector<SpirvInstr>& Instructions() const { return instrs_; }

private:
    static bool HasResultAndType(std::uint32_t opcode) {
        return opcode == spv::OpConstant || opcode == spv::OpConstantComposite || opcode == spv::OpBitcast ||
               opcode == spv::OpCompositeExtract || opcode == spv::OpCompositeConstruct || opcode == spv::OpVectorShuffle ||
               opcode == spv::OpExtInst || opcode == spv::OpLoad;
    }
    std::vector<SpirvInstr> instrs_;
    std::map<std::uint32_t, const SpirvInstr*> byId_;
};

// Compiles `v_mov v0..v3 <distinct literals> ; exp mrt0 v0..v3 (en 0xf, done, optionally compr) ; s_endpgm` as a pixel
// shader whose MRT0 SPI_SHADER_COL_FORMAT is `colFormat`, and returns the SPIR-V.
std::vector<std::uint32_t> CompilePixelMrtExport(std::uint32_t colFormat, bool compressed) {
    constexpr std::uint32_t kMovLiteral = 0x7E0002FFu;  // v_mov_b32 v<d>, literal: VOP1 op 1, src0 = 255, vdst in [24:17]
    const std::uint32_t exp0 = 0xF800080Fu | (compressed ? 1u << 10u : 0u);  // target 0 (MRT0), en 0xf, done
    const Golden::SyntheticCase testCase{Golden::SyntheticFamily::Exp, "pixel_mrt_export",
        {kMovLiteral | (0u << 17u), 0x11111111u, kMovLiteral | (1u << 17u), 0x22222222u, kMovLiteral | (2u << 17u), 0x33333333u,
         kMovLiteral | (3u << 17u), 0x44444444u, exp0, 0x03020100u, 0xBF810000u},
        {}, 8u, {}, ShaderStage::Fragment};
    auto owned = Golden::MakeRequest(testCase, 64u, 64u);
    ShaderPixelStageInfo pixel{};
    pixel.targetOutputMode.fill(0u);
    pixel.targetOutputMode[0] = static_cast<std::uint8_t>(colFormat);
    pixel.targetExportMapping.fill(0xe4u);  // identity component mapping
    owned.request.context.compute.reset();
    owned.request.context.pixel = pixel;
    return CompileOrFail(owned.request);
}

// Returns the constant bits of the four components stored to the module's fragment color output, or nullopt per
// component when it is not a compile-time constant.
std::array<std::optional<std::uint32_t>, 4> StoredColorComponents(const std::vector<std::uint32_t>& spirv) {
    const SpirvConstants constants(spirv);
    std::array<std::optional<std::uint32_t>, 4> result{};
    std::map<std::uint32_t, bool> outputVariable;
    for (const SpirvInstr& inst : constants.Instructions()) {
        // OpVariable: result type, result id, storage class. Output = 3.
        if (inst.opcode == spv::OpVariable && inst.count >= 4u && inst.words[3] == spv::StorageClassOutput) outputVariable[inst.words[2]] = true;
    }
    for (const SpirvInstr& inst : constants.Instructions()) {
        if (inst.opcode != spv::OpStore || inst.count < 3u || !outputVariable.count(inst.words[1])) continue;
        for (std::uint32_t component = 0; component < 4u; ++component) result[component] = constants.Element(inst.words[2], component);
    }
    return result;
}

// SPI_SHADER_COL_FORMAT 32_R (1), 32_GR (2) and 32_AR (3) export only those components of a 32-bit export; the other
// components read as 0 (alpha as 1) however many the export's EN mask enables. 32_ABGR (9) exports all four.
// Test: distinct literals 0x11111111..0x44444444 in v0..v3 are exported to MRT0 and the stored color is inspected.
// Failure mode without the mask: modes 1-3 store all four literals.
TEST(RdnaPortedInstructionTests, MrtExportFormatMasksUnexportedComponents) {
    constexpr std::uint32_t kOne = 0x3f800000u;
    struct Expect {
        std::uint32_t format;
        std::array<std::uint32_t, 4> bits;
    };
    const Expect expectations[] = {
        {9u, {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u}},  // 32_ABGR
        {1u, {0x11111111u, 0u, 0u, kOne}},                           // 32_R
        {2u, {0x11111111u, 0x22222222u, 0u, kOne}},                  // 32_GR
        {3u, {0x11111111u, 0u, 0u, 0x44444444u}},                    // 32_AR
    };
    for (const Expect& expect : expectations) {
        const auto stored = StoredColorComponents(CompilePixelMrtExport(expect.format, false));
        for (std::uint32_t component = 0; component < 4u; ++component) {
            ASSERT_TRUE(stored[component].has_value()) << "format " << expect.format << " component " << component;
            EXPECT_EQ(*stored[component], expect.bits[component]) << "format " << expect.format << " component " << component;
        }
    }
}

// A compressed (two 16-bit values per dword) export is unpacked by the SPI_SHADER_COL_FORMAT of its target: 5 UNORM16,
// 6 SNORM16, anything else FP16. SNORM16 used to take the half-float path, which reads the wrong values.
TEST(RdnaPortedInstructionTests, CompressedMrtExportUnpacksPerColorFormat) {
    const auto unpackOps = [](const std::vector<std::uint32_t>& spirv) {
        std::vector<std::uint32_t> ops;
        for (const SpirvInstr& inst : ParseSpirv(spirv)) {
            // OpExtInst: result type, result id, set, instruction number, operands.
            if (inst.opcode != spv::OpExtInst || inst.count < 5u) continue;
            const std::uint32_t number = inst.words[4];
            if (number == GLSLstd450UnpackSnorm2x16 || number == GLSLstd450UnpackUnorm2x16 || number == GLSLstd450UnpackHalf2x16) ops.push_back(number);
        }
        return ops;
    };
    const auto snorm = unpackOps(CompilePixelMrtExport(6u, true));
    const auto unorm = unpackOps(CompilePixelMrtExport(5u, true));
    const auto half = unpackOps(CompilePixelMrtExport(4u, true));
    ASSERT_FALSE(snorm.empty());
    ASSERT_FALSE(unorm.empty());
    ASSERT_FALSE(half.empty());
    for (const auto op : snorm) EXPECT_EQ(op, static_cast<std::uint32_t>(GLSLstd450UnpackSnorm2x16));
    for (const auto op : unorm) EXPECT_EQ(op, static_cast<std::uint32_t>(GLSLstd450UnpackUnorm2x16));
    for (const auto op : half) EXPECT_EQ(op, static_cast<std::uint32_t>(GLSLstd450UnpackHalf2x16));
}

// ---------------------------------------------------------------------------------------------------------
// Structurizer: a selection whose arm continues
// ---------------------------------------------------------------------------------------------------------

// Smallest graph where the nearest common post-dominator of a selection is computed across a loop's back edge:
//   0 -> 1 (loop header) -> 2;  2: 5 | 3;  3: 5 | 4;  4 -> 7 (continue);  5: 6 (exit) | 7;  6 returns;  7 -> 1.
// Every path from 4 passes 5 only around the back edge, so 5 post-dominates both selections although arm 4 merely
// continues. The merge of block 3 must then be where its other arm goes (5, within this iteration), and splitting the
// merge shared with block 2 must terminate. Without the fix the split repeats until the budget throws.
TEST(RdnaPortedInstructionTests, StructurizerTerminatesOnSelectionWhoseArmContinues) {
    const std::vector<std::vector<std::uint32_t>> successors{{1}, {2}, {5, 3}, {5, 4}, {7}, {6, 7}, {}, {1}};
    ControlFlowGraph graph;
    graph.entryBlock = 0;
    for (std::uint32_t id = 0; id < successors.size(); ++id) {
        BasicBlock block;
        block.id = id;
        block.startProgramCounter = id * 8u;
        block.endProgramCounter = id * 8u + 8u;
        block.instructionBegin = id * 2u;
        block.instructionEnd = id * 2u + 2u;
        block.successors = successors[id];
        auto& terminator = block.terminator;
        if (successors[id].empty()) {
            terminator.kind = TerminatorKind::Return;
        } else if (successors[id].size() == 1u) {
            terminator.kind = TerminatorKind::Branch;
            terminator.trueBlock = successors[id][0];
        } else {
            terminator.kind = TerminatorKind::ConditionalBranch;
            terminator.condition = BranchCondition::SccNonZero;
            terminator.trueBlock = successors[id][0];
            terminator.falseBlock = successors[id][1];
        }
        graph.blocks.push_back(std::move(block));
    }
    for (const auto& block : graph.blocks) {
        for (const auto successor : block.successors) graph.blocks[successor].predecessors.push_back(block.id);
    }
    ASSERT_NO_THROW(Structurizer{}.Structurize(graph));
    EXPECT_NE(graph.FindBlock(2).terminator.mergeBlock, graph.FindBlock(3).terminator.mergeBlock) << "the nested selections must not share a merge block";
}

// ---------------------------------------------------------------------------------------------------------
// BDA access inside a loop's continue construct
// ---------------------------------------------------------------------------------------------------------

// A guest-memory (BDA) access whose lookup fails ends the invocation. SPIR-V requires a loop's back-edge block to
// post-dominate the continue target structurally, so an early return inside the continue construct makes the module
// invalid ("The continue construct ... is not structurally post dominated by the back-edge block"). The recompiler
// must therefore record the fault without returning while it emits a continue target (the access then reads zero).
// Test: a single-block counting loop whose body loads through a flat (BDA) address; the body is the continue target:
//   s_mov_b32 s0, 0 ; L: flat_load_dword v0, v[1:2] ; s_waitcnt 0 ; s_add_u32 s0, s0, 1 ; s_cmp_lt_u32 s0, 4 ;
//   s_cbranch_scc1 L ; exp pos0 v0..v3 done ; s_endpgm
// Failure mode without the fix: Recompile throws "SPIR-V validation before optimization failed".
// Known, separate limitation: with SPIRV-Tools enabled (the ci preset) the optimizer's merge-blocks pass then rejects the
// same loop for any continue target that contains a selection (reproduced with a plain buffer store, no BDA access), so
// this test asserts only that the *emitted* module passes validation. See docs/spec/shader-recompiler.md, open question
// on loop continue targets.
TEST(RdnaPortedInstructionTests, BdaAccessInContinueTargetEmitsValidModule) {
    const Golden::SyntheticCase testCase{Golden::SyntheticFamily::Flat, "flat_load_in_loop",
        {0xBE800380u, 0xDC300000u, 0x00000001u, 0xBF8C0000u, 0x80008100u, 0xBF0A8400u, 0xBF85FFFAu, 0xF80008CFu, 0x03020100u, 0xBF810000u},
        {}, 8u, {}, ShaderStage::Vertex};
    auto owned = Golden::MakeRequest(testCase, 64u, 64u);
    owned.request.target.bdaAbiVersion = BdaAbi::Version;
    static constexpr std::array<std::uint32_t, 3> kCapabilities{11u, 5347u, 4448u};
    static constexpr std::array<std::string_view, 2> kExtensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    owned.request.target.supportedCapabilities = kCapabilities;
    owned.request.target.supportedExtensions = kExtensions;
    try {
        EXPECT_FALSE(Recompile(owned.request).spirv.empty());
    } catch (const std::exception& error) {
        const std::string message = error.what();
        EXPECT_EQ(message.find("validation before optimization"), std::string::npos) << message;
    }
}

// Emits the request's module with the real pipeline stages but WITHOUT the SPIRV-Tools validate/optimize step of
// Recompile (a flat load needs no resource plan, so the materializer is skipped). Lets a test inspect what the emitter
// produced even where the optimizer rejects it (see the continue-target limitation above).
std::vector<std::uint32_t> EmitUnoptimized(const RecompileRequest& request) {
    IrProgram program = PrepareResourceProgram(request);
    const auto inputInfo = BuildShaderStageInputInfo(ShaderStageKind::Vertex, request.context);
    DeadCodeEliminator{}.RemoveIdentities(program);
    DeadCodeEliminator{}.Eliminate(program);
    ShaderInfoCollector{}.Collect(program, inputInfo);
    auto bindings = BindingAllocator{}.Allocate(program, request.layout);
    DescriptorBindingBuilder{}.Populate(bindings, program, ResourceSnapshot{});
    SpirvTargetOptions options{};
    options.vulkanVersion = request.target.vulkanVersion;
    options.spirvVersion = request.target.spirvVersion;
    options.subgroupSize = request.target.subgroupSize;
    options.bdaAbiVersion = request.target.bdaAbiVersion;
    options.supportedCapabilities = request.target.supportedCapabilities;
    options.supportedExtensions = request.target.supportedExtensions;
    return SpirvEmitter{}.Emit(program, inputInfo, bindings, options);
}

// Counts OpSelect instructions that choose the all-ones 64-bit constant (the poisoned address).
std::uint32_t CountPoisonedAddressSelects(const std::vector<std::uint32_t>& spirv) {
    std::uint32_t allOnesId = 0u;
    const auto instrs = ParseSpirv(spirv);
    for (const SpirvInstr& inst : instrs) {
        // OpConstant: result type, result id, low word, high word (64-bit).
        if (inst.opcode == spv::OpConstant && inst.count == 5u && inst.words[3] == 0xffffffffu && inst.words[4] == 0xffffffffu) allOnesId = inst.words[2];
    }
    std::uint32_t selects = 0u;
    for (const SpirvInstr& inst : instrs) {
        // OpSelect: result type, result id, condition, object 1, object 2.
        if (allOnesId != 0u && inst.opcode == spv::OpSelect && inst.count == 6u && inst.words[4] == allOnesId) ++selects;
    }
    return selects;
}

// With the invocation stop suppressed (loop continue target) an overflowing address add or subtract only records the
// fault; its wrapped result could land on a mapped page and read real bytes from the wrong place. The emitter must
// poison the wrapped address (all ones, which the read's own overflow check turns into a zero read) whenever it keeps
// going. Test: the same counting loop as above with a flat load at +4 (an address add that can overflow). Outside a
// continue target the stop returns from the invocation, so no poison select is needed there.
// Failure mode without the fix: no poisoned-address select in the loop module.
TEST(RdnaPortedInstructionTests, WrappedBdaAddressInContinueTargetIsPoisoned) {
    const auto build = [](bool inLoop) {
        std::vector<std::uint32_t> code;
        if (inLoop) {
            code = {0xBE800380u, 0xDC300004u, 0x00000001u, 0xBF8C0000u, 0x80008100u, 0xBF0A8400u, 0xBF85FFFAu, 0xF80008CFu, 0x03020100u, 0xBF810000u};
        } else {
            code = {0xDC300004u, 0x00000001u, 0xBF8C0000u, 0xF80008CFu, 0x03020100u, 0xBF810000u};
        }
        const Golden::SyntheticCase testCase{Golden::SyntheticFamily::Flat, "flat_load_offset", code, {}, 8u, {}, ShaderStage::Vertex};
        auto owned = Golden::MakeRequest(testCase, 64u, 64u);
        owned.request.target.bdaAbiVersion = BdaAbi::Version;
        static constexpr std::array<std::uint32_t, 3> kCapabilities{11u, 5347u, 4448u};
        static constexpr std::array<std::string_view, 2> kExtensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
        owned.request.target.supportedCapabilities = kCapabilities;
        owned.request.target.supportedExtensions = kExtensions;
        try {
            return CountPoisonedAddressSelects(EmitUnoptimized(owned.request));
        } catch (const std::exception& error) {
            ADD_FAILURE() << "emit threw: " << error.what();
            return 0u;
        }
    };
    EXPECT_GE(build(true), 1u) << "the overflowing add in the continue target must be poisoned";
    EXPECT_EQ(build(false), 0u) << "outside a continue target the invocation stops instead";
}

} // namespace
} // namespace ShaderRecompiler
