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

/// DB_Z_INFO.FORMAT (cx 0x10, bits 0-1). Value 2 is reserved and rejected by the decoder.
enum class DepthSurfaceFormat : std::uint8_t {
    None = 0,
    Z16 = 1,
    Z32Float = 3,
};

/**
 * @brief Guest depth/stencil surface bound by DB_Z_INFO / DB_STENCIL_INFO for one draw.
 *
 * Decoded from DB_Z_INFO (cx 0x10), DB_STENCIL_INFO (0x11), DB_Z_WRITE_BASE (0x14, hi 0x1c),
 * DB_STENCIL_WRITE_BASE (0x15, hi 0x1d), DB_DEPTH_SIZE_XY (0x7), DB_DEPTH_VIEW (0x2),
 * DB_RENDER_CONTROL (0x0), DB_DEPTH_CLEAR (0xb) and DB_STENCIL_CLEAR (0xa). The surface is
 * host-owned: the addresses only identify it across draws, guest memory is never read or
 * written (see docs/spec/gpu-driver.md "Depth/stencil"). Default-constructed means unbound.
 */
struct DepthTarget {
    DepthSurfaceFormat depthFormat = DepthSurfaceFormat::None;
    bool hasStencil = false;
    std::uint64_t depthAddress = 0;
    std::uint64_t stencilAddress = 0;
    VkExtent2D extent{};
    /// DB_RENDER_CONTROL DEPTH_CLEAR_ENABLE on a bound depth aspect: this draw clears depth.
    bool clearDepth = false;
    /// DB_RENDER_CONTROL STENCIL_CLEAR_ENABLE on a bound stencil aspect.
    bool clearStencil = false;
    float clearDepthValue = 0;
    std::uint8_t clearStencilValue = 0;

    bool HasDepth() const { return depthFormat != DepthSurfaceFormat::None; }
    bool Bound() const { return HasDepth() || hasStencil; }
};

/**
 * @brief Host-side translation of the guest depth/stencil registers.
 *
 * Decoded from DB_DEPTH_CONTROL (cx 0x200), DB_STENCIL_CONTROL (0x10b),
 * DB_STENCILREFMASK (0x10c), DB_STENCILREFMASK_BF (0x10d) and the depth-bounds
 * pair (0x8/0x9). Fields reflect the registers; the pipeline builder reconciles them
 * with the bound DepthTarget through ToVulkan(state, target, ...); see
 * docs/spec/gpu-driver.md "Depth/stencil".
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
    /// Bound depth/stencil surface; default (unbound) when DB_Z_INFO and DB_STENCIL_INFO are 0.
    DepthTarget depthTarget;
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
 * @brief Builds the Vulkan depth/stencil descriptor for a draw against a bound surface.
 * @param state Decoded guest depth/stencil state.
 * @param target Surface bound for this draw (may be unbound).
 * @param depthBoundsSupported VkPhysicalDeviceFeatures::depthBounds was enabled on the device.
 * @return Create info reconciled with @p target: depth state is dropped without a depth
 *         aspect, stencil state without a stencil aspect, and an aspect being cleared by
 *         this draw (DB_RENDER_CONTROL clear) neither tests nor writes, because a hardware
 *         clear draw writes the clear value instead of running the tests.
 * @throws std::runtime_error (logged abort path upstream) when the draw needs the depth-bounds
 *         test on a bound depth aspect and the device lacks the feature.
 */
VkPipelineDepthStencilStateCreateInfo ToVulkan(const DepthStencilState& state, const DepthTarget& target, bool depthBoundsSupported);

/**
 * @brief True when the draw reads the bound surface (a test is enabled on a bound aspect and
 *        the aspect is not being cleared by this draw), so the aspect must hold defined data.
 * @param state Decoded guest depth/stencil state.
 * @param target Surface bound for this draw.
 * @param[out] depth Set when the depth aspect is read (depth test or depth bounds).
 * @param[out] stencil Set when the stencil aspect is read (stencil test).
 */
void DepthStencilReads(const DepthStencilState& state, const DepthTarget& target, bool& depth, bool& stencil);

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

/**
 * @brief Applies DB_DEPTH_CONTROL bits 30/31 (conditional colour writes) to @p state.
 * @param state Fully decoded state; blend.colorWriteMask is cleared when colour can never be written.
 * @throws std::runtime_error when colour would have to be written on depth fail with a depth
 *         test that can fail (needs two passes; unsupported, see docs/spec/gpu-driver.md).
 */
void ApplyConditionalColorWrites(State& state);

/**
 * @brief Validates that a DB_RENDER_CONTROL clear draw is expressible as a render-pass loadOp CLEAR.
 * @param state Fully decoded state.
 * @throws std::runtime_error unless the draw is a rect-list whose scissor and render extent
 *         cover the whole depth surface (a partial clear would clear too much).
 */
void ValidateDepthClear(const State& state);

ShaderStages DecodeShaderStages(const QueueState& queue);
State DecodeState(const QueueState& queue);

}

#endif
