// Vulkan graphics pipeline, render pass and framebuffer for one decoded State (AGC graphics subsystem).
// A Pipeline owns its Vulkan objects and releases them on destruction; the colour and depth surfaces
// it references must outlive it (the pipeline cache holds them).
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINE_HPP

#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"

namespace AgcDriver::Graphics {

class Pipeline {
public:
    /**
     * @param depth Host depth/stencil surface when state.depthTarget is bound, otherwise null.
     * @param depthLoad loadOps the render pass is built with (part of the pipeline identity).
     */
    Pipeline(const Context& context, const State& state, const RenderTarget* target, const ResidentDepth* depth, const DepthLoadOps& depthLoad, const ShaderResources& resources, std::span<const CompiledShader> shaders);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    VkPipelineLayout Layout() const;
    /// Begins the render pass; clear values come from @p depthTarget (used by loadOp CLEAR aspects).
    void Begin(VkCommandBuffer commands, VkExtent2D extent, const DepthTarget& depthTarget) const;
    void PushConstants(VkCommandBuffer commands, std::span<const CompiledShader> shaders) const;

private:
    void release() noexcept;
    Context context;
    std::vector<VkShaderModule> _modules;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool hasColorAttachment = false;
    bool hasDepthAttachment = false;
};

void ValidateShaderPair(const ShaderRecompiler::RecompileResult& vertex, const ShaderRecompiler::RecompileResult& fragment);
void ValidateShaders(std::span<const CompiledShader> shaders, const State& state, const VkPhysicalDeviceSubgroupProperties& subgroup, bool fragmentShaderBarycentric);

}

#endif
