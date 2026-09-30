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

// Invariant: every supported AGC stencil op maps to the expected Vulkan op; the bitwise
// ops (9..15) have no Vulkan equivalent and throw.
TEST(DepthStencilState, StencilOpMapping) {
    using AgcDriver::Graphics::DecodeStencilOp;
    const VkStencilOp expected[] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE,
        VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT,
        VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP};
    for (std::uint32_t op = 0; op < 9; ++op) EXPECT_EQ(DecodeStencilOp(op), expected[op]) << op;
    for (std::uint32_t op = 9; op < 16; ++op) EXPECT_THROW(DecodeStencilOp(op), std::runtime_error) << op;
}

// Invariant: front state comes from the low control fields; with BACKFACE_ENABLE clear the
// back face mirrors the front, with it set the _BF fields and DB_STENCILREFMASK_BF are used.
TEST(DepthStencilState, StencilFrontAndBackFaces) {
    auto queue = makeQueue();
    // Stencil on, STENCILFUNC=LESS(1). Ops: fail=INVERT(6) pass=INCR_WRAP(7) zfail=ZERO(1).
    queue.context[0x200] = 1u | (1u << 8u);
    queue.context[0x10b] = 6u | (7u << 4u) | (1u << 8u);
    queue.context[0x10c] = 0x5u | (0xf0u << 8u) | (0x3cu << 16u) | (0x5u << 24u);
    auto ds = DecodeState(queue).depthStencil;
    EXPECT_TRUE(ds.stencilTestEnable);
    EXPECT_EQ(ds.front.compareOp, VK_COMPARE_OP_LESS);
    EXPECT_EQ(ds.front.failOp, VK_STENCIL_OP_INVERT);
    EXPECT_EQ(ds.front.passOp, VK_STENCIL_OP_INCREMENT_AND_WRAP);
    EXPECT_EQ(ds.front.depthFailOp, VK_STENCIL_OP_ZERO);
    EXPECT_EQ(ds.front.reference, 0x5u);
    EXPECT_EQ(ds.front.compareMask, 0xf0u);
    EXPECT_EQ(ds.front.writeMask, 0x3cu);
    EXPECT_EQ(ds.back.passOp, ds.front.passOp) << "back mirrors front without BACKFACE_ENABLE";
    EXPECT_EQ(ds.back.writeMask, ds.front.writeMask);

    // BACKFACE_ENABLE: STENCILFUNC_BF=GREATER(4), ops fail=KEEP pass=DECR_CLAMP(5) zfail=REPLACE_TEST(2).
    queue.context[0x200] |= 0x80u | (4u << 20u);
    queue.context[0x10b] |= (5u << 16u) | (2u << 20u);
    queue.context[0x10d] = 0x9u | (0x0fu << 8u) | (0xffu << 16u);
    ds = DecodeState(queue).depthStencil;
    EXPECT_EQ(ds.back.compareOp, VK_COMPARE_OP_GREATER);
    EXPECT_EQ(ds.back.failOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(ds.back.passOp, VK_STENCIL_OP_DECREMENT_AND_CLAMP);
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
// expressed by a single Vulkan reference and must be rejected, not mistranslated.
TEST(DepthStencilState, ReplaceOpWithDifferentValueRejected) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u;
    queue.context[0x10b] = 3u << 4u;
    queue.context[0x10c] = 0x1u | (0x2u << 24u);
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue.context[0x10c] = 0x2u | (0x2u << 24u);
    EXPECT_NO_THROW(DecodeState(queue));
}

// Failure mode: reserved DB_DEPTH_CONTROL bits and bitwise stencil ops are rejected.
TEST(DepthStencilState, ReservedBitsAndBitwiseOpsRejected) {
    auto queue = makeQueue();
    queue.context[0x200] = 1u << 12u;
    EXPECT_THROW(DecodeState(queue), std::runtime_error);
    queue = makeQueue();
    queue.context[0x200] = 1u;
    queue.context[0x10b] = 9u;
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

// Invariant: the conditional colour-write bits (30/31) are decoded and, without a bound
// depth surface, inert (the draw is accepted).
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
