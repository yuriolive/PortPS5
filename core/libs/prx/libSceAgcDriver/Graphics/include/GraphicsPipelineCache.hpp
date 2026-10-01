// Keyed cache of graphics pipelines (AGC graphics subsystem).
// Entries pin the colour and depth surfaces their framebuffer references. Used only from the draw
// recording path, which is serialised by the device.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_GRAPHICSPIPELINECACHE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_GRAPHICSPIPELINECACHE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/RenderCache.hpp"
#include <list>
#include <map>
#include <string>

namespace AgcDriver::Graphics {

class GraphicsPipelineCache {
public:
    explicit GraphicsPipelineCache(const Context& context) : context(context) {}
    /**
     * @param depth Host depth surface for state.depthTarget (null when unbound). Its loadOps for
     *        this draw (ResidentDepth::LoadOps) are part of the key: they are baked into the render pass.
     */
    std::shared_ptr<Pipeline> Get(const State& state, const std::shared_ptr<ResidentColor>& target, const std::shared_ptr<ResidentDepth>& depth, const ShaderResources& resources, std::span<const CompiledShader> shaders);

private:
    struct Entry {
        std::string key;
        std::shared_ptr<ResidentColor> target;
        std::shared_ptr<ResidentDepth> depth;
        std::shared_ptr<Pipeline> pipeline;
    };
    Context context;
    std::list<Entry> entries;
    std::map<std::string, std::list<Entry>::iterator> lookup;
};

}

#endif
