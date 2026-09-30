/**
 * @file DepthStencilState.cpp
 * @brief GoogleTest coverage for depth/stencil decoding in Graphics/src/State.cpp
 *        (DB_DEPTH_CONTROL, DB_STENCIL_CONTROL, DB_STENCILREFMASK[_BF], depth bounds).
 *
 * Hermetic and host-only: DecodeState is fed synthetic register maps, no GPU,
 * no guest memory and no game data. Every draw uses no colour target (target
 * mask 0); per-channel colour write masks are covered in tests/Graphics.cpp,
 * which owns a registered colour surface.
 */
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include <gtest/gtest.h>
#include <bit>

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

// Failure mode: REPLACE_OP with an op value different from the test value cannot be
// expressed by a single Vulkan reference and must be rejected, not mistranslated. With a zero
// write mask the op has no effect, so the mismatch is harmless and accepted (sharpemu case).
TEST(DepthStencilState, ReplaceOpWithDifferentValueRejected) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u;
    queue.context[0x10b] = 4u << 4u;  // STENCILZPASS = REPLACE_OP
    queue.context[0x10c] = 0x1u | (0xffu << 16u) | (0x2u << 24u);
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue.context[0x10c] = 0x2u | (0xffu << 16u) | (0x2u << 24u);
    EXPECT_NO_THROW(DecodeState(queue));
    queue.context[0x10c] = 0x1u | (0x2u << 24u);  // write mask 0
    const auto ds = DecodeState(queue).depthStencil;
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.front.writeMask, 0u);
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

// Invariant: a bound depth or stencil surface with an enabled test is rejected loudly
// (no host depth image yet); a bound surface with every test disabled is inert and accepted.
TEST(DepthStencilState, BoundSurfaceRejectsEnabledTests) {
    auto queue = makeQueue();
    queue.context[0x10] = 3;
    EXPECT_NO_THROW(DecodeState(queue));
    queue.context[0x200] = 2;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue = makeQueue();
    queue.context[0x11] = 1;
    queue.context[0x200] = 1;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
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

// Invariant: bit 30 (colour write on depth fail) is decoded and inert without a depth test.
// Bit 31 (disable colour writes on depth pass) would suppress colour output, which the host
// pipeline cannot express, so it is rejected whenever CB_TARGET_MASK enables a colour write
// and only accepted for a draw that writes no colour.
TEST(DepthStencilState, ConditionalColorWriteBitsDecoded) {
    auto queue = makeQueue();
    queue.context[0x200] = 0x40000000u;
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.colorWriteOnDepthFail);
    EXPECT_FALSE(ds.disableColorWriteOnDepthPass);
    queue.context[0x200] = 0x80000000u;  // CB_TARGET_MASK is 0: no colour is written
    ds = DecodeState(queue).depthStencil;
    EXPECT_FALSE(ds.colorWriteOnDepthFail);
    EXPECT_TRUE(ds.disableColorWriteOnDepthPass);
    queue.context[0x8e] = 0xf;
    try {
        DecodeState(queue);
        FAIL() << "bit 31 with colour writes enabled must be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("bit 31"), std::string::npos) << error.what();
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

}  // namespace
