// GoogleTest suite for how Amd64OnlyConverter handles EXTRQ/INSERTQ sites that
// are shorter than the 5-byte `jmp rel32` that replaces them (the 4-byte
// register forms), and for the Unproven-bytes policy.
//
// Subsystem: relinker codegen, `--to-intel`. Hosted-CI safe: every segment is a
// synthetic hand-assembled byte string with an explicit CodeMap (proven starts
// and branch targets are spelled out per test), so each test states exactly
// which facts the converter may rely on. No game data is involved.
//
// Policy under test (docs/spec/relinker.md): a short site is extended over the
// following straight-line, position-independent instructions that are proven
// code and not branch targets. If it cannot be extended it stays a logged,
// reported Residual site (never silently skipped, never mis-moved).
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/x86/Sse4aLowering.hpp>
#include <domain/CodeMap.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint64_t kCodeFileOffset = 0x200;
constexpr std::uint64_t kCodeAddress = 0x1000;

const Bytes kExtrqReg = {0x66, 0x0F, 0x79, 0xCA};          // EXTRQ xmm1, xmm2 (4 bytes)
const Bytes kExtrqRegRex = {0x66, 0x41, 0x0F, 0x79, 0xCA};  // EXTRQ xmm1, xmm10 (5 bytes)
const Bytes kInsertqReg = {0xF2, 0x0F, 0x79, 0xCA};         // INSERTQ xmm1, xmm2 (4 bytes)

struct Fixture {
    Bytes File;                 // whole "ELF" file: zero header area, code at kCodeFileOffset
    Domain::ProgramHeader Header;
    Domain::CodeMap Map;
};

// Lays `code` at kCodeFileOffset and builds the CodeMap from explicit proven
// instruction start offsets (relative to the code) and branch-target offsets.
// `segmentSize` lets a test make the segment shorter than the bytes laid out.
Fixture Make(const Bytes& code, const std::set<std::size_t>& starts, const std::set<std::size_t>& targets = {}, std::size_t segmentSize = 0) {
    Fixture f;
    f.File.assign(kCodeFileOffset + code.size() + 0x40, 0xCC);
    std::copy(code.begin(), code.end(), f.File.begin() + kCodeFileOffset);
    if (segmentSize == 0)
        segmentSize = code.size();
    f.Header = {1, 5, kCodeFileOffset, kCodeAddress, 0, segmentSize, segmentSize, 16};
    for (const auto s : starts)
        f.Map.Starts.insert(kCodeAddress + s);
    for (const auto t : targets)
        f.Map.BranchTargets.insert(kCodeAddress + t);
    return f;
}

Bytes Concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

Codegen::ConvertResult Convert(const Fixture& f, std::string* log = nullptr) {
    const auto converter = Codegen::MakeAmd64OnlyConverter();
    testing::internal::CaptureStdout();
    Codegen::ConvertResult result;
    try {
        result = converter->Convert(f.File, {f.Header}, f.Map);
    } catch (...) {
        (void)testing::internal::GetCapturedStdout();
        throw;
    }
    const auto out = testing::internal::GetCapturedStdout();
    if (log != nullptr)
        *log = out;
    return result;
}

// Invariant: a 4-byte register form followed by one ordinary 1-byte instruction
// is extended to the 5 bytes a `jmp rel32` needs. The moved instruction is
// re-executed by the stub right before the return branch, the recorded site
// covers both instructions, and no Residual remains.
// Failure mode: the 4-byte site is left Residual (AMD-only instruction stays in
// the image) or the moved instruction is lost.
TEST(Amd64OnlyShortSite, ExtendsOverFollowingNop) {
    const auto f = Make(Concat({kExtrqReg, {0x90}, {0xC3}}), {0, 4, 5});
    const auto result = Convert(f);
    ASSERT_EQ(result.Trampolines.size(), 1u);
    EXPECT_TRUE(result.Residuals.empty());
    const auto& site = result.Trampolines[0];
    EXPECT_EQ(site.Address, kCodeAddress);
    EXPECT_EQ(site.Length, 5u);
    EXPECT_EQ(site.OriginalBytes, Concat({kExtrqReg, {0x90}}));
    ASSERT_GT(site.ReturnBranchOffset, 0u);
    EXPECT_EQ(site.Body[site.ReturnBranchOffset - 1], 0x90) << "moved NOP must run just before the return branch";
    ASSERT_EQ(result.Reports.size(), 1u);
    EXPECT_EQ(result.Reports[0].InstructionName, "EXTRQ register form");
    EXPECT_EQ(result.Reports[0].OriginalLength, 5u);
    EXPECT_EQ(result.Bytes, f.File) << "the converter reports the site; the writer patches the bytes";
}

// Invariant: a longer successor is moved whole, in order, after the lowering.
// Failure mode: only part of the instruction moved (the return branch would
// land in its middle).
TEST(Amd64OnlyShortSite, MovesAWholeFiveByteSuccessor) {
    const Bytes movEax1 = {0xB8, 0x01, 0x00, 0x00, 0x00};
    const auto f = Make(Concat({kExtrqReg, movEax1, {0xC3}}), {0, 4, 9});
    const auto result = Convert(f);
    ASSERT_EQ(result.Trampolines.size(), 1u);
    const auto& site = result.Trampolines[0];
    EXPECT_EQ(site.Length, 9u);
    const Bytes tail(site.Body.begin() + site.ReturnBranchOffset - movEax1.size(), site.Body.begin() + site.ReturnBranchOffset);
    EXPECT_EQ(tail, movEax1);
}

// Invariant: the REX encoding is already 5 bytes, so it is a plain trampoline
// site and consumes nothing that follows.
// Failure mode: needless extension would move (and risk) the following code.
TEST(Amd64OnlyShortSite, FiveByteRegisterFormIsNotExtended) {
    const auto f = Make(Concat({kExtrqRegRex, {0xC3}}), {0, 5});
    const auto result = Convert(f);
    ASSERT_EQ(result.Trampolines.size(), 1u);
    EXPECT_EQ(result.Trampolines[0].Length, 5u);
    EXPECT_TRUE(result.Residuals.empty());
}

// Invariant: adjacent register forms share one stub; the second instruction is
// consumed and must not be patched or reported a second time.
// Failure mode: two overlapping trampoline sites (the writer would then reject
// the "bytes changed before patching" second site) or a duplicate report.
TEST(Amd64OnlyShortSite, AdjacentRegisterFormsShareOneStub) {
    const auto f = Make(Concat({kExtrqReg, kInsertqReg, {0xC3}}), {0, 4, 8});
    const auto result = Convert(f);
    ASSERT_EQ(result.Trampolines.size(), 1u);
    EXPECT_EQ(result.Trampolines[0].Length, 8u);
    EXPECT_EQ(result.Reports.size(), 1u);
    EXPECT_TRUE(result.Residuals.empty());
    const Codegen::Sse4aLowering lowering;
    const std::array<Codegen::Sse4aOperands, 2> seq = {
        Codegen::Sse4aOperands{false, true, 1, 2, 0, 0},
        Codegen::Sse4aOperands{true, true, 1, 2, 0, 0},
    };
    EXPECT_EQ(result.Trampolines[0].Body, lowering.LowerOutOfLine(seq, {}).Bytes);
}

struct RefusalCase {
    const char* Name;
    Bytes Successor;       // instruction after the 4-byte EXTRQ
    bool SuccessorIsStart; // whether the CodeMap proves the successor
    const char* ReasonFragment;
};

// Prints the case by name in gtest failure output (the default dumps raw bytes).
void PrintTo(const RefusalCase& c, std::ostream* os) {
    *os << c.Name;
}

// Invariant: when the bytes after a short site cannot safely move, the site is
// NOT extended: it stays a Residual (reported, logged with the reason), the
// image bytes are untouched and nothing is silently dropped. Each case is a
// distinct hazard of moving code into a stub at another address:
//  - control transfers (the stub would need re-targeting),
//  - RIP-relative operands (displacement is wrong at the stub address),
//  - FS/GS overrides (WindowsTlsBuilder patches those in place and cannot see
//    a copy inside the stub; a raw fs: access would fault under Windows),
//  - successors that are not proven code.
// Failure mode: the successor is moved anyway and the converted code corrupts
// memory or jumps to the wrong place.
class ShortSiteRefusal : public testing::TestWithParam<RefusalCase> {};

TEST_P(ShortSiteRefusal, StaysResidualAndExplainsWhy) {
    const auto& c = GetParam();
    const auto code = Concat({kExtrqReg, c.Successor});
    std::set<std::size_t> starts = {0};
    if (c.SuccessorIsStart)
        starts.insert(4);
    const auto f = Make(code, starts);
    std::string log;
    const auto result = Convert(f, &log);
    EXPECT_TRUE(result.Trampolines.empty()) << "must not move: " << c.Name;
    ASSERT_EQ(result.Residuals.size(), 1u) << c.Name;
    EXPECT_EQ(result.Residuals[0].Mnemonic, "EXTRQ register form");
    EXPECT_EQ(result.Bytes, f.File) << "residual bytes must be left untouched";
    EXPECT_NE(log.find(c.ReasonFragment), std::string::npos) << "log must say why: " << log;
    EXPECT_NE(log.find("runtime trap"), std::string::npos) << log;
}

INSTANTIATE_TEST_SUITE_P(Hazards, ShortSiteRefusal, testing::Values(
    RefusalCase{"ret", {0xC3}, true, "control transfer"},
    RefusalCase{"jmp rel32", {0xE9, 0x10, 0x00, 0x00, 0x00}, true, "control transfer"},
    RefusalCase{"call rel32", {0xE8, 0x10, 0x00, 0x00, 0x00}, true, "control transfer"},
    RefusalCase{"jne rel8", {0x75, 0x02}, true, "control transfer"},
    RefusalCase{"mov rax,[rip+disp]", {0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00}, true, "RIP-relative"},
    RefusalCase{"mov rax,fs:[0]", {0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00}, true, "FS/GS"},
    RefusalCase{"mov rax,gs:[0]", {0x65, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00}, true, "FS/GS"},
    RefusalCase{"unproven successor", {0x90, 0xC3}, false, "not a proven instruction start"}
));

// Invariant: a segment that ends right after the short site (no successor at
// all) is a Residual, not an out-of-bounds read.
// Failure mode: reading past the segment end while decoding the successor.
TEST(Amd64OnlyShortSite, SiteAtSegmentEndStaysResidual) {
    // The file has more 0x90 bytes after the code, but the segment stops at the site.
    auto f = Make(Concat({kExtrqReg, {0x90}}), {0, 4}, {}, kExtrqReg.size());
    std::string log;
    const auto result = Convert(f, &log);
    EXPECT_TRUE(result.Trampolines.empty());
    EXPECT_EQ(result.Residuals.size(), 1u);
    EXPECT_NE(log.find("not a proven instruction start"), std::string::npos) << log;
}

// Invariant: a branch landing inside the bytes that would move blocks the
// extension (the jump would enter the middle of the replaced range), while a
// branch to the site start or to the first byte AFTER the moved range is fine.
// Failure mode: a branch into the NOP now executes the tail of a 5-byte `jmp`
// displacement.
TEST(Amd64OnlyShortSite, BranchIntoMovedBytesBlocksExtension) {
    const auto code = Concat({kExtrqReg, {0x90}, {0x90}, {0xC3}});
    {
        const auto f = Make(code, {0, 4, 5, 6}, {4});  // target = the moved NOP
        const auto result = Convert(f);
        EXPECT_TRUE(result.Trampolines.empty());
        EXPECT_EQ(result.Residuals.size(), 1u);
    }
    {
        const auto f = Make(code, {0, 4, 5, 6}, {0, 5});  // site start and first byte after: both allowed
        const auto result = Convert(f);
        EXPECT_EQ(result.Trampolines.size(), 1u);
        EXPECT_TRUE(result.Residuals.empty());
    }
}

// Invariant: an AMD-only successor that this lowering cannot take into the same
// stub (here MOVNTSS, an in-place instruction) blocks the extension, and that
// successor is still converted on its own.
// Failure mode: the successor is swallowed into the stub unlowered (it would
// #UD on Intel) or converted twice.
TEST(Amd64OnlyShortSite, UnmovableAmdOnlySuccessorBlocksExtension) {
    const Bytes movntss = {0xF3, 0x0F, 0x2B, 0x07};  // MOVNTSS [rdi], xmm0
    const auto f = Make(Concat({kExtrqReg, movntss, {0xC3}}), {0, 4, 8});
    std::string log;
    const auto result = Convert(f, &log);
    EXPECT_TRUE(result.Trampolines.empty());
    EXPECT_EQ(result.Residuals.size(), 1u);
    EXPECT_EQ(result.ReplacedCount, 1u) << "MOVNTSS must still be rewritten in place";
    EXPECT_EQ(result.Bytes[kCodeFileOffset + 4 + 2], 0x11) << "MOVNTSS opcode byte not rewritten to MOVSS";
    EXPECT_NE(log.find("no out-of-line lowering"), std::string::npos) << log;
}

// Invariant (portps5-55): executable bytes that the code prover never reached
// are counted and logged but NEVER patched, even when they contain a valid
// SSE4a encoding. A converted image therefore still contains AMD-only bytes
// there; whether they execute is a runtime question (see relinker.md Q3).
// Failure mode: the converter patching bytes it cannot prove are instructions
// (corrupting literal pools / jump tables) or silently hiding the count.
TEST(Amd64OnlyShortSite, UnprovenBytesAreCountedNeverPatched) {
    // Proven: ret at 0. Unproven (no start): a register-form EXTRQ and a full
    // immediate-form INSERTQ that would otherwise be lowered.
    const Bytes unprovenInsertq = {0xF2, 0x0F, 0x78, 0xC8, 0x08, 0x00};
    const auto code = Concat({{0xC3}, kExtrqReg, unprovenInsertq});
    auto f = Make(code, {0});
    f.Map.Unproven.push_back({kCodeAddress + 1, kCodeAddress + code.size()});
    std::string log;
    const auto result = Convert(f, &log);
    EXPECT_EQ(result.Bytes, f.File);
    EXPECT_TRUE(result.Trampolines.empty());
    EXPECT_TRUE(result.Residuals.empty()) << "unproven sites are not Residual sites";
    EXPECT_EQ(result.ReplacedCount, 0u);
    EXPECT_EQ(result.UnprovenBytes, code.size() - 1);
    EXPECT_EQ(result.UnprovenRanges, 1u);
    EXPECT_NE(log.find("unproven bytes"), std::string::npos) << log;
}

}  // namespace
