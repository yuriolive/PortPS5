// core/shader/recompiler/tests/golden/SyntheticCorpus.hpp
// Hand-assembled synthetic RDNA2 corpus for the M1 hosted `recompiler-golden` gate.
// Each case covers one decoded instruction class (see GetRdnaInstructionFamily) with
// project-written dwords only: no game bytecode may ever enter this directory
// (legal boundary, docs/spec/verification.md). The dwords below were derived from the
// decoder sources (RdnaScalarOpDecoder, RdnaVectorOpDecoder, RdnaMemoryOpDecoder,
// RdnaImageOpDecoder, RdnaExportOpDecoder); the comments cite the field layout so a
// reviewer can re-derive every word without hardware docs.
// Subsystem owner: shader recompiler. Header-only, no state; safe on any thread.
// Guest-memory model: descriptor dwords live in userData SGPRs and every guest address
// they reference is backed by an explicit snapshot region, so replay is deterministic
// and needs no live host pointers (unlike AgcDriver::ShaderMemory::Capture).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "Recompiler.hpp"

namespace ShaderRecompiler::Golden {

// One decoded instruction class per docs/spec/shader-recompiler.md coverage gate.
// VINTRP is intentionally absent: pixel-only interpolation has no compute encoding and
// the gate lists the 17 classes below.
enum class SyntheticFamily : std::uint32_t {
    Sopp = 0,
    Sop1,
    Sop2,
    SopK,
    SopC,
    Smem,
    Vop1,
    Vop2,
    Vop3,
    VopC,
    Vop3P,
    Ds,
    Mubuf,
    Mtbuf,
    Mimg,
    Flat,
    Exp,
    Count
};

// Human-readable family name for test and failure messages.
[[nodiscard]] inline const char* SyntheticFamilyName(SyntheticFamily family) {
    switch (family) {
    case SyntheticFamily::Sopp: return "SOPP";
    case SyntheticFamily::Sop1: return "SOP1";
    case SyntheticFamily::Sop2: return "SOP2";
    case SyntheticFamily::SopK: return "SOPK";
    case SyntheticFamily::SopC: return "SOPC";
    case SyntheticFamily::Smem: return "SMEM";
    case SyntheticFamily::Vop1: return "VOP1";
    case SyntheticFamily::Vop2: return "VOP2";
    case SyntheticFamily::Vop3: return "VOP3";
    case SyntheticFamily::VopC: return "VOPC";
    case SyntheticFamily::Vop3P: return "VOP3P";
    case SyntheticFamily::Ds: return "DS";
    case SyntheticFamily::Mubuf: return "MUBUF";
    case SyntheticFamily::Mtbuf: return "MTBUF";
    case SyntheticFamily::Mimg: return "MIMG";
    case SyntheticFamily::Flat: return "FLAT";
    case SyntheticFamily::Exp: return "EXP";
    case SyntheticFamily::Count: break;
    }
    return "unknown";
}

// One synthetic shader: hand-assembled code plus the SGPR/memory snapshot it needs.
// userData[i] is visible to the shader as s[userDataBase + i]. Every guest address
// embedded in userData must be backed by exactly one memory region.
struct SyntheticCase {
    SyntheticFamily family;
    const char* name;
    std::vector<std::uint32_t> code;
    std::vector<std::uint32_t> userData;
    std::uint32_t userDataBase = 8u;
    struct Region {
        std::uint64_t guestAddress;
        std::vector<std::byte> bytes;
    };
    std::vector<Region> memory;
    ShaderStage stage = ShaderStage::Compute;
};

// Guest snapshot bases. Arbitrary 1 MiB-apart addresses; they never touch host memory.
inline constexpr std::uint64_t kSmemBase = 0x100000u;
inline constexpr std::uint64_t kMubufBase = 0x110000u;
inline constexpr std::uint64_t kMtbufBase = 0x120000u;
inline constexpr std::uint64_t kImageBase = 0x200000u;

// Zero-filled snapshot region of the given size.
[[nodiscard]] inline SyntheticCase::Region ZeroRegion(std::uint64_t guestAddress, std::size_t bytes) {
    return SyntheticCase::Region{guestAddress, std::vector<std::byte>(bytes, std::byte{0})};
}

// Builds every synthetic case. All shaders end with s_endpgm (0xBF810000), which the
// program decoder requires as terminator (DecodeRdnaProgram).
[[nodiscard]] inline std::vector<SyntheticCase> AllSyntheticCases() {
    std::vector<SyntheticCase> cases;
    // SOPP: s_nop. Word = 0x80000000 | (0x7F << 23) | (op << 16) | simm.
    cases.push_back({SyntheticFamily::Sopp, "sopp_nop", {0xBF800000u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // SOP1: s_mov_b32 s2, s3. Word = base | (dst << 16) | (op << 8) | src0, op 0x03.
    cases.push_back({SyntheticFamily::Sop1, "sop1_mov", {0xBE820303u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // SOP2: s_add_u32 s2, s3, s4. Word = 0x80000000 | (op << 23) | (dst << 16) | (src1 << 8) | src0, op 0x00.
    cases.push_back({SyntheticFamily::Sop2, "sop2_add", {0x80020403u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // SOPK: s_movk_i32 s2, 0x1234. Word = 0xB0000000 | (simm & 0xFFFF); sub-opcode (bits 27-23) is 0x00.
    cases.push_back({SyntheticFamily::SopK, "sopk_movk", {0xB0021234u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // SOPC: s_cmp_eq_u32 s3, s4. Word = 0x80000000 | (0x7E << 23) | (op << 16) | (src1 << 8) | src0, op 0x09.
    cases.push_back({SyntheticFamily::SopC, "sopc_cmp", {0xBF090403u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // SMEM: s_load_dword s4, s[8:9], null+0. sbase field 4 selects s8 (decoder doubles it);
    // soffset code 125 is Null, so no SGPR offset pollutes the snapshot.
    cases.push_back({SyntheticFamily::Smem,
                     "smem_load",
                     {0xF4000104u, 0xFA000000u, 0xBF810000u},
                     {static_cast<std::uint32_t>(kSmemBase), 0u},
                     8u,
                     {ZeroRegion(kSmemBase, 64u)},
                     ShaderStage::Compute});
    // VOP1: v_mov_b32 v0, v1. VGPR1 as a scalar-source code is 256 + 1 = 257 (0x101).
    cases.push_back({SyntheticFamily::Vop1, "vop1_mov", {0x7E000301u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // VOP2: v_add_f32 v0, s3, v1. Opcode (bits 30-25) 0x03, vsrc1 field holds the VGPR index.
    cases.push_back({SyntheticFamily::Vop2, "vop2_add", {0x06000203u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // VOP3: v_fma_f32 v0, v1, v2, v3 with all-zero modifiers. Opcode 0x14B spans bits 16-25.
    cases.push_back({SyntheticFamily::Vop3, "vop3_fma", {0xD54B0000u, 0x040E0501u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // VOPC: v_cmp_eq_f32 vcc, v1, v2. Opcode (bits 24-17) 0x02 writes VccLo.
    cases.push_back({SyntheticFamily::VopC, "vopc_cmp", {0x7C040501u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // VOP3P: v_pk_add_i16 v0, v1, v2. Two-source packed op; src2 field reads s0 but is unused.
    cases.push_back({SyntheticFamily::Vop3P, "vop3p_add", {0xCC020000u, 0x00020501u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // DS: ds_write_b32 v0, v1 with zero offsets. LDS-backed; compute info sizes LDS below.
    cases.push_back({SyntheticFamily::Ds, "ds_write", {0xD8340000u, 0x00000100u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // MUBUF: buffer_load_dword v0, v1, s[8:11], null. srsrc field 2 selects s8 (decoder quadruples it).
    cases.push_back({SyntheticFamily::Mubuf,
                     "mubuf_load",
                     {0xE0300000u, 0x7D020001u, 0xBF810000u},
                     {static_cast<std::uint32_t>(kMubufBase), 0u, 0xFFFFFFFFu, 0u},
                     8u,
                     {ZeroRegion(kMubufBase, 256u)},
                     ShaderStage::Compute});
    // MTBUF: tbuffer_load_format_x v0, v1, s[8:11], null, DFMT 32-bit (4), NFMT float (2).
    cases.push_back({SyntheticFamily::Mtbuf,
                     "mtbuf_load",
                     {0xE9200000u, 0x7D020001u, 0xBF810000u},
                     {static_cast<std::uint32_t>(kMtbufBase), 0u, 0xFFFFFFFFu, 0u},
                     8u,
                     {ZeroRegion(kMtbufBase, 256u)},
                     ShaderStage::Compute});
    // MIMG: image_load v0, v[2:3], s[8:15], 2D, dmask x, sunk into ds_write_b32 v4, v0.
    // The LDS sink is load-bearing, not incidental: an unused ImageRead is pure, so the
    // dead-code eliminator removes it before resource tracking, leaving an unpatched
    // memory reference that the materializer cannot remap. The side-effecting DS write
    // keeps the read (and its descriptor evaluation) alive. Resource field 2 selects s8
    // for the 8-dword T# (r128 clear); sampler field 0 is unused by non-sampling ops.
    // The 2D dimension needs a two-component address (v2, v3): the SPIR-V emitter
    // rejects fewer coordinate components. The T# is a valid 2D R32F descriptor in this
    // tree's layout (type in dwords[3] bits 28-31, format in dwords[1] bits 20-28).
    cases.push_back({SyntheticFamily::Mimg,
                     "mimg_load",
                     {0xF0000108u, 0x00020002u, 0xD8340000u, 0x00000004u, 0xBF810000u},
                     {static_cast<std::uint32_t>(kImageBase),
                      22u << 20u,
                      63u | (63u << 14u),
                      0x00000688u | (9u << 28u),
                      0u,
                      0u,
                      0u,
                      0u},
                     8u,
                     {ZeroRegion(kImageBase, 256u)},
                     ShaderStage::Compute});
    // FLAT: flat_load_dword v0, v[1:2], flat segment (seg 0 pairs addr with addr + 1).
    cases.push_back({SyntheticFamily::Flat, "flat_load", {0xDC300000u, 0x00000001u, 0xBF810000u}, {}, 8u, {}, ShaderStage::Compute});
    // EXP: exp pos0, v0..v3, done. Position export is the vertex-stage export path, matching
    // the proven vertex pattern (agc_shader_memory_tests replays EXP through Recompile).
    cases.push_back({SyntheticFamily::Exp,
                     "exp_pos",
                     {0xF80008CFu, 0x03020100u, 0xBF810000u},
                     {},
                     8u,
                     {},
                     ShaderStage::Vertex});
    return cases;
}

// Owned RecompileRequest: keeps every span-referenced vector alive next to the request.
struct SyntheticRequest {
    std::vector<std::uint32_t> code;
    std::vector<std::uint32_t> userData;
    ShaderComputeStageInfo compute{};
    ShaderVertexStageInfo vertex{};
    std::vector<std::vector<std::byte>> memoryBytes;
    std::vector<MemoryRegion> memory;
    RecompileRequest request{};
};

// Builds a deterministic request for a case: cache disabled so every replay exercises the
// full pipeline, Vulkan 1.1 / SPIR-V 1.3 target matching the driver's target.
[[nodiscard]] inline SyntheticRequest MakeRequest(const SyntheticCase& testCase, std::uint32_t waveSize, std::uint32_t subgroupSize) {
    SyntheticRequest owned;
    owned.code = testCase.code;
    owned.userData = testCase.userData;
    owned.memoryBytes.reserve(testCase.memory.size());
    owned.memory.reserve(testCase.memory.size());
    for (const auto& region : testCase.memory) {
        owned.memoryBytes.push_back(region.bytes);
    }
    for (std::size_t i = 0; i < testCase.memory.size(); ++i) {
        owned.memory.push_back(MemoryRegion{testCase.memory[i].guestAddress, owned.memoryBytes[i]});
    }
    owned.request.useCache = false;
    owned.request.shader = ShaderBinary{testCase.stage, 0x10000u, owned.code, 0u, {}};
    owned.request.context.waveSize = waveSize;
    owned.request.context.userDataBaseRegister = testCase.userDataBase;
    owned.request.context.userData = owned.userData;
    owned.request.context.memory = owned.memory;
    if (testCase.stage == ShaderStage::Compute) {
        owned.compute.numThreads = {64u, 1u, 1u};
        owned.compute.ldsSizeDwords = 1024u;
        owned.compute.groupIdEnable = {false, false, false};
        owned.compute.tgSizeEnable = false;
        owned.compute.threadIdComponentCount = 0u;
        owned.request.context.compute = owned.compute;
    } else {
        owned.request.context.vertex = owned.vertex;
    }
    owned.request.target.vulkanVersion = 0x00401000u;
    owned.request.target.spirvVersion = 0x00010300u;
    owned.request.target.subgroupSize = subgroupSize;
    owned.request.target.bdaAbiVersion = 0u;
    owned.request.target.fragmentShaderBarycentricEnabled = false;
    owned.request.target.maxWorkgroupSize = {1024u, 1024u, 1024u};
    owned.request.target.maxWorkgroupInvocations = 1024u;
    owned.request.target.maxWorkgroupSharedMemoryBytes = 32768u;
    owned.request.layout = BindingLayout{0u, 0u, 0u, 128u};
    return owned;
}

} // namespace ShaderRecompiler::Golden
