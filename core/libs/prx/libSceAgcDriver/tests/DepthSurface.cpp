/**
 * @file DepthSurface.cpp
 * @brief GoogleTest coverage for the host depth/stencil surface (Graphics/src/DepthFormat.cpp and
 *        Graphics/src/DepthSurface.cpp): format policy, loadOp mapping, ResidentDepth lifetime,
 *        per-draw barriers and aspect defined-ness.
 *
 * Hermetic and host-only: a mock Vulkan device table records calls, no GPU, no guest memory and
 * no game data. Decoding of the guest registers lives in tests/DepthStencilState.cpp.
 */
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <map>
#include <string>

namespace {

using namespace AgcDriver::Graphics;

/// Recorded state of the mock Vulkan device. Reset by every fixture.
struct MockDevice {
    int liveImages = 0;
    int liveViews = 0;
    int liveMemory = 0;
    std::uint64_t nextHandle = 1;
    std::vector<VkImageMemoryBarrier> barriers;
    std::vector<VkPipelineStageFlags> srcStages;
    VkImageCreateInfo lastImage{};
    VkImageViewCreateInfo lastView{};
    /// Formats the mock device reports as depth-attachment capable.
    std::vector<VkFormat> depthFormats;
    VkExtent3D maxExtent{16384, 16384, 1};
} mock;

template<typename THandle>
THandle makeHandle() {
    return reinterpret_cast<THandle>(static_cast<std::uintptr_t>(mock.nextHandle++));
}

VKAPI_ATTR VkResult VKAPI_CALL mockCreateImage(VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* image) {
    mock.lastImage = *info;
    ++mock.liveImages;
    *image = makeHandle<VkImage>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL mockDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) { --mock.liveImages; }
VKAPI_ATTR void VKAPI_CALL mockGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* requirements) {
    *requirements = {4096, 256, 1};
}
VKAPI_ATTR VkResult VKAPI_CALL mockAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* memory) {
    ++mock.liveMemory;
    *memory = makeHandle<VkDeviceMemory>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL mockFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) { --mock.liveMemory; }
VKAPI_ATTR VkResult VKAPI_CALL mockBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL mockCreateImageView(VkDevice, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* view) {
    mock.lastView = *info;
    ++mock.liveViews;
    *view = makeHandle<VkImageView>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL mockDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) { --mock.liveViews; }
VKAPI_ATTR void VKAPI_CALL mockCmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags src, VkPipelineStageFlags, VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*, std::uint32_t, const VkBufferMemoryBarrier*, std::uint32_t imageCount, const VkImageMemoryBarrier* images) {
    mock.srcStages.push_back(src);
    for (std::uint32_t i = 0; i < imageCount; ++i) mock.barriers.push_back(images[i]);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mockProc(VkDevice, const char* name) {
    static const std::map<std::string, PFN_vkVoidFunction> table{
        {"vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(mockCreateImage)},
        {"vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(mockDestroyImage)},
        {"vkGetImageMemoryRequirements", reinterpret_cast<PFN_vkVoidFunction>(mockGetImageMemoryRequirements)},
        {"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(mockAllocateMemory)},
        {"vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(mockFreeMemory)},
        {"vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(mockBindImageMemory)},
        {"vkCreateImageView", reinterpret_cast<PFN_vkVoidFunction>(mockCreateImageView)},
        {"vkDestroyImageView", reinterpret_cast<PFN_vkVoidFunction>(mockDestroyImageView)},
        {"vkCmdPipelineBarrier", reinterpret_cast<PFN_vkVoidFunction>(mockCmdPipelineBarrier)},
    };
    const auto it = table.find(name);
    return it == table.end() ? nullptr : it->second;
}

VKAPI_ATTR void VKAPI_CALL mockFormatProperties(VkPhysicalDevice, VkFormat format, VkFormatProperties* properties) {
    *properties = {};
    for (const auto supported : mock.depthFormats) {
        if (supported == format) properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL mockImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties* properties) {
    *properties = {};
    properties->maxExtent = mock.maxExtent;
    properties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    return VK_SUCCESS;
}

Context mockContext() {
    Context context{};
    context.deviceProc = mockProc;
    context.formatProperties = mockFormatProperties;
    context.imageFormatProperties = mockImageFormatProperties;
    context.memory.memoryTypeCount = 1;
    context.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.limits.maxFramebufferWidth = 8192;
    context.limits.maxFramebufferHeight = 8192;
    return context;
}

DepthTarget target(DepthSurfaceFormat format, bool stencil, std::uint32_t size = 64) {
    DepthTarget result;
    result.depthFormat = format;
    result.hasStencil = stencil;
    result.depthAddress = format == DepthSurfaceFormat::None ? 0 : 0x100000;
    result.stencilAddress = stencil ? 0x200000 : 0;
    result.extent = {size, size};
    return result;
}

class DepthSurfaceDevice : public ::testing::Test {
protected:
    void SetUp() override {
        mock = MockDevice{};
        mock.depthFormats = {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_S8_UINT};
    }
};

// Invariant: DB_RENDER_CONTROL clears map to loadOp CLEAR, a previously cleared aspect is LOADed,
// and a never-cleared aspect is DONT_CARE (a write-only draw does not define it).
TEST(DepthLoadOp, TruthTable) {
    EXPECT_EQ(DepthAspectLoadOp(true, false), VK_ATTACHMENT_LOAD_OP_CLEAR);
    EXPECT_EQ(DepthAspectLoadOp(true, true), VK_ATTACHMENT_LOAD_OP_CLEAR);
    EXPECT_EQ(DepthAspectLoadOp(false, true), VK_ATTACHMENT_LOAD_OP_LOAD);
    EXPECT_EQ(DepthAspectLoadOp(false, false), VK_ATTACHMENT_LOAD_OP_DONT_CARE);
}

// Invariant: the candidate lists keep Z32_FLOAT at float precision (never D24) and offer the
// combined formats for Z16+stencil because Vulkan has no 16-bit depth+stencil format.
TEST(DepthFormat, Candidates) {
    using F = DepthSurfaceFormat;
    EXPECT_EQ(DepthFormatCandidates(F::Z16, false), (std::vector<VkFormat>{VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT}));
    EXPECT_EQ(DepthFormatCandidates(F::Z32Float, false), (std::vector<VkFormat>{VK_FORMAT_D32_SFLOAT}));
    EXPECT_EQ(DepthFormatCandidates(F::Z16, true), (std::vector<VkFormat>{VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}));
    EXPECT_EQ(DepthFormatCandidates(F::Z32Float, true), (std::vector<VkFormat>{VK_FORMAT_D32_SFLOAT_S8_UINT}));
    EXPECT_EQ(DepthFormatCandidates(F::None, true).front(), VK_FORMAT_S8_UINT);
    EXPECT_TRUE(DepthFormatCandidates(F::None, false).empty());
}

// Invariant: selection takes the first supported candidate; nothing supported yields nullopt.
TEST(DepthFormat, SelectionFallsBackAndFails) {
    using F = DepthSurfaceFormat;
    const auto only = [](std::vector<VkFormat> formats) {
        return [formats](VkFormat format) { return std::find(formats.begin(), formats.end(), format) != formats.end(); };
    };
    EXPECT_EQ(SelectDepthFormat(F::Z16, false, only({VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT})), VK_FORMAT_D16_UNORM);
    EXPECT_EQ(SelectDepthFormat(F::Z16, false, only({VK_FORMAT_D32_SFLOAT})), VK_FORMAT_D32_SFLOAT) << "no D16: widen to D32";
    EXPECT_EQ(SelectDepthFormat(F::Z16, true, only({VK_FORMAT_D32_SFLOAT_S8_UINT})), VK_FORMAT_D32_SFLOAT_S8_UINT);
    EXPECT_FALSE(SelectDepthFormat(F::Z32Float, true, only({VK_FORMAT_D24_UNORM_S8_UINT})).has_value()) << "D24 would quantise Z32F";
    EXPECT_FALSE(SelectDepthFormat(F::Z32Float, false, only({})).has_value());
    EXPECT_EQ(SelectDepthFormat(F::None, true, only({VK_FORMAT_D24_UNORM_S8_UINT})), VK_FORMAT_D24_UNORM_S8_UINT) << "stencil-only falls back to a combined format";
}

// Invariant: DepthFormatHasStencil names exactly the formats with a stencil aspect.
TEST(DepthFormat, StencilAspectQuery) {
    EXPECT_TRUE(DepthFormatHasStencil(VK_FORMAT_S8_UINT));
    EXPECT_TRUE(DepthFormatHasStencil(VK_FORMAT_D24_UNORM_S8_UINT));
    EXPECT_TRUE(DepthFormatHasStencil(VK_FORMAT_D32_SFLOAT_S8_UINT));
    EXPECT_FALSE(DepthFormatHasStencil(VK_FORMAT_D16_UNORM));
    EXPECT_FALSE(DepthFormatHasStencil(VK_FORMAT_D32_SFLOAT));
}

// Invariant: ResolveDepthFormat consults the device (depth-stencil attachment feature) and
// rejects, with a logged error, a guest surface the device cannot host.
TEST_F(DepthSurfaceDevice, ResolveUsesDeviceSupport) {
    const auto context = mockContext();
    EXPECT_EQ(ResolveDepthFormat(context, target(DepthSurfaceFormat::Z16, false)), VK_FORMAT_D16_UNORM);
    mock.depthFormats = {VK_FORMAT_D32_SFLOAT};
    EXPECT_EQ(ResolveDepthFormat(context, target(DepthSurfaceFormat::Z16, false)), VK_FORMAT_D32_SFLOAT);
    EXPECT_THROW(ResolveDepthFormat(context, target(DepthSurfaceFormat::Z32Float, true)), std::runtime_error);
    EXPECT_THROW(ResolveDepthFormat(context, DepthTarget{}), std::runtime_error) << "unbound target";
}

// Invariant: the host image is a single-sample 2D depth-attachment image of the guest extent with a
// view naming every aspect of the chosen format; destruction releases every Vulkan object.
TEST_F(DepthSurfaceDevice, CreatesAndReleasesImage) {
    const auto context = mockContext();
    {
        ResidentDepth depth(context, target(DepthSurfaceFormat::Z32Float, true, 128));
        EXPECT_EQ(depth.Format(), VK_FORMAT_D32_SFLOAT_S8_UINT);
        EXPECT_EQ(mock.lastImage.format, VK_FORMAT_D32_SFLOAT_S8_UINT);
        EXPECT_EQ(mock.lastImage.extent.width, 128u);
        EXPECT_EQ(mock.lastImage.extent.height, 128u);
        EXPECT_EQ(mock.lastImage.samples, VK_SAMPLE_COUNT_1_BIT);
        EXPECT_EQ(mock.lastImage.usage, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        EXPECT_EQ(mock.lastView.subresourceRange.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        EXPECT_EQ(mock.liveImages, 1);
        EXPECT_EQ(mock.liveViews, 1);
        EXPECT_EQ(mock.liveMemory, 1);
        EXPECT_NE(depth.View(), VK_NULL_HANDLE);
    }
    EXPECT_EQ(mock.liveImages, 0);
    EXPECT_EQ(mock.liveViews, 0);
    EXPECT_EQ(mock.liveMemory, 0);
    ResidentDepth depthOnly(context, target(DepthSurfaceFormat::Z16, false));
    EXPECT_EQ(mock.lastView.subresourceRange.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// Failure mode: a surface larger than the device image or framebuffer limits is rejected before
// any object is created, so nothing leaks.
TEST_F(DepthSurfaceDevice, OversizedSurfaceRejectedWithoutLeak) {
    const auto context = mockContext();
    EXPECT_THROW(ResidentDepth(context, target(DepthSurfaceFormat::Z32Float, false, 16384)), std::runtime_error) << "beyond framebuffer limit";
    mock.maxExtent = {32, 32, 1};
    EXPECT_THROW(ResidentDepth(context, target(DepthSurfaceFormat::Z32Float, false, 64)), std::runtime_error) << "beyond image limit";
    EXPECT_EQ(mock.liveImages + mock.liveViews + mock.liveMemory, 0);
}

// Invariant: aspects start undefined; a draw without a clear leaves them undefined (DONT_CARE), a
// clearing draw uses CLEAR and defines the aspect, and every later draw LOADs it. Stencil and depth
// are tracked independently.
TEST_F(DepthSurfaceDevice, LoadOpsFollowDefinedness) {
    const auto context = mockContext();
    ResidentDepth depth(context, target(DepthSurfaceFormat::Z32Float, true));
    const auto commands = reinterpret_cast<VkCommandBuffer>(std::uintptr_t{1});
    auto draw = target(DepthSurfaceFormat::Z32Float, true);
    EXPECT_EQ(depth.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_DONT_CARE}));
    depth.Begin(commands, draw);
    EXPECT_FALSE(depth.DepthDefined());
    EXPECT_FALSE(depth.StencilDefined());
    draw.clearDepth = true;
    EXPECT_EQ(depth.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE}));
    depth.Begin(commands, draw);
    EXPECT_TRUE(depth.DepthDefined());
    EXPECT_FALSE(depth.StencilDefined());
    draw.clearDepth = false;
    EXPECT_EQ(depth.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_DONT_CARE}));
    draw.clearStencil = true;
    EXPECT_EQ(depth.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR}));
    depth.Begin(commands, draw);
    EXPECT_TRUE(depth.StencilDefined());
    draw.clearStencil = false;
    EXPECT_EQ(depth.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD}));
}

// Invariant: an aspect the guest did not bind keeps DONT_CARE even on a combined host image: a
// depth-only guest surface on a D32S8 device format has no stencil loadOp to honour, and a
// stencil-only guest surface never loads the depth aspect.
TEST_F(DepthSurfaceDevice, UnboundAspectsKeepDontCare) {
    auto context = mockContext();
    mock.depthFormats = {VK_FORMAT_D24_UNORM_S8_UINT};
    ResidentDepth stencilOnly(context, target(DepthSurfaceFormat::None, true));
    EXPECT_EQ(stencilOnly.Format(), VK_FORMAT_D24_UNORM_S8_UINT);
    auto draw = target(DepthSurfaceFormat::None, true);
    draw.clearStencil = true;
    EXPECT_EQ(stencilOnly.LoadOps(draw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_CLEAR}));
    mock.depthFormats = {VK_FORMAT_D16_UNORM};
    ResidentDepth depthOnly(context, target(DepthSurfaceFormat::Z16, false));
    auto depthDraw = target(DepthSurfaceFormat::Z16, false);
    depthDraw.clearDepth = true;
    EXPECT_EQ(depthOnly.LoadOps(depthDraw), (DepthLoadOps{VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE}));
}

// Invariant: the first use transitions UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL with nothing to
// wait for; later draws (each its own render pass) get a fragment-test write-after-write dependency
// in the same layout, so consecutive draws never race on the depth buffer.
TEST_F(DepthSurfaceDevice, BarriersBetweenDraws) {
    const auto context = mockContext();
    ResidentDepth depth(context, target(DepthSurfaceFormat::Z32Float, false));
    const auto commands = reinterpret_cast<VkCommandBuffer>(std::uintptr_t{1});
    const auto draw = target(DepthSurfaceFormat::Z32Float, false);
    depth.Begin(commands, draw);
    depth.Begin(commands, draw);
    ASSERT_EQ(mock.barriers.size(), 2u);
    EXPECT_EQ(mock.barriers[0].oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(mock.barriers[0].newLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(mock.barriers[0].srcAccessMask, 0u);
    EXPECT_EQ(mock.srcStages[0], static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));
    EXPECT_EQ(mock.barriers[1].oldLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(mock.barriers[1].newLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_NE(mock.barriers[1].srcAccessMask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0u);
    EXPECT_NE(mock.srcStages[1] & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0u);
    EXPECT_EQ(mock.barriers[1].image, depth.Image());
    EXPECT_EQ(mock.barriers[1].subresourceRange.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// Invariant (review finding on PR #55): eviction never drops the surface being requested and never
// the whole cache. Under capacity nothing is evicted; at capacity the least recently used other
// entry goes; a cache holding only the requested surface evicts nothing.
TEST(DepthEviction, LeastRecentlyUsedNeverTheRequested) {
    const std::vector<DepthCacheUse> uses{{10, 5, true}, {20, 1, true}, {30, 9, true}};
    EXPECT_FALSE(SelectDepthEviction(uses, 4, 10).has_value()) << "under capacity";
    EXPECT_EQ(SelectDepthEviction(uses, 3, 10), std::optional<std::uint64_t>(20));
    EXPECT_EQ(SelectDepthEviction(uses, 3, 20), std::optional<std::uint64_t>(10)) << "the requested entry is skipped even when oldest";
    EXPECT_FALSE(SelectDepthEviction({{10, 1, true}}, 1, 10).has_value()) << "only the requested surface is cached";
    EXPECT_FALSE(SelectDepthEviction({}, 0, 10).has_value());
}

// Invariant (review follow-up on PR #55): surfaces with no defined data are evicted before any
// surface holding cleared contents, however recently the undefined one was used, so capacity
// pressure only destroys real data when every other cached surface holds some.
TEST(DepthEviction, UndefinedSurfacesGoBeforeDefinedOnes) {
    const std::vector<DepthCacheUse> uses{{10, 1, true}, {20, 9, false}, {30, 5, false}, {40, 2, true}};
    EXPECT_EQ(SelectDepthEviction(uses, 4, 99), std::optional<std::uint64_t>(30)) << "oldest undefined, not oldest overall";
    EXPECT_EQ(SelectDepthEviction(uses, 4, 30), std::optional<std::uint64_t>(20)) << "requested undefined entry is skipped";
    const std::vector<DepthCacheUse> allDefined{{10, 4, true}, {20, 2, true}};
    EXPECT_EQ(SelectDepthEviction(allDefined, 2, 99), std::optional<std::uint64_t>(20)) << "falls back to the oldest defined";
}

// Invariant: a surface is identified by base addresses, extent and formats; any change means a
// different surface (the cache replaces it with a fresh, undefined one).
TEST_F(DepthSurfaceDevice, MatchesSurfaceIdentity) {
    const auto context = mockContext();
    const auto base = target(DepthSurfaceFormat::Z32Float, true);
    ResidentDepth depth(context, base);
    EXPECT_TRUE(depth.Matches(base));
    auto changed = base;
    changed.clearDepth = true;  // per-draw flags do not change identity
    changed.clearDepthValue = 0.5f;
    EXPECT_TRUE(depth.Matches(changed));
    changed = base;
    changed.depthAddress += 0x1000;
    EXPECT_FALSE(depth.Matches(changed));
    changed = base;
    changed.stencilAddress += 0x1000;
    EXPECT_FALSE(depth.Matches(changed));
    changed = base;
    changed.extent.width = 32;
    EXPECT_FALSE(depth.Matches(changed));
    changed = base;
    changed.depthFormat = DepthSurfaceFormat::Z16;
    EXPECT_FALSE(depth.Matches(changed));
    changed = base;
    changed.hasStencil = false;
    EXPECT_FALSE(depth.Matches(changed));
}

}  // namespace
