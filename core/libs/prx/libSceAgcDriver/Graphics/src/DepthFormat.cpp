// Pure depth/stencil format and loadOp policy (AGC graphics subsystem).
// No Vulkan calls are made here, so the functions run in hosted tests without a device.
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"

namespace AgcDriver::Graphics {

VkAttachmentLoadOp DepthAspectLoadOp(bool clear, bool defined) {
    if (clear) return VK_ATTACHMENT_LOAD_OP_CLEAR;
    return defined ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

std::vector<VkFormat> DepthFormatCandidates(DepthSurfaceFormat depth, bool stencil) {
    if (depth == DepthSurfaceFormat::None) {
        if (!stencil) return {};
        return {VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    }
    if (depth == DepthSurfaceFormat::Z16) {
        // A D32 fallback only widens precision; a 16-bit depth+stencil format does not exist.
        if (!stencil) return {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT};
        return {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    }
    // Z32_FLOAT must keep float precision: D24 would quantise it, so it is never a candidate.
    return {stencil ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D32_SFLOAT};
}

std::optional<VkFormat> SelectDepthFormat(DepthSurfaceFormat depth, bool stencil, const std::function<bool(VkFormat)>& supported) {
    for (const auto format : DepthFormatCandidates(depth, stencil)) {
        if (supported(format)) return format;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> SelectDepthEviction(const std::vector<DepthCacheUse>& entries, std::size_t capacity, std::uint64_t keep) {
    if (entries.size() < capacity) return std::nullopt;
    // Undefined surfaces hold nothing worth keeping, so they always go before a defined one.
    std::optional<DepthCacheUse> oldest;
    for (const auto& entry : entries) {
        if (entry.key == keep) continue;
        const bool better = !oldest || (oldest->defined && !entry.defined) || (oldest->defined == entry.defined && entry.lastUse < oldest->lastUse);
        if (better) oldest = entry;
    }
    if (!oldest) return std::nullopt;
    return oldest->key;
}

bool DepthFormatHasStencil(VkFormat format) {
    return format == VK_FORMAT_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat ResolveDepthFormat(const Context& context, const DepthTarget& target) {
    Require(target.Bound(), "no depth/stencil surface is bound");
    const auto selected = SelectDepthFormat(target.depthFormat, target.hasStencil, [&](VkFormat format) {
        VkFormatProperties properties{};
        context.formatProperties(context.physical, format, &properties);
        return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    });
    Require(selected.has_value(), "device supports no depth/stencil attachment format for the guest surface");
    return *selected;
}

}
