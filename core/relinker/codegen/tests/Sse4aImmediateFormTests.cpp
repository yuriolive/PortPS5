// GoogleTest suite for the EXTRQ/INSERTQ immediate-form Intel lowering used by
// `--to-intel` (in-place rewrites and out-of-line stubs).
//
// Subsystem: relinker codegen. Hosted-CI safe, synthetic bytes only. For EVERY
// defined (length, index) pair (1..64 bits, index + length <= 64) the matcher's
// lowering is executed on the host CPU in the harness of
// Sse4aExecutionHarness.hpp and compared with the architectural model of the
// instruction (AMD APM vol. 4). It also checks that the lowering preserves all
// other XMM registers, the arithmetic flags and the red zone. On SSE4a hosts
// the original instruction is executed too, as ground truth for the model.
// This covers both lowering shapes: same-length in-place sequences (movq, shift,
// pblendw) and the generic stub (pshufb, shift pairs, masked merge).
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/Sse4aOperands.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "Sse4aExecutionHarness.hpp"

#if defined(__x86_64__)

namespace {

using namespace Sse4aHarness;

struct Case {
    bool Insertq;
    std::uint8_t Destination;
    std::uint8_t Source;  // INSERTQ only; EXTRQ operates on Destination alone
    std::uint8_t Length;  // 1..64
    std::uint8_t Index;
};

// Builds `66 [REX] 0F 78 /0 ib ib` (EXTRQ xmm, imm8, imm8) or
// `F2 [REX] 0F 78 /r ib ib` (INSERTQ xmm1, xmm2, imm8, imm8). Length 64 is
// encoded as 0, as the hardware defines it.
Bytes Encode(const Case& c) {
    Bytes bytes{static_cast<std::uint8_t>(c.Insertq ? 0xF2 : 0x66)};
    const int reg = c.Insertq ? c.Destination : 0;
    const int rm = c.Insertq ? c.Source : c.Destination;
    const auto rex = static_cast<std::uint8_t>(0x40 | ((reg & 8) ? 4 : 0) | ((rm & 8) ? 1 : 0));
    if (rex != 0x40)
        bytes.push_back(rex);
    bytes.insert(bytes.end(), {0x0F, 0x78, static_cast<std::uint8_t>(0xC0 | ((reg & 7) << 3) | (rm & 7))});
    bytes.push_back(static_cast<std::uint8_t>(c.Length == 64 ? 0 : c.Length));
    bytes.push_back(c.Index);
    return bytes;
}

// Architectural result of the low quadword of the destination.
std::uint64_t Model(const Case& c, const Harness& in) {
    const auto mask = c.Length == 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << c.Length) - 1);
    const auto destination = in.Xmm[c.Destination][0];
    if (!c.Insertq)
        return (destination >> c.Index) & mask;
    return (destination & ~(mask << c.Index)) | ((in.Xmm[c.Source][0] & mask) << c.Index);
}

std::string Describe(const Case& c) {
    return std::string(c.Insertq ? "insertq xmm" : "extrq xmm") + std::to_string(c.Destination) +
           (c.Insertq ? ", xmm" + std::to_string(c.Source) : "") + ", " + std::to_string(c.Length) + ", " + std::to_string(c.Index);
}

// The code to run for a case: the matcher's in-place replacement followed by a
// return branch placeholder, or the out-of-line stub body.
Bytes LoweredMiddle(const Case& c, bool* inPlace) {
    const auto site = Encode(c);
    const auto matcher = Codegen::MakeAmd64OnlyInstructionMatcher();
    const auto match = matcher->Match(site.data(), site.size());
    EXPECT_TRUE(match.has_value()) << Describe(c);
    if (!match.has_value())
        return {};
    Codegen::LoweredBody body;
    if (match->Lowering == Codegen::Amd64OnlyLowering::InPlace) {
        EXPECT_EQ(match->ReplacementBytes.size(), site.size()) << "in-place lowering changed the length: " << Describe(c);
        body.Bytes = match->ReplacementBytes;
        body.ReturnBranchOffset = body.Bytes.size();
        body.Bytes.insert(body.Bytes.end(), {0xE9, 0, 0, 0, 0});
        *inPlace = true;
    } else {
        EXPECT_EQ(match->Lowering, Codegen::Amd64OnlyLowering::Trampoline) << Describe(c);
        body.Bytes = match->StubBody;
        body.ReturnBranchOffset = match->ReturnBranchOffset;
        *inPlace = false;
    }
    return StubMiddle(body);
}

Harness RandomInput(std::mt19937_64& random) {
    Harness in{};
    for (auto& lane : in.Xmm)
        for (auto& q : lane)
            q = random();
    return in;
}

// Checks everything except the destination's low quadword value.
void ExpectPreserved(const Harness& in, const Harness& out, const Case& c) {
    for (int n = 0; n < 16; ++n) {
        if (n == c.Destination)
            continue;  // the destination's upper quadword is architecturally undefined
        EXPECT_EQ(out.Xmm[n][0], in.Xmm[n][0]) << "xmm" << n << " low clobbered: " << Describe(c);
        EXPECT_EQ(out.Xmm[n][1], in.Xmm[n][1]) << "xmm" << n << " high clobbered: " << Describe(c);
    }
    EXPECT_EQ(out.Flags & kFlagMask, kFlagsIn & kFlagMask) << "flags changed: " << Describe(c);
    for (int k = 1; k <= 16; ++k)
        ASSERT_EQ(out.RedZone[k - 1], RedZonePattern(k)) << "red zone slot " << k << " overwritten: " << Describe(c);
}

// Visits every defined (length, index) with the register choices that matter:
// identical operands, distinct low and distinct REX-encoded registers.
template<typename Fn>
void ForEachCase(Fn&& fn) {
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 7> pairs = {{{3, 3}, {1, 0}, {9, 4}, {3, 4}, {15, 15}, {0, 15}, {8, 8}}};
    for (unsigned length = 1; length <= 64; ++length) {
        for (unsigned index = 0; index + length <= 64; ++index) {
            for (const auto [dst, src] : pairs)
                fn(Case{true, dst, src, static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(index)});
            for (const std::uint8_t dst : {0, 3, 9, 15})
                fn(Case{false, dst, dst, static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(index)});
        }
    }
}

// Invariant: for every defined field, the lowered EXTRQ/INSERTQ (immediate
// form), whether emitted in place or as a stub, leaves the architectural result
// in the destination's low quadword and changes nothing else observable.
// Failure mode: an in-place rewrite that is too short for a field it claims to
// handle, a wrong PSHUFB mask, a wrong blend mask, or a clobbered scratch
// register in the generic path.
TEST(Sse4aImmediateForm, LoweringMatchesArchitecturalModelForEveryField) {
    std::mt19937_64 random(0x5EED);
    std::size_t inPlaceCount = 0;
    std::size_t stubCount = 0;
    ForEachCase([&](const Case& c) {
        bool inPlace = false;
        const auto middle = LoweredMiddle(c, &inPlace);
        (inPlace ? inPlaceCount : stubCount) += 1;
        const auto in = RandomInput(random);
        const auto out = RunThunk(middle, in);
        ASSERT_EQ(out.Xmm[c.Destination][0], Model(c, in)) << Describe(c) << (inPlace ? " [in place]" : " [stub]");
        ExpectPreserved(in, out, c);
    });
    // Both shapes must actually have been exercised, or the test proves nothing
    // about one of them.
    EXPECT_GT(inPlaceCount, 0u);
    EXPECT_GT(stubCount, 0u);
}

// Invariant (hardware oracle): on an SSE4a CPU the real instruction and the
// lowering agree with each other and with the model for every defined field.
// Skipped on hosts without SSE4a (Intel).
TEST(Sse4aImmediateForm, NativeInstructionAgreesWithModelAndLowering) {
    if (!HostHasSse4a())
        GTEST_SKIP() << "host CPU does not implement SSE4a";
    std::mt19937_64 random(0xC0FFEE);
    ForEachCase([&](const Case& c) {
        bool inPlace = false;
        const auto middle = LoweredMiddle(c, &inPlace);
        const auto in = RandomInput(random);
        const auto native = RunThunk(Encode(c), in);
        const auto lowered = RunThunk(middle, in);
        ASSERT_EQ(native.Xmm[c.Destination][0], Model(c, in)) << "model disagrees with hardware: " << Describe(c);
        ASSERT_EQ(lowered.Xmm[c.Destination][0], native.Xmm[c.Destination][0]) << "lowering disagrees with hardware: " << Describe(c);
    });
}

}  // namespace

#endif  // __x86_64__
