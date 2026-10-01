// Host depth/stencil image lifetime and per-draw transitions (AGC graphics subsystem).
// Mirrors RenderTarget (Resources.cpp) for image/memory ownership: every Vulkan object is
// released on construction failure and on destruction. No guest memory is touched.
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"

namespace AgcDriver::Graphics {
namespace {

// All aspects of the host format: a framebuffer view of a combined format must name both.
VkImageAspectFlags aspectsOf(VkFormat format) {
    if (format == VK_FORMAT_S8_UINT) return VK_IMAGE_ASPECT_STENCIL_BIT;
    return DepthFormatHasStencil(format) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
}

}

ResidentDepth::ResidentDepth(const Context& context, const DepthTarget& target) : context(context), description(target) {
    format = ResolveDepthFormat(context, target);
    constexpr auto usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImageFormatProperties supported{};
    Check(context.imageFormatProperties(context.physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported), "vkGetPhysicalDeviceImageFormatProperties depth");
    Require(target.extent.width <= supported.maxExtent.width && target.extent.height <= supported.maxExtent.height && (supported.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0, "depth surface exceeds device image limits");
    Require(target.extent.width <= context.limits.maxFramebufferWidth && target.extent.height <= context.limits.maxFramebufferHeight, "depth surface exceeds framebuffer limits");
    try {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {target.extent.width, target.extent.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(context.Function<PFN_vkCreateImage>("vkCreateImage")(context.device, &info, nullptr, &image), "vkCreateImage depth");
        VkMemoryRequirements requirements{};
        context.Function<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(context.device, image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = context.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(context.Function<PFN_vkAllocateMemory>("vkAllocateMemory")(context.device, &allocation, nullptr, &memory), "vkAllocateMemory depth");
        Check(context.Function<PFN_vkBindImageMemory>("vkBindImageMemory")(context.device, image, memory, 0), "vkBindImageMemory depth");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {aspectsOf(format), 0, 1, 0, 1};
        Check(context.Function<PFN_vkCreateImageView>("vkCreateImageView")(context.device, &viewInfo, nullptr, &view), "vkCreateImageView depth");
    } catch (...) {
        release();
        throw;
    }
}

ResidentDepth::~ResidentDepth() {
    release();
}

void ResidentDepth::release() noexcept {
    if (view) context.Function<PFN_vkDestroyImageView>("vkDestroyImageView")(context.device, view, nullptr);
    if (image) context.Function<PFN_vkDestroyImage>("vkDestroyImage")(context.device, image, nullptr);
    if (memory) context.Function<PFN_vkFreeMemory>("vkFreeMemory")(context.device, memory, nullptr);
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

bool ResidentDepth::Matches(const DepthTarget& target) const {
    return description.depthFormat == target.depthFormat && description.hasStencil == target.hasStencil && description.depthAddress == target.depthAddress && description.stencilAddress == target.stencilAddress && description.extent.width == target.extent.width && description.extent.height == target.extent.height;
}

DepthLoadOps ResidentDepth::LoadOps(const DepthTarget& target) const {
    DepthLoadOps ops;
    // An aspect the guest did not bind (stencil-only surface on a D24S8 image, say) is never
    // tested or written by the pipeline, so it keeps DONT_CARE.
    if (target.HasDepth()) ops.depth = DepthAspectLoadOp(target.clearDepth, depthDefined);
    if (target.hasStencil && DepthFormatHasStencil(format)) ops.stencil = DepthAspectLoadOp(target.clearStencil, stencilDefined);
    return ops;
}

void ResidentDepth::Begin(VkCommandBuffer commands, const DepthTarget& target) {
    // Every draw is its own render pass (attachments stay in DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
    // so consecutive draws need a write-after-write/read-after-write dependency on the
    // fragment-test stages; the first use starts from UNDEFINED with nothing to wait for.
    const bool first = layout == VK_IMAGE_LAYOUT_UNDEFINED;
    constexpr VkAccessFlags access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    constexpr VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = first ? 0u : access;
    barrier.dstAccessMask = access;
    barrier.oldLayout = layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspectsOf(format), 0, 1, 0, 1};
    context.Function<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(commands, first ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : tests, tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    // A clear defines the whole aspect (validateDepthClear guarantees full coverage).
    depthDefined = depthDefined || target.clearDepth;
    stencilDefined = stencilDefined || target.clearStencil;
}

}
