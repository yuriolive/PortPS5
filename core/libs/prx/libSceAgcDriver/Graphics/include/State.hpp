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
 * @brief Maps an AGC stencil operation (0..8) to Vulkan.
 * @param value 4-bit register field.
 * @return Matching VkStencilOp. The bitwise ops 9..14 throw (no Vulkan equivalent).
 */
VkStencilOp DecodeStencilOp(std::uint32_t value);

ShaderStages DecodeShaderStages(const QueueState& queue);
State DecodeState(const QueueState& queue);

}

#endif
