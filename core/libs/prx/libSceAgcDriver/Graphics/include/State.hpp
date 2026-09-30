// Decoded guest graphics state for one draw (AGC graphics subsystem).
// Owns the State/ShaderStages/ColorTarget/DepthStencilState value types and the
// register-to-Vulkan decode entry points. Plain values, no ownership, thread-safe to copy.
// Register semantics and rejections are specified in docs/spec/gpu-driver.md.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_STATE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_STATE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Execution/include/QueueState.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include "Recompiler.hpp"

namespace AgcDriver::Graphics {

enum class ShaderPath {
    Vertex,
    Geometry,
    Tessellation,
    TessellationGeometry
};

struct ShaderStages {
    ShaderPath path;
    std::uint32_t registerValue;
    std::uint32_t vertexWaveSize;
    std::uint32_t fragmentWaveSize;
    std::optional<ShaderRecompiler::MeshConfiguration> mesh;
    std::optional<ShaderRecompiler::TessellationConfiguration> tessellation;
};

struct ColorTarget {
    std::uint64_t address;
    VkExtent2D extent;
    VkFormat format;
    std::size_t bytes;
    std::uint8_t componentMapping;
    ColorTileMode tileMode = ColorTileMode::Linear;
};

/**
 * @brief Host-side translation of the guest depth/stencil registers.
 *
 * Decoded from DB_DEPTH_CONTROL (cx 0x200), DB_STENCIL_CONTROL (0x10b),
 * DB_STENCILREFMASK (0x10c), DB_STENCILREFMASK_BF (0x10d) and the depth-bounds
 * pair (0x8/0x9). The renderer owns no depth image yet, so this only feeds
 * VkPipelineDepthStencilStateCreateInfo (ignored by Vulkan for a render pass
 * without a depth attachment); see docs/spec/gpu-driver.md "Depth/stencil".
 */
struct DepthStencilState {
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_NEVER;
    VkStencilOpState front{};
    VkStencilOpState back{};
    float minDepthBounds = 0;
    float maxDepthBounds = 1;
    /// DB_DEPTH_CONTROL bit 30: write colour even when the depth test fails.
    bool colorWriteOnDepthFail = false;
    /// DB_DEPTH_CONTROL bit 31: suppress colour writes when the depth test passes.
    bool disableColorWriteOnDepthPass = false;
};

struct State {
    ShaderStages stages;
    ColorTarget color;
    bool hasColorTarget;
    bool rectList = false;
    VkExtent2D renderExtent;
    VkPrimitiveTopology topology;
    VkViewport viewport;
    bool negativeOneToOne;
    VkRect2D scissor;
    VkCullModeFlags cullMode;
    VkFrontFace frontFace;
    VkPipelineColorBlendAttachmentState blend;
    std::array<float, 4> blendConstants;
    DepthStencilState depthStencil;
};

/**
 * @brief Builds the Vulkan depth/stencil descriptor for a decoded state.
 * @param state Decoded guest depth/stencil state.
 * @return Create info with every field set from @p state; no pNext chain.
 */
VkPipelineDepthStencilStateCreateInfo ToVulkan(const DepthStencilState& state);

/**
 * @brief Maps an AGC compare function (ZFUNC / STENCILFUNC, 0..7) to Vulkan.
 * @param value 3-bit register field.
 * @return Matching VkCompareOp. The AGC numbering equals the Vulkan order.
 */
VkCompareOp DecodeCompareOp(std::uint32_t value);

/**
 * @brief Maps an AGC stencil operation (StencilOp enum, 0..15) to Vulkan.
 * @param value 4-bit register field: 0 KEEP, 1 ZERO, 2 ONES, 3 REPLACE_TEST, 4 REPLACE_OP,
 *        5 ADD_CLAMP, 6 SUB_CLAMP, 7 INVERT, 8 ADD_WRAP, 9 SUB_WRAP, 10 AND, 11 OR, 12 XOR,
 *        13 NAND, 14 NOR, 15 XNOR (AMD gfx10 register database).
 * @param writeMask The face's STENCILWRITEMASK; zero makes every op a KEEP.
 * @param opValue The face's STENCILOPVAL: the ADD/SUB operand (must be 1) and the XOR operand.
 * @return Matching VkStencilOp. ONES, AND, OR, NAND, NOR, XNOR, ADD/SUB with an operand other
 *         than 1 and XOR that flips only part of the written bits throw (no Vulkan equivalent).
 */
VkStencilOp DecodeStencilOp(std::uint32_t value, std::uint32_t writeMask = 0xffu, std::uint32_t opValue = 1u);

ShaderStages DecodeShaderStages(const QueueState& queue);
State DecodeState(const QueueState& queue);

}

#endif
