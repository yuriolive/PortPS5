// GoogleTest suite for the EXTRQ/INSERTQ register-form Intel lowering used by
// `--to-intel` (Sse4aLowering, Amd64OnlyInstructionMatcher).
//
// Subsystem: relinker codegen. Hosted-CI safe: it needs no GPU and no game
// data. Every input is a synthetic, hand-assembled instruction. The lowered
// stub bodies are EXECUTED on the host CPU inside the generated harness thunk
// of Sse4aExecutionHarness.hpp, which also checks that XMM registers, flags and
// the red zone are preserved. The harness is host-independent (the stubs use
// SSE2 only). When the host CPU implements SSE4a (AMD), the original
// instruction is also executed natively and used as the ground truth for the
// architectural reference model; on other hosts that test is skipped.
#include <codegen/CodegenException.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/Sse4aLowering.hpp>
#include <codegen/x86/Sse4aOperands.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Sse4aExecutionHarness.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;
#if defined(__x86_64__)
using namespace Sse4aHarness;
#endif

// Builds `66|F2 [REX] 0F 79 /r` (register form) for xmm<dst>, xmm<src>.
Bytes RegisterFormSite(const bool insertq, const int dst, const int src) {
    Bytes out{static_cast<std::uint8_t>(insertq ? 0xF2 : 0x66)};
    const auto rex = static_cast<std::uint8_t>(0x40 | ((dst & 8) ? 4 : 0) | ((src & 8) ? 1 : 0));
    if (rex != 0x40)
        out.push_back(rex);
    out.push_back(0x0F);
    out.push_back(0x79);
    out.push_back(static_cast<std::uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7)));
    return out;
}

// Architectural EXTRQ xmm1, xmm2 (AMD APM vol. 4): the low 64 bits of the
// result. `control` is xmm2[63:0]; length = bits 5:0 (0 means 64), index = bits
// 13:8. Valid only for index + length <= 64 (the rest is architecturally undefined).
std::uint64_t ExtrqModel(const std::uint64_t value, const std::uint64_t control) {
    unsigned length = control & 0x3F;
    const unsigned index = (control >> 8) & 0x3F;
    if (length == 0)
        length = 64;
    const auto shifted = value >> index;
    return length == 64 ? shifted : shifted & ((std::uint64_t{1} << length) - 1);
}

// Architectural INSERTQ xmm1, xmm2: low 64 bits of the result. `control` is
// xmm2[127:64]. Valid only for index + length <= 64.
std::uint64_t InsertqModel(const std::uint64_t destination, const std::uint64_t value, const std::uint64_t control) {
    unsigned length = control & 0x3F;
    const unsigned index = (control >> 8) & 0x3F;
    if (length == 0)
        length = 64;
    const auto mask = length == 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << length) - 1);
    return (destination & ~(mask << index)) | ((value & mask) << index);
}

struct Field {
    unsigned Length;  // 0 encodes 64
    unsigned Index;
};

// (length, index) pairs that are architecturally defined (index + length <= 64),
// covering the boundaries: full width, single bits at both ends, byte aligned
// and unaligned fields, and the length == 0 (== 64) encoding.
const std::array<Field, 12> kFields = {{
    {0, 0}, {1, 0}, {1, 63}, {8, 4}, {16, 48}, {32, 32}, {63, 1}, {40, 20}, {8, 0}, {8, 56}, {5, 17}, {33, 31},
}};

// Control value with the field encoded and junk in every ignored bit
// (7:6 and 63:14), which hardware must mask off.
std::uint64_t Control(const Field f, const std::uint64_t junk) {
    return static_cast<std::uint64_t>(f.Length) | (static_cast<std::uint64_t>(f.Index) << 8) | (junk & ~0x3F3Full);
}

const std::array<std::pair<int, int>, 8> kRegisterPairs = {{
    {1, 5}, {5, 1}, {2, 2}, {9, 10}, {0, 15}, {15, 15}, {14, 7}, {7, 14},
}};

// ---- matcher / decoder level (no execution) ------------------------------

// Invariant: register-form EXTRQ/INSERTQ are lowered through a stub (never
// InPlace, never Unsupported), named distinctly, and the stub follows the
// documented layout (red-zone skip first, return branch placeholder inside).
// Failure mode: a register form returning Residual/Unsupported would leave an
// AMD-only instruction (#UD on Intel) in the converted image.
TEST(Sse4aRegisterForm, MatcherLowersBothFormsThroughAStub) {
    const auto matcher = Codegen::MakeAmd64OnlyInstructionMatcher();
    for (const bool insertq : {false, true}) {
        const auto site = RegisterFormSite(insertq, 3, 4);
        const auto match = matcher->Match(site.data(), site.size());
        ASSERT_TRUE(match.has_value());
        EXPECT_EQ(match->Lowering, Codegen::Amd64OnlyLowering::Trampoline);
        EXPECT_EQ(match->InstructionName, insertq ? "INSERTQ register form" : "EXTRQ register form");
        EXPECT_EQ(match->Length, site.size());
        ASSERT_GT(match->StubBody.size(), match->ReturnBranchOffset + 5);
        EXPECT_EQ(match->StubBody[0], 0x48) << "stub must start with the red-zone skip (lea rsp)";
        EXPECT_EQ(match->StubBody[match->ReturnBranchOffset], 0xE9);
        EXPECT_EQ(match->StubBody.size() % 16, 0u) << "constants are read with aligned m128 operands";
    }
}

// Invariant: MatchSequence lowers only runs made entirely of EXTRQ/INSERTQ and
// reports the first instruction's name and length. Anything else (an ordinary
// instruction, an AMD-only non-SSE4a one, or an empty run) is nullopt so the
// converter refuses the extension instead of mis-lowering.
TEST(Sse4aRegisterForm, MatchSequenceAcceptsOnlySse4aRuns) {
    const auto matcher = Codegen::MakeAmd64OnlyInstructionMatcher();
    const auto extrq = RegisterFormSite(false, 1, 2);
    const auto insertq = RegisterFormSite(true, 3, 4);
    const Bytes nop = {0x90};
    const Bytes monitorx = {0x0F, 0x01, 0xFA};

    const std::array<std::span<const std::uint8_t>, 2> good = {std::span<const std::uint8_t>(extrq), std::span<const std::uint8_t>(insertq)};
    const auto match = matcher->MatchSequence(good, {});
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->Lowering, Codegen::Amd64OnlyLowering::Trampoline);
    EXPECT_EQ(match->InstructionName, "EXTRQ register form");
    EXPECT_EQ(match->Length, extrq.size());

    const std::array<std::span<const std::uint8_t>, 2> withNop = {std::span<const std::uint8_t>(extrq), std::span<const std::uint8_t>(nop)};
    EXPECT_FALSE(matcher->MatchSequence(withNop, {}).has_value());
    const std::array<std::span<const std::uint8_t>, 1> withMonitorx = {std::span<const std::uint8_t>(monitorx)};
    EXPECT_FALSE(matcher->MatchSequence(withMonitorx, {}).has_value());
    EXPECT_FALSE(matcher->MatchSequence({}, {}).has_value());
}

// Invariant: a stub holds each distinct constant once. The register forms use
// the same 0x3F mask four times; storing it four times would waste stub space
// in every converted image (hundreds of sites). Failure mode: body grows by
// 16 bytes per duplicate.
TEST(Sse4aRegisterForm, StubSharesIdenticalConstants) {
    const Codegen::Sse4aLowering lowering;
    const auto extrq = lowering.LowerOutOfLine(Codegen::Sse4aOperands{false, true, 1, 2, 0, 0});
    // Layout: code, 5-byte return branch, padding to 16, then constants {0x3F, 1}.
    const auto constantsBegin = (extrq.ReturnBranchOffset + 5 + 15) / 16 * 16;
    EXPECT_EQ(extrq.Bytes.size() - constantsBegin, 2u * 16u) << "expected exactly two 16-byte constants";
}

#if defined(__x86_64__)

// ---- execution level -----------------------------------------------------

// Invariant: the lowered EXTRQ register form produces the architectural result
// in the destination's low quadword for every defined (length, index), with
// junk in the ignored control bits, for distinct and identical operands and for
// registers above xmm7 (REX-encoded). It must leave every other XMM register,
// the flags and the red zone untouched, and must return with the stack
// balanced (the thunk's RET would crash otherwise).
// Failure mode: wrong shift counts (e.g. a missing mask of the ignored bits, or
// 64-length mishandled for length == 0) or a clobbered scratch register.
TEST(Sse4aRegisterForm, ExtrqStubMatchesArchitecturalModel) {
    const Codegen::Sse4aLowering lowering;
    constexpr std::uint64_t kValue = 0x9E3779B97F4A7C15ull;
    for (const auto [dst, src] : kRegisterPairs) {
        const auto stub = lowering.LowerOutOfLine(Codegen::Sse4aOperands{false, true, static_cast<std::uint8_t>(dst), static_cast<std::uint8_t>(src), 0, 0});
        const auto middle = StubMiddle(stub);
        for (const auto field : kFields) {
            const auto control = Control(field, 0xDEADBEEFCAFE0000ull);
            auto in = CanaryInput();
            in.Xmm[dst][0] = kValue;
            in.Xmm[src][0] = control;  // when dst == src the control IS the value (src written last)
            const auto out = RunThunk(middle, in);
            const auto expected = ExtrqModel(in.Xmm[dst][0], in.Xmm[src][0]);
            EXPECT_EQ(out.Xmm[dst][0], expected) << "dst=" << dst << " src=" << src << " length=" << field.Length << " index=" << field.Index;
            for (int n = 0; n < 16; ++n) {
                if (n == dst)
                    continue;  // the upper quadword of the destination is architecturally undefined
                EXPECT_EQ(out.Xmm[n][0], n == src ? in.Xmm[src][0] : in.Xmm[n][0]) << "xmm" << n << " low clobbered";
                EXPECT_EQ(out.Xmm[n][1], in.Xmm[n][1]) << "xmm" << n << " high clobbered";
            }
            EXPECT_EQ(out.Flags & kFlagMask, kFlagsIn & kFlagMask) << "flags changed";
            for (int k = 1; k <= 16; ++k)
                EXPECT_EQ(out.RedZone[k - 1], RedZonePattern(k)) << "red zone slot " << k << " overwritten";
        }
    }
}

// Invariant: the lowered INSERTQ register form inserts src[length-1:0] at
// `index` (length/index from src[69:64] and src[77:72]) into the destination,
// matching the architectural model, keeps the bits outside the field, zeroes
// the destination's undefined upper quadword (documented choice), and preserves
// all other XMM state, flags and the red zone.
// Failure mode: wrong hole mask (bits outside the field changed), control read
// from the wrong quadword, or a clobbered scratch register.
TEST(Sse4aRegisterForm, InsertqStubMatchesArchitecturalModel) {
    const Codegen::Sse4aLowering lowering;
    constexpr std::uint64_t kValue = 0x9E3779B97F4A7C15ull;
    constexpr std::uint64_t kDestination = 0x0F1E2D3C4B5A6978ull;
    for (const auto [dst, src] : kRegisterPairs) {
        const auto stub = lowering.LowerOutOfLine(Codegen::Sse4aOperands{true, true, static_cast<std::uint8_t>(dst), static_cast<std::uint8_t>(src), 0, 0});
        const auto middle = StubMiddle(stub);
        for (const auto field : kFields) {
            const auto control = Control(field, 0xC0FFEE00DEAD0000ull);
            auto in = CanaryInput();
            in.Xmm[dst][0] = kDestination;
            in.Xmm[src][0] = kValue;
            in.Xmm[src][1] = control;
            // dst == src: the same register supplies the value (low) and the control (high);
            // the three assignments above already wrote exactly that.
            const auto out = RunThunk(middle, in);
            const auto expected = InsertqModel(in.Xmm[dst][0], in.Xmm[src][0], in.Xmm[src][1]);
            EXPECT_EQ(out.Xmm[dst][0], expected) << "dst=" << dst << " src=" << src << " length=" << field.Length << " index=" << field.Index;
            EXPECT_EQ(out.Xmm[dst][1], 0u) << "undefined upper quadword must be zeroed";
            for (int n = 0; n < 16; ++n) {
                if (n == dst)
                    continue;
                EXPECT_EQ(out.Xmm[n][0], in.Xmm[n][0]) << "xmm" << n << " low clobbered";
                EXPECT_EQ(out.Xmm[n][1], in.Xmm[n][1]) << "xmm" << n << " high clobbered";
            }
            EXPECT_EQ(out.Flags & kFlagMask, kFlagsIn & kFlagMask) << "flags changed";
            for (int k = 1; k <= 16; ++k)
                EXPECT_EQ(out.RedZone[k - 1], RedZonePattern(k)) << "red zone slot " << k << " overwritten";
        }
    }
}

// Invariant: a run of SSE4a instructions followed by moved ordinary bytes is
// lowered in program order: first EXTRQ, then INSERTQ, then the moved
// instruction (here PADDQ xmm3, xmm3). This is the shape the converter emits
// when a 4-byte register form is extended over its successors.
// Failure mode: reordered or dropped trailing bytes, or the second lowering
// reading a register the first one clobbered.
TEST(Sse4aRegisterForm, SequenceWithTrailingBytesRunsInProgramOrder) {
    const Codegen::Sse4aLowering lowering;
    const std::array<Codegen::Sse4aOperands, 2> sequence = {
        Codegen::Sse4aOperands{false, true, 1, 5, 0, 0},  // EXTRQ xmm1, xmm5
        Codegen::Sse4aOperands{true, true, 2, 6, 0, 0},   // INSERTQ xmm2, xmm6
    };
    const Bytes paddq = {0x66, 0x0F, 0xD4, 0xDB};  // paddq xmm3, xmm3
    const auto stub = lowering.LowerOutOfLine(sequence, paddq);
    const auto middle = StubMiddle(stub);

    auto in = CanaryInput();
    const auto extrqControl = Control({8, 4}, 0);
    const auto insertqControl = Control({16, 8}, 0);
    in.Xmm[1][0] = 0x1122334455667788ull;
    in.Xmm[5][0] = extrqControl;
    in.Xmm[2][0] = 0xAAAABBBBCCCCDDDDull;
    in.Xmm[6][0] = 0x0000000000001234ull;
    in.Xmm[6][1] = insertqControl;
    const auto out = RunThunk(middle, in);
    EXPECT_EQ(out.Xmm[1][0], ExtrqModel(in.Xmm[1][0], extrqControl));
    EXPECT_EQ(out.Xmm[2][0], InsertqModel(in.Xmm[2][0], in.Xmm[6][0], insertqControl));
    EXPECT_EQ(out.Xmm[3][0], in.Xmm[3][0] * 2) << "moved PADDQ did not run exactly once";
    EXPECT_EQ(out.Xmm[3][1], in.Xmm[3][1] * 2);
}

// Invariant (hardware oracle): on a CPU that implements SSE4a, the real
// EXTRQ/INSERTQ register forms, the architectural model above and the lowered
// stubs agree on the low quadword for every defined field. This is what proves
// the model (and so the stubs) against silicon rather than against our reading
// of the manual. Skipped on hosts without SSE4a (Intel).
TEST(Sse4aRegisterForm, NativeInstructionAgreesWithModelAndStub) {
    if (!HostHasSse4a())
        GTEST_SKIP() << "host CPU does not implement SSE4a";
    const Codegen::Sse4aLowering lowering;
    constexpr std::uint64_t kValue = 0x9E3779B97F4A7C15ull;
    constexpr std::uint64_t kDestination = 0x0F1E2D3C4B5A6978ull;
    for (const auto [dst, src] : kRegisterPairs) {
        if (dst == src)
            continue;  // identical-operand semantics are covered against the model above
        for (const bool insertq : {false, true}) {
            const auto site = RegisterFormSite(insertq, dst, src);
            const auto stub = lowering.LowerOutOfLine(Codegen::Sse4aOperands{insertq, true, static_cast<std::uint8_t>(dst), static_cast<std::uint8_t>(src), 0, 0});
            const auto stubMiddle = StubMiddle(stub);
            for (const auto field : kFields) {
                const auto control = Control(field, 0x5A5A5A5A5A5A0000ull);
                auto in = CanaryInput();
                if (insertq) {
                    in.Xmm[dst][0] = kDestination;
                    in.Xmm[src][0] = kValue;
                    in.Xmm[src][1] = control;
                } else {
                    in.Xmm[dst][0] = kValue;
                    in.Xmm[src][0] = control;
                }
                const auto native = RunThunk(site, in);
                const auto lowered = RunThunk(stubMiddle, in);
                const auto expected = insertq ? InsertqModel(kDestination, kValue, control) : ExtrqModel(kValue, control);
                EXPECT_EQ(native.Xmm[dst][0], expected) << "model disagrees with hardware: insertq=" << insertq << " length=" << field.Length << " index=" << field.Index;
                EXPECT_EQ(lowered.Xmm[dst][0], native.Xmm[dst][0]) << "stub disagrees with hardware: insertq=" << insertq << " length=" << field.Length << " index=" << field.Index;
            }
        }
    }
}

#endif  // __x86_64__

}  // namespace
