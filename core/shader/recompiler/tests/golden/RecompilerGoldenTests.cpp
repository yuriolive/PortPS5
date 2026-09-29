// core/shader/recompiler/tests/golden/RecompilerGoldenTests.cpp
// Hosted `recompiler-golden` gate (M1 exit): every synthetic corpus case must decode to
// its expected instruction class and must recompile to validated SPIR-V on wave32 and
// wave64 compute. In the `ci` preset ANYPS5_ENABLE_SPIRV_TOOLS is ON, so Recompile runs
// spirv-val before and after the optimizer and throws on any validation failure; the
// replay assertions below therefore prove the "0 validation failures" exit criterion.
// Preconditions: corpus dwords are project-written (see SyntheticCorpus.hpp); no guest
// memory outside the case snapshot is read, so results are deterministic across runs.
// Expected failure mode: Recompile throws std::runtime_error naming the failing stage.

#include "SyntheticCorpus.hpp"
#include "RdnaDecoder/include/RdnaDecoder/RdnaInstructionDecoder.hpp"

#include <gtest/gtest.h>
#include <string>

namespace ShaderRecompiler::Golden {
namespace {

// Maps a synthetic family to the decoder's family enum for the coverage gate.
[[nodiscard]] RdnaInstructionFamily ExpectedDecoderFamily(SyntheticFamily family) {
    switch (family) {
    case SyntheticFamily::Sopp: return RdnaInstructionFamily::SOPP;
    case SyntheticFamily::Sop1: return RdnaInstructionFamily::SOP1;
    case SyntheticFamily::Sop2: return RdnaInstructionFamily::SOP2;
    case SyntheticFamily::SopK: return RdnaInstructionFamily::SOPK;
    case SyntheticFamily::SopC: return RdnaInstructionFamily::SOPC;
    case SyntheticFamily::Smem: return RdnaInstructionFamily::SMEM;
    case SyntheticFamily::Vop1: return RdnaInstructionFamily::VOP1;
    case SyntheticFamily::Vop2: return RdnaInstructionFamily::VOP2;
    case SyntheticFamily::Vop3: return RdnaInstructionFamily::VOP3;
    case SyntheticFamily::VopC: return RdnaInstructionFamily::VOPC;
    case SyntheticFamily::Vop3P: return RdnaInstructionFamily::VOP3P;
    case SyntheticFamily::Ds: return RdnaInstructionFamily::DS;
    case SyntheticFamily::Mubuf: return RdnaInstructionFamily::MUBUF;
    case SyntheticFamily::Mtbuf: return RdnaInstructionFamily::MTBUF;
    case SyntheticFamily::Mimg: return RdnaInstructionFamily::MIMG;
    case SyntheticFamily::Flat: return RdnaInstructionFamily::FLAT;
    case SyntheticFamily::Exp: return RdnaInstructionFamily::EXP;
    case SyntheticFamily::Count: break;
    }
    return RdnaInstructionFamily::Unknown;
}

// Verifies the corpus covers every decoded class the M1 exit requires: each case's first
// word must decode to its declared family, and the union must span all 17 families.
// Catches silent family drift (for example a wrong base word turning SOPK into SOP2).
TEST(RecompilerGoldenTests, CorpusCoversEveryDecodedClass) {
    const auto cases = AllSyntheticCases();
    ASSERT_EQ(cases.size(), static_cast<std::size_t>(SyntheticFamily::Count));
    RdnaInstructionDecoder decoder;
    std::vector<bool> covered(static_cast<std::size_t>(SyntheticFamily::Count), false);
    for (const auto& testCase : cases) {
        SCOPED_TRACE(std::string("case ") + testCase.name);
        const RdnaProgram program = decoder.Decode(testCase.code);
        ASSERT_FALSE(program.instructions.empty());
        EXPECT_EQ(program.instructions.front().family, ExpectedDecoderFamily(testCase.family));
        covered[static_cast<std::size_t>(testCase.family)] = true;
    }
    for (std::size_t i = 0; i < covered.size(); ++i) {
        EXPECT_TRUE(covered[i]) << "decoded class missing from corpus: " << SyntheticFamilyName(static_cast<SyntheticFamily>(i));
    }
}

// Replays one case through the full Recompile pipeline for a wave size and proves the
// module is non-empty. SPIR-V validity is enforced inside Recompile by spirv-val when
// SPIRV-Tools are enabled (ci/dev presets); without them the driver capability check is
// the last guard (docs/spec/shader-recompiler.md failure modes).
void ReplayCase(const SyntheticCase& testCase, std::uint32_t waveSize) {
    SCOPED_TRACE(std::string("case ") + testCase.name + " wave" + std::to_string(waveSize));
    auto owned = MakeRequest(testCase, waveSize, waveSize);
    RecompileResult result{};
    EXPECT_NO_THROW(result = Recompile(owned.request));
    EXPECT_FALSE(result.spirv.empty());
    EXPECT_EQ(result.spirv[0], 0x07230203u);
}

// Full-corpus replay on wave32 and wave64, including the vertex-stage EXP export path.
TEST(RecompilerGoldenTests, CorpusRecompilesOnWave32AndWave64) {
    for (const auto& testCase : AllSyntheticCases()) {
        ReplayCase(testCase, 32u);
        ReplayCase(testCase, 64u);
    }
}

} // namespace
} // namespace ShaderRecompiler::Golden
