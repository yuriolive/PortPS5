/**
 * @file DepthStencilState.cpp
 * @brief GoogleTest coverage for depth/stencil decoding in Graphics/src/State.cpp
 *        (DB_DEPTH_CONTROL, DB_STENCIL_CONTROL, DB_STENCILREFMASK[_BF], depth bounds) and the
 *        depth/stencil surface registers (DB_Z_INFO, DB_STENCIL_INFO, bases, size, clears).
 *
 * Hermetic and host-only: DecodeState is fed synthetic register maps, no GPU,
 * no guest memory and no game data. Every draw uses no colour target (target
 * mask 0); per-channel colour write masks are covered in tests/Graphics.cpp,
 * which owns a registered colour surface.
 */
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <string>

namespace {

using AgcDriver::Graphics::DecodeState;

/// Minimal register set accepted by DecodeState for a draw without colour target.
AgcDriver::QueueState makeQueue() {
    AgcDriver::QueueState queue;
    queue.userConfig[0x242] = 4;
    queue.context = {
        {0x2d5, 0x2000}, {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0}, {0x8f, 0}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4},
        {0xc, 0}, {0xd, 0x400040},
        {0x81, 0x80000000}, {0x82, 0x400040}, {0x90, 0x80000000}, {0x91, 0x400040},
        {0x94, 0x80000000}, {0x95, 0x400040}, {0xb4, 0},
        {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0}
    };
    return queue;
}

// Invariant: the reset (all-zero) register file yields a fully disabled depth/stencil state.
TEST(DepthStencilState, ResetStateIsDisabled) {
    const auto ds = DecodeState(makeQueue()).depthStencil;
    EXPECT_FALSE(ds.depthTestEnable);
    EXPECT_FALSE(ds.depthWriteEnable);
    EXPECT_FALSE(ds.stencilTestEnable);
    EXPECT_FALSE(ds.depthBoundsTestEnable);
}

// Invariant: Z_ENABLE/Z_WRITE_ENABLE map to the Vulkan flags, and ZFUNC 0..7 maps
// one-to-one to VK_COMPARE_OP_*. A write without the test is inert and cleared.
TEST(DepthStencilState, DepthTestAndCompareOps) {
    for (std::uint32_t func = 0; func < 8; ++func) {
        auto queue = makeQueue();
        queue.context[0x200] = 2u | 4u | (func << 4u);
        const auto ds = DecodeState(queue).depthStencil;
        EXPECT_TRUE(ds.depthTestEnable);
        EXPECT_TRUE(ds.depthWriteEnable);
        EXPECT_EQ(ds.depthCompareOp, static_cast<VkCompareOp>(func));
        EXPECT_EQ(AgcDriver::Graphics::ToVulkan(ds).depthCompareOp, static_cast<VkCompareOp>(func));
    }
    auto queue = makeQueue();
    queue.context[0x200] = 4u;
    const auto ds = DecodeState(queue).depthStencil;
    EXPECT_FALSE(ds.depthTestEnable);
    EXPECT_FALSE(ds.depthWriteEnable) << "depth write with the test disabled is inert";
}

// Invariant: the AGC StencilOp enum (AMD gfx10 register database: 0 KEEP, 1 ZERO, 2 ONES,
// 3 REPLACE_TEST, 4 REPLACE_OP, 5 ADD_CLAMP, 6 SUB_CLAMP, 7 INVERT, 8 ADD_WRAP, 9 SUB_WRAP,
// 10 AND, 11 OR, 12 XOR, 13 NAND, 14 NOR, 15 XNOR) maps to the matching Vulkan op. Values
// with no Vulkan equivalent throw.
TEST(DepthStencilState, StencilOpMapping) {
    using AgcDriver::Graphics::DecodeStencilOp;
    EXPECT_EQ(DecodeStencilOp(0), VK_STENCIL_OP_KEEP);
    EXPECT_EQ(DecodeStencilOp(1), VK_STENCIL_OP_ZERO);
    EXPECT_EQ(DecodeStencilOp(3), VK_STENCIL_OP_REPLACE);
    EXPECT_EQ(DecodeStencilOp(4), VK_STENCIL_OP_REPLACE);
    EXPECT_EQ(DecodeStencilOp(5), VK_STENCIL_OP_INCREMENT_AND_CLAMP);
    EXPECT_EQ(DecodeStencilOp(6), VK_STENCIL_OP_DECREMENT_AND_CLAMP);
    EXPECT_EQ(DecodeStencilOp(7), VK_STENCIL_OP_INVERT);
    EXPECT_EQ(DecodeStencilOp(8), VK_STENCIL_OP_INCREMENT_AND_WRAP);
    EXPECT_EQ(DecodeStencilOp(9), VK_STENCIL_OP_DECREMENT_AND_WRAP);
    for (const std::uint32_t op : {2u, 10u, 11u, 13u, 14u, 15u}) EXPECT_THROW(DecodeStencilOp(op), std::runtime_error) << op;
    EXPECT_THROW(DecodeStencilOp(16), std::runtime_error);
}

// Invariant: ADD/SUB add STENCILOPVAL, Vulkan steps by one, so only an op value of 1 is
// representable. A zero write mask makes any op a KEEP, even an unsupported one.
TEST(DepthStencilState, StencilAddSubtractOperand) {
    using AgcDriver::Graphics::DecodeStencilOp;
    for (const std::uint32_t op : {5u, 6u, 8u, 9u}) {
        EXPECT_NO_THROW(DecodeStencilOp(op, 0xff, 1));
        EXPECT_THROW(DecodeStencilOp(op, 0xff, 2), std::runtime_error) << op;
    }
    EXPECT_EQ(DecodeStencilOp(10, 0, 0), VK_STENCIL_OP_KEEP);
    EXPECT_EQ(DecodeStencilOp(5, 0, 7), VK_STENCIL_OP_KEEP);
}

// Invariant: XOR flips the written bits that are set in STENCILOPVAL. All written bits set
// is INVERT, none set is KEEP, a partial flip has no Vulkan equivalent and is rejected.
TEST(DepthStencilState, StencilXorOperands) {
    using AgcDriver::Graphics::DecodeStencilOp;
    EXPECT_EQ(DecodeStencilOp(12, 0x0f, 0x0f), VK_STENCIL_OP_INVERT);
    EXPECT_EQ(DecodeStencilOp(12, 0x0f, 0xff), VK_STENCIL_OP_INVERT);
    EXPECT_EQ(DecodeStencilOp(12, 0x0f, 0xf0), VK_STENCIL_OP_KEEP);
    EXPECT_THROW(DecodeStencilOp(12, 0xff, 0x0f), std::runtime_error);
}

// Invariant: DB_DEPTH_CONTROL 0x002005B7 decodes to stencil+Z+Z-write on, ZFUNC=LEQUAL(3),
// BACKFACE on, STENCILFUNC=5 (NOTEQUAL), STENCILFUNC_BF=2 (EQUAL); bit layout per the AMD
// gfx10 register database. Cross-checked against sharpemu's register-decode vector.
TEST(DepthStencilState, DepthControlRegisterVector) {
    auto queue = makeQueue();
    queue.context[0x200] = 0x002005B7u;
    const auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.stencilTestEnable);
    EXPECT_TRUE(ds.depthTestEnable);
    EXPECT_TRUE(ds.depthWriteEnable);
    EXPECT_FALSE(ds.depthBoundsTestEnable);
    EXPECT_EQ(ds.depthCompareOp, VK_COMPARE_OP_LESS_OR_EQUAL);
    EXPECT_EQ(ds.front.compareOp, VK_COMPARE_OP_NOT_EQUAL);
    EXPECT_EQ(ds.back.compareOp, VK_COMPARE_OP_EQUAL);
}

// Invariant: front state comes from the low control fields; with BACKFACE_ENABLE clear the
// back face mirrors the front, with it set the _BF fields and DB_STENCILREFMASK_BF are used.
TEST(DepthStencilState, StencilFrontAndBackFaces) {
    auto queue = makeQueue();
    // Stencil on, STENCILFUNC=LESS(1). Ops: fail=INVERT(7) pass=ADD_WRAP(8) zfail=ZERO(1);
    // STENCILOPVAL=1 because ADD_WRAP adds it.
    queue.context[0x200] = 1u | (1u << 8u);
    queue.context[0x10b] = 7u | (8u << 4u) | (1u << 8u);
    queue.context[0x10c] = 0x5u | (0xf0u << 8u) | (0x3cu << 16u) | (0x1u << 24u);
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.stencilTestEnable);
    EXPECT_EQ(ds.front.compareOp, VK_COMPARE_OP_LESS);
    EXPECT_EQ(ds.front.failOp, VK_STENCIL_OP_INVERT);
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_INCREMENT_AND_WRAP);  // ADD_WRAP
    EXPECT_EQ(ds.front.depthFailOp, VK_STENCIL_OP_ZERO);
    EXPECT_EQ(ds.front.reference, 0x5u);
    EXPECT_EQ(ds.front.compareMask, 0xf0u);
    EXPECT_EQ(ds.front.writeMask, 0x3cu);
    EXPECT_EQ(ds.back.passOp, ds.front.passOp) << "back mirrors front without BACKFACE_ENABLE";
    EXPECT_EQ(ds.back.writeMask, ds.front.writeMask);

    // BACKFACE_ENABLE: STENCILFUNC_BF=GREATER(4), ops fail=KEEP pass=SUB_CLAMP(6) zfail=REPLACE_TEST(3).
    queue.context[0x200] |= 0x80u | (4u << 20u);
    queue.context[0x10b] |= (6u << 16u) | (3u << 20u);
    queue.context[0x10d] = 0x9u | (0x0fu << 8u) | (0xffu << 16u) | (0x1u << 24u);
    ds = DecodeState(queue).depthStencil;
    EXPECT_EQ(ds.back.compareOp, VK_COMPARE_OP_GREATER);
    EXPECT_EQ(ds.back.failOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.back.passOp, VK_STENCIL_OP_DECREMENT_AND_CLAMP);  // SUB_CLAMP
    EXPECT_EQ(ds.back.depthFailOp, VK_STENCIL_OP_REPLACE);
    EXPECT_EQ(ds.back.reference, 0x9u);
    EXPECT_EQ(ds.back.compareMask, 0x0fu);
    EXPECT_EQ(ds.back.writeMask, 0xffu);
    const auto vk = AgcDriver::Graphics::ToVulkan(ds);
    EXPECT_EQ(vk.stencilTestEnable, VK_TRUE);
    EXPECT_EQ(vk.back.compareOp, VK_COMPARE_OP_GREATER);
    EXPECT_EQ(vk.front.compareOp, VK_COMPARE_OP_LESS);
}

// Invariant: Vulkan has one reference per face serving as compare reference and REPLACE value.
// REPLACE_OP with an op value different from the test value is therefore only expressible when
// the stencil function ignores the reference (NEVER/ALWAYS): the reference is then repurposed as
// the replace value. With a reference-reading function (LESS here) it must be rejected, not
// mistranslated. With a zero write mask the op has no effect, so the mismatch is harmless.
TEST(DepthStencilState, ReplaceOpWithDifferentValue) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u | (1u << 8u);  // stencil on, STENCILFUNC = LESS (reads the reference)
    queue.context[0x10b] = 4u << 4u;         // STENCILZPASS = REPLACE_OP
    queue.context[0x10c] = 0x1u | (0xffu << 16u) | (0x2u << 24u);
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue.context[0x10c] = 0x2u | (0xffu << 16u) | (0x2u << 24u);  // op value == test value
    EXPECT_NO_THROW(DecodeState(queue));
    queue.context[0x10c] = 0x1u | (0x2u << 24u);  // write mask 0
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.front.writeMask, 0u);
    // ALWAYS / NEVER ignore the reference: REPLACE_OP writes the op value through the reference.
    for (const std::uint32_t func : {0u, 7u}) {
        queue.context[0x200] = 1u | (func << 8u);
        queue.context[0x10c] = 0x1u | (0xffu << 16u) | (0x2u << 24u);
        ds = DecodeState(queue).depthStencil;
        EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_REPLACE) << func;
        EXPECT_EQ(ds.front.reference, 0x2u) << "replace value is carried by the reference";
    }
}

// Invariant: STENCIL_ONES writes 0xff under the write mask, expressible as REPLACE when the
// function ignores the reference; it must agree with any other replace op in the face, and is
// rejected when the function reads a reference that differs from 0xff under the mask.
TEST(DepthStencilState, StencilOnesAsReplace) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u | (7u << 8u);  // ALWAYS
    queue.context[0x10b] = 2u << 4u;         // STENCILZPASS = ONES
    queue.context[0x10c] = 0x1u | (0x0fu << 16u);
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_REPLACE);
    EXPECT_EQ(ds.front.reference & 0x0fu, 0x0fu);
    // ONES together with REPLACE_TEST (test value 1) writes two different values: rejected.
    queue.context[0x10b] = (2u << 4u) | (3u << 8u);
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    // A reference-reading function with a test value != 0xff cannot carry the ONES value.
    queue.context[0x10b] = 2u << 4u;
    queue.context[0x200] = 1u | (1u << 8u);  // LESS
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    // ...but a reference already equal to the ones value under the write mask is fine.
    queue.context[0x10c] = 0xffu | (0xffu << 16u);
    EXPECT_NO_THROW(DecodeState(queue));
}

// Failure mode: reserved DB_DEPTH_CONTROL bits and bitwise stencil ops are rejected.
TEST(DepthStencilState, ReservedBitsAndBitwiseOpsRejected) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u << 12u;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue = makeQueue();
    queue.context[0x200] = 1u;
    queue.context[0x10b] = 10u;  // STENCIL_AND
    queue.context[0x10c] = 0xffu << 16u;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
}

/// DB_DEPTH_SIZE_XY for a 64x64 surface: X_MAX in bits 0-13, Y_MAX in bits 16-29.
constexpr std::uint32_t kSize64 = 63u | (63u << 16u);

/// Binds a Z32_FLOAT (and optionally stencil) surface at guest address 0x100000 (registers hold
/// the address >> 8; DB_Z_READ_BASE and DB_Z_WRITE_BASE agree).
void bindDepth(AgcDriver::QueueState& queue, bool stencil = false, std::uint32_t zFormat = 3) {
    queue.context[0x10] = zFormat;
    queue.context[0x7] = kSize64;
    queue.context[0x12] = queue.context[0x14] = 0x1000;
    if (stencil) {
        queue.context[0x11] = 1;
        queue.context[0x13] = queue.context[0x15] = 0x2000;
    }
}

// Invariant: with neither DB_Z_INFO nor DB_STENCIL_INFO format set nothing is bound and the
// surface registers are not even read (a stale DB_DEPTH_SIZE_XY must not reject the draw).
TEST(DepthSurface, UnboundSurfaceIsInert) {
    auto queue = makeQueue();
    queue.context[0x200] = 0x2u | 0x4u;
    queue.context[0x14] = 0;
    const auto state = DecodeState(queue);
    EXPECT_FALSE(state.depthTarget.Bound());
    EXPECT_TRUE(state.depthStencil.depthTestEnable);
}

// Invariant: a bound depth-only surface decodes format, extent and the base address
// ((HI << 40) | (LO << 8)); the draw may now enable depth tests (previously rejected).
TEST(DepthSurface, DepthOnlyDecode) {
    auto queue = makeQueue();
    bindDepth(queue);
    queue.context[0x1c] = 0x1;  // DB_Z_WRITE_BASE_HI
    queue.context[0x1a] = 0x1;  // DB_Z_READ_BASE_HI
    queue.context[0x200] = 2u | 4u | (1u << 4u);
    const auto target = DecodeState(queue).depthTarget;
    EXPECT_TRUE(target.HasDepth());
    EXPECT_FALSE(target.hasStencil);
    EXPECT_EQ(target.depthFormat, AgcDriver::Graphics::DepthSurfaceFormat::Z32Float);
    EXPECT_EQ(target.depthAddress, (1ull << 40u) | (0x1000ull << 8u));
    EXPECT_EQ(target.extent.width, 64u);
    EXPECT_EQ(target.extent.height, 64u);
    EXPECT_FALSE(target.clearDepth);
    queue.context[0x10] = 1;  // Z16
    EXPECT_EQ(DecodeState(queue).depthTarget.depthFormat, AgcDriver::Graphics::DepthSurfaceFormat::Z16);
}

// Invariant: DB_STENCIL_INFO.FORMAT binds a stencil surface; depth+stencil and stencil-only
// both decode, with the stencil base taken from DB_STENCIL_WRITE_BASE.
TEST(DepthSurface, StencilSurfaceDecode) {
    auto queue = makeQueue();
    bindDepth(queue, true);
    auto target = DecodeState(queue).depthTarget;
    EXPECT_TRUE(target.HasDepth());
    EXPECT_TRUE(target.hasStencil);
    EXPECT_EQ(target.stencilAddress, 0x2000ull << 8u);
    queue.context[0x10] = 0;  // stencil only
    queue.context[0x12] = queue.context[0x14] = 0;
    target = DecodeState(queue).depthTarget;
    EXPECT_FALSE(target.HasDepth());
    EXPECT_TRUE(target.hasStencil);
    EXPECT_TRUE(target.Bound());
}

// Invariant: a stencil format with null stencil bases and no stencil use is an absent plane,
// not an error. Regression for a gate title that declares Z32F + STENCIL_8 but never
// allocates the stencil plane (DB_STENCIL_{READ,WRITE}_BASE = 0, STENCIL_ENABLE = 0): the
// decoder used to abort the whole title with "stencil surface bound with a null
// DB_STENCIL_WRITE_BASE". Precondition: depth bound normally, stencil bases zeroed, no
// stencil enable/clear. Expected: depth stays bound, hasStencil drops to false.
TEST(DepthSurface, UnusedStencilWithNullBaseIsAbsent) {
    auto queue = makeQueue();
    bindDepth(queue, true);
    queue.context[0x13] = queue.context[0x15] = 0;
    const auto target = DecodeState(queue).depthTarget;
    EXPECT_TRUE(target.HasDepth());
    EXPECT_FALSE(target.hasStencil);
    EXPECT_EQ(target.stencilAddress, 0ull);
    EXPECT_FALSE(target.clearStencil);
}

// Invariant: the null-stencil-base tolerance never hides real use. Stencil test/write
// (DB_DEPTH_CONTROL bit 0) or clear (DB_RENDER_CONTROL bit 1) with no memory still aborts,
// with a message that names the cause. Failure mode guarded: silently dropping a used plane.
TEST(DepthSurface, UsedStencilWithNullBaseStillRejected) {
    const auto expectRejected = [](auto mutate) {
        auto queue = makeQueue();
        bindDepth(queue, true);
        queue.context[0x13] = queue.context[0x15] = 0;
        mutate(queue);
        try {
            DecodeState(queue);
            FAIL() << "expected a rejection";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("null DB_STENCIL_WRITE_BASE"), std::string::npos) << error.what();
        }
    };
    expectRejected([](auto& q) { q.context[0x200] |= 1u; });
    expectRejected([](auto& q) { q.context[0x0] = 1u << 1u; });
}

// Failure modes of the surface decode: reserved format, MSAA, partially resident, mips and
// slices, missing size, null or split bases, bad address extension, reserved size bits.
TEST(DepthSurface, UnsupportedSurfaceRejected) {
    const auto rejects = [](const char* what, auto mutate) {
        auto queue = makeQueue();
        bindDepth(queue, true);
        mutate(queue);
        EXPECT_THROW(DecodeState(queue), std::runtime_error) << what;
    };
    rejects("reserved DB_Z_INFO.FORMAT 2", [](auto& q) { q.context[0x10] = 2; });
    rejects("MSAA depth", [](auto& q) { q.context[0x10] |= 1u << 2u; });
    rejects("partially resident depth", [](auto& q) { q.context[0x10] |= 1u << 12u; });
    rejects("partially resident stencil", [](auto& q) { q.context[0x11] |= 1u << 12u; });
    rejects("depth MAXMIP", [](auto& q) { q.context[0x10] |= 1u << 16u; });
    rejects("slice range", [](auto& q) { q.context[0x2] = 1u << 13u; });
    rejects("slice start", [](auto& q) { q.context[0x2] = 1u; });
    rejects("mip view", [](auto& q) { q.context[0x2] = 1u << 26u; });
    rejects("missing DB_DEPTH_SIZE_XY", [](auto& q) { q.context.erase(0x7); });
    rejects("reserved size bits", [](auto& q) { q.context[0x7] |= 1u << 14u; });
    rejects("null depth base", [](auto& q) { q.context[0x12] = q.context[0x14] = 0; });
    rejects("split depth bases", [](auto& q) { q.context[0x12] = 0x1001; });
    rejects("split stencil bases", [](auto& q) { q.context[0x13] = 0x2001; });
    rejects("null stencil base with stencil enabled", [](auto& q) { q.context[0x13] = q.context[0x15] = 0; q.context[0x200] |= 1u; });
    rejects("null stencil base with stencil clear", [](auto& q) { q.context[0x13] = q.context[0x15] = 0; q.context[0x0] = 1u << 1u; });
    rejects("null stencil write base only", [](auto& q) { q.context[0x15] = 0; });
    rejects("depth base extension", [](auto& q) { q.context[0x1c] = 0x100; q.context[0x1a] = 0x100; });
    rejects("depth-to-colour copy", [](auto& q) { q.context[0x0] = 1u << 2u; });
    rejects("stencil-to-colour copy", [](auto& q) { q.context[0x0] = 1u << 3u; });
    rejects("reserved render control bits", [](auto& q) { q.context[0x0] = 1u << 13u; });
}

// Invariant: compression/metadata bits that do not change the host image are accepted:
// SW_MODE/TILE_SURFACE_ENABLE in DB_Z_INFO, and RESUMMARIZE/COMPRESS_DISABLE/DECOMPRESS in
// DB_RENDER_CONTROL.
TEST(DepthSurface, LayoutAndMetadataBitsAreInert) {
    auto queue = makeQueue();
    bindDepth(queue);
    queue.context[0x10] |= (0x5u << 4u) | (1u << 29u) | (1u << 27u);
    queue.context[0x0] = (1u << 4u) | (1u << 5u) | (1u << 6u) | (1u << 12u);
    EXPECT_NO_THROW(DecodeState(queue));
}

// Invariant: DB_RENDER_CONTROL DEPTH_CLEAR_ENABLE/STENCIL_CLEAR_ENABLE on a bound aspect decode
// the clear flags and values (DB_DEPTH_CLEAR float, DB_STENCIL_CLEAR low byte). A clear draw is a
// full-surface rect-list; the clear flag on an unbound aspect is ignored.
TEST(DepthSurface, ClearDecode) {
    auto queue = makeQueue();
    bindDepth(queue, true);
    queue.userConfig[0x242] = 0x11;  // RECTLIST
    queue.context[0x0] = 3;
    queue.context[0xb] = std::bit_cast<std::uint32_t>(0.5f);
    queue.context[0xa] = 0x1a5;  // only bits 0-7 are the value
    auto target = DecodeState(queue).depthTarget;
    EXPECT_TRUE(target.clearDepth);
    EXPECT_TRUE(target.clearStencil);
    EXPECT_FLOAT_EQ(target.clearDepthValue, 0.5f);
    EXPECT_EQ(target.clearStencilValue, 0xa5);
    queue.context[0x0] = 2;  // stencil clear only
    target = DecodeState(queue).depthTarget;
    EXPECT_FALSE(target.clearDepth);
    EXPECT_TRUE(target.clearStencil);
    queue = makeQueue();
    bindDepth(queue);
    queue.context[0x0] = 2;  // stencil clear, but no stencil bound: ignored
    EXPECT_FALSE(DecodeState(queue).depthTarget.clearStencil);
    queue.context[0x0] = 1;
    queue.context[0xb] = std::bit_cast<std::uint32_t>(2.0f);  // clear value outside [0, 1]
    queue.userConfig[0x242] = 0x11;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
}

// Invariant: a clear becomes loadOp CLEAR for the whole attachment, so the draw must be a
// rect-list covering the whole surface; a triangle-list clear or a smaller scissor would clear
// more (or other) pixels than the guest asked for and is rejected.
TEST(DepthSurface, ClearMustCoverWholeSurface) {
    auto queue = makeQueue();
    bindDepth(queue);
    queue.context[0x0] = 1;
    queue.context[0xb] = 0;
    EXPECT_THROW(DecodeState(queue), std::runtime_error) << "triangle-list clear";
    queue.userConfig[0x242] = 0x11;
    EXPECT_NO_THROW(DecodeState(queue));
    queue.context[0x82] = 0x400020;  // window scissor bottom-right: 32 wide, smaller than the surface
    EXPECT_THROW(DecodeState(queue), std::runtime_error) << "partial clear";
    queue.context[0x82] = 0x400040;
    queue.context[0xd] = 0x200020;  // render extent smaller than the depth surface
    EXPECT_THROW(DecodeState(queue), std::runtime_error) << "render extent smaller than depth";
}

// Invariant: DB_DEPTH_VIEW Z_READ_ONLY (bit 24) clears depth writes and STENCIL_READ_ONLY
// (bit 25) turns every stencil op into a keep with a zero write mask.
TEST(DepthSurface, ReadOnlyViewDropsWrites) {
    auto queue = makeQueue();
    bindDepth(queue, true);
    queue.context[0x200] = 1u | 2u | 4u | (7u << 8u);
    queue.context[0x10b] = 1u | (1u << 4u) | (1u << 8u);  // ZERO ops
    queue.context[0x10c] = 0xffu << 16u;
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.depthWriteEnable);
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_ZERO);
    queue.context[0x2] = (1u << 24u) | (1u << 25u);
    ds = DecodeState(queue).depthStencil;
    EXPECT_FALSE(ds.depthWriteEnable);
    EXPECT_TRUE(ds.depthTestEnable);
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.back.depthFailOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.front.writeMask, 0u);
}

// Invariant: ToVulkan(state, target) reconciles the guest state with the bound aspects. No
// depth aspect drops depth test/write/bounds; no stencil aspect drops the stencil test; an
// aspect cleared by this draw neither tests nor writes; a fully present surface passes through.
TEST(DepthSurface, ToVulkanReconcilesWithTarget) {
    using AgcDriver::Graphics::DepthSurfaceFormat;
    AgcDriver::Graphics::DepthStencilState ds;
    ds.depthTestEnable = ds.depthWriteEnable = ds.depthBoundsTestEnable = ds.stencilTestEnable = true;
    ds.front.reference = ds.back.reference = 3;
    ds.minDepthBounds = 0.25f;
    AgcDriver::Graphics::DepthTarget target;
    auto vk = AgcDriver::Graphics::ToVulkan(ds, target, true);
    EXPECT_FALSE(vk.depthTestEnable || vk.depthWriteEnable || vk.depthBoundsTestEnable || vk.stencilTestEnable) << "unbound";
    target.depthFormat = DepthSurfaceFormat::Z32Float;
    vk = AgcDriver::Graphics::ToVulkan(ds, target, true);
    EXPECT_TRUE(vk.depthTestEnable && vk.depthWriteEnable && vk.depthBoundsTestEnable);
    EXPECT_FALSE(vk.stencilTestEnable) << "depth-only surface has no stencil aspect";
    EXPECT_FLOAT_EQ(vk.minDepthBounds, 0.25f);
    target.hasStencil = true;
    vk = AgcDriver::Graphics::ToVulkan(ds, target, true);
    EXPECT_TRUE(vk.stencilTestEnable);
    EXPECT_EQ(vk.front.reference, 3u);
    target.clearDepth = true;
    vk = AgcDriver::Graphics::ToVulkan(ds, target, true);
    EXPECT_FALSE(vk.depthTestEnable || vk.depthWriteEnable || vk.depthBoundsTestEnable) << "cleared depth";
    EXPECT_TRUE(vk.stencilTestEnable);
    target.clearStencil = true;
    vk = AgcDriver::Graphics::ToVulkan(ds, target, true);
    EXPECT_FALSE(vk.stencilTestEnable) << "cleared stencil";
}

// Invariant: the depth-bounds test needs VkPhysicalDeviceFeatures::depthBounds. A draw that
// uses it on a bound depth aspect without the feature is rejected with a logged error;
// without a depth aspect (or while clearing) the test is dropped and nothing is required.
TEST(DepthSurface, DepthBoundsRequiresFeature) {
    AgcDriver::Graphics::DepthStencilState ds;
    ds.depthBoundsTestEnable = true;
    AgcDriver::Graphics::DepthTarget target;
    EXPECT_NO_THROW(AgcDriver::Graphics::ToVulkan(ds, target, false));
    target.depthFormat = AgcDriver::Graphics::DepthSurfaceFormat::Z16;
    EXPECT_THROW(AgcDriver::Graphics::ToVulkan(ds, target, false), std::runtime_error);
    EXPECT_NO_THROW(AgcDriver::Graphics::ToVulkan(ds, target, true));
    target.clearDepth = true;
    EXPECT_NO_THROW(AgcDriver::Graphics::ToVulkan(ds, target, false));
}

// Invariant: DepthStencilReads reports which bound aspects a draw reads. Depth is read by the
// depth test or the bounds test, stencil by the stencil test; cleared or unbound aspects are not read.
TEST(DepthSurface, ReadsReflectEnabledTests) {
    using AgcDriver::Graphics::DepthStencilReads;
    AgcDriver::Graphics::DepthStencilState ds;
    AgcDriver::Graphics::DepthTarget target;
    target.depthFormat = AgcDriver::Graphics::DepthSurfaceFormat::Z32Float;
    target.hasStencil = true;
    bool depth = true;
    bool stencil = true;
    DepthStencilReads(ds, target, depth, stencil);
    EXPECT_FALSE(depth);
    EXPECT_FALSE(stencil);
    ds.depthWriteEnable = true;  // a write-only draw does not read
    DepthStencilReads(ds, target, depth, stencil);
    EXPECT_FALSE(depth);
    ds.depthBoundsTestEnable = true;
    ds.stencilTestEnable = true;
    DepthStencilReads(ds, target, depth, stencil);
    EXPECT_TRUE(depth);
    EXPECT_TRUE(stencil);
    target.clearDepth = true;
    target.clearStencil = true;
    DepthStencilReads(ds, target, depth, stencil);
    EXPECT_FALSE(depth);
    EXPECT_FALSE(stencil);
}

// Invariant: depth bounds decode min/max only when enabled; inverted bounds are rejected.
TEST(DepthStencilState, DepthBounds) {
    auto queue = makeQueue();
    queue.context[0x200] = 8u;
    queue.context[0x8] = std::bit_cast<std::uint32_t>(0.25f);
    queue.context[0x9] = std::bit_cast<std::uint32_t>(0.75f);
    const auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.depthBoundsTestEnable);
    EXPECT_FLOAT_EQ(ds.minDepthBounds, 0.25f);
    EXPECT_FLOAT_EQ(ds.maxDepthBounds, 0.75f);
    queue.context[0x8] = std::bit_cast<std::uint32_t>(0.9f);
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
}

// Invariant: bits 30/31 of DB_DEPTH_CONTROL are decoded into the state (this draw has no colour
// target, so they are accepted without touching any colour write mask).
TEST(DepthStencilState, ConditionalColorWriteBitsDecoded) {
    auto queue = makeQueue();
    queue.context[0x200] = 0x40000000u;
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.colorWriteOnDepthFail);
    EXPECT_FALSE(ds.disableColorWriteOnDepthPass);
    queue.context[0x200] = 0x80000000u;
    ds = DecodeState(queue).depthStencil;
    EXPECT_FALSE(ds.colorWriteOnDepthFail);
    EXPECT_TRUE(ds.disableColorWriteOnDepthPass);
}

/// A state with a colour target writing every channel, as DecodeState would produce it.
AgcDriver::Graphics::State colorState() {
    AgcDriver::Graphics::State state{};
    state.hasColorTarget = true;
    state.blend.colorWriteMask = 0xf;
    return state;
}

// Invariant: bit 31 (disable colour writes on depth pass) without bit 30 suppresses colour
// everywhere, which Vulkan expresses as a zero colour write mask (the depth/stencil tests still
// run). It is independent of whether a depth test is enabled or a surface is bound.
TEST(ConditionalColorWrites, DisableOnPassZeroesColorMask) {
    for (const bool depthBound : {false, true}) {
        auto state = colorState();
        state.depthStencil.disableColorWriteOnDepthPass = true;
        state.depthStencil.depthTestEnable = depthBound;
        if (depthBound) state.depthTarget.depthFormat = AgcDriver::Graphics::DepthSurfaceFormat::Z32Float;
        AgcDriver::Graphics::ApplyConditionalColorWrites(state);
        EXPECT_EQ(state.blend.colorWriteMask, 0u) << depthBound;
    }
}

// Invariant: with no depth test that can fail, bit 30 is inert and the default colour mask is kept;
// bits 30+31 together ("colour only where depth fails") degenerate to "never" and zero the mask.
TEST(ConditionalColorWrites, InertWithoutFailableDepthTest) {
    auto state = colorState();
    state.depthStencil.colorWriteOnDepthFail = true;
    AgcDriver::Graphics::ApplyConditionalColorWrites(state);
    EXPECT_EQ(state.blend.colorWriteMask, 0xfu);
    state.depthStencil.depthTestEnable = true;  // test enabled but no depth surface bound: cannot fail
    AgcDriver::Graphics::ApplyConditionalColorWrites(state);
    EXPECT_EQ(state.blend.colorWriteMask, 0xfu);
    state.depthStencil.disableColorWriteOnDepthPass = true;
    AgcDriver::Graphics::ApplyConditionalColorWrites(state);
    EXPECT_EQ(state.blend.colorWriteMask, 0u);
}

// Failure mode: bit 30 with a depth test that can fail needs colour written on depth fail (alone:
// both outcomes; with bit 31: failure only). Both need a second pass with an inverted compare, so
// they are rejected with a logged error rather than mistranslated. No colour target: nothing to do.
TEST(ConditionalColorWrites, FailableDepthTestWithBit30Rejected) {
    for (const bool bit31 : {false, true}) {
        auto state = colorState();
        state.depthTarget.depthFormat = AgcDriver::Graphics::DepthSurfaceFormat::Z16;
        state.depthStencil.depthTestEnable = true;
        state.depthStencil.colorWriteOnDepthFail = true;
        state.depthStencil.disableColorWriteOnDepthPass = bit31;
        EXPECT_THROW(AgcDriver::Graphics::ApplyConditionalColorWrites(state), std::runtime_error) << bit31;
        state.hasColorTarget = false;
        EXPECT_NO_THROW(AgcDriver::Graphics::ApplyConditionalColorWrites(state));
    }
}

// Invariant: with the stencil test disabled, stale stencil ops and replace values left in the
// registers (bitwise ops, REPLACE_OP with a differing op value) must not reject the draw.
TEST(DepthStencilState, DisabledStencilIgnoresStaleOperations) {
    auto queue = makeQueue();
    queue.context[0x200] = 2u;  // Z only, stencil off
    queue.context[0x10b] = 10u | (4u << 4u) | (15u << 8u);
    queue.context[0x10c] = 0x1u | (0xffu << 16u) | (0x2u << 24u);
    const auto ds = DecodeState(queue).depthStencil;
    EXPECT_FALSE(ds.stencilTestEnable);
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_KEEP);
    queue.context[0x200] = 3u;  // stencil on: the same registers are now rejected
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
}

// Invariant: a depth-stencil descriptor round-trips through ToVulkan field for field.
TEST(DepthStencilState, ToVulkanCopiesEveryField) {
    AgcDriver::Graphics::DepthStencilState ds;
    ds.depthTestEnable = true;
    ds.depthWriteEnable = true;
    ds.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    ds.minDepthBounds = 0.1f;
    ds.maxDepthBounds = 0.9f;
    ds.front.reference = 7;
    const auto vk = AgcDriver::Graphics::ToVulkan(ds);
    EXPECT_EQ(vk.sType, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    EXPECT_EQ(vk.depthTestEnable, VK_TRUE);
    EXPECT_EQ(vk.depthWriteEnable, VK_TRUE);
    EXPECT_EQ(vk.depthCompareOp, VK_COMPARE_OP_GREATER_OR_EQUAL);
    EXPECT_EQ(vk.stencilTestEnable, VK_FALSE);
    EXPECT_FLOAT_EQ(vk.minDepthBounds, 0.1f);
    EXPECT_FLOAT_EQ(vk.maxDepthBounds, 0.9f);
    EXPECT_EQ(vk.front.reference, 7u);
}

/// One viewport depth-transform vector: PA_CL_CLIP_CNTL bit 19 (DX_CLIP_SPACE_DEF), Z scale/offset
/// (PA_CL_VPORT_ZSCALE/ZOFFSET) and the Vulkan min/max depth the host viewport must get.
struct DepthRangeVector {
    bool zeroToOne;
    float scale;
    float offset;
    float expectedMin;
    float expectedMax;
};

class ViewportDepthRange : public ::testing::TestWithParam<DepthRangeVector> {};

// Invariant: the guest Z transform survives the conversion to a Vulkan depth range in both clip
// spaces, including reversed-Z (min > max) and a degenerate zero-scale range, and the host
// scale/offset reconstructed from the result equals the guest's. Vectors ported from SharpEmu
// `RenderExecutorStateTests.DynamicState_DepthRangePreservesTheGuestTransform` (GPL-2.0).
TEST_P(ViewportDepthRange, PreservesGuestTransform) {
    const auto v = GetParam();
    auto queue = makeQueue();
    queue.context[0x204] = v.zeroToOne ? 0x80000u : 0u;
    queue.context[0x113] = std::bit_cast<std::uint32_t>(v.scale);
    queue.context[0x114] = std::bit_cast<std::uint32_t>(v.offset);
    const auto state = DecodeState(queue);
    EXPECT_EQ(state.negativeOneToOne, !v.zeroToOne);
    EXPECT_FLOAT_EQ(state.viewport.minDepth, v.expectedMin);
    EXPECT_FLOAT_EQ(state.viewport.maxDepth, v.expectedMax);
    const float range = state.viewport.maxDepth - state.viewport.minDepth;
    EXPECT_FLOAT_EQ(v.zeroToOne ? range : range / 2.0f, v.scale);
    EXPECT_FLOAT_EQ(v.zeroToOne ? state.viewport.minDepth : (state.viewport.maxDepth + state.viewport.minDepth) / 2.0f, v.offset);
}

INSTANTIATE_TEST_SUITE_P(SharpEmuVectors, ViewportDepthRange, ::testing::Values(
    DepthRangeVector{false, 0.5f, 0.5f, 0.0f, 1.0f},
    DepthRangeVector{true, 0.5f, 0.5f, 0.5f, 1.0f},
    DepthRangeVector{false, -0.5f, 0.5f, 1.0f, 0.0f},
    DepthRangeVector{true, -1.0f, 1.0f, 1.0f, 0.0f},
    DepthRangeVector{false, 0.25f, 0.5f, 0.25f, 0.75f},
    DepthRangeVector{true, 0.25f, 0.5f, 0.5f, 0.75f},
    DepthRangeVector{false, 0.0f, 0.5f, 0.5f, 0.5f},
    DepthRangeVector{true, 1.0f, 0.0f, 0.0f, 1.0f}));

}  // namespace
