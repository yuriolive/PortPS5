// Resident colour targets and host depth surfaces (AGC graphics subsystem).
// ResidentColor mirrors a guest colour surface with write-back; the depth side hands out host-only
// ResidentDepth surfaces. Used under the device graphics serialisation plus guest-memory tracking locks.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RENDERCACHE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RENDERCACHE_HPP

#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GpuColorTransfer.hpp"
#include "prx/libc/include/GuestMemoryTracking.hpp"
#include <map>

namespace AgcDriver::Graphics {

class ResidentColor {
public:
    ResidentColor(const Context& context, const ColorTarget& color);
    ~ResidentColor();
    void Begin(VkCommandBuffer commands);
    void Download(VkCommandBuffer commands);
    void Commit();
    void Transition(VkCommandBuffer commands, VkImageLayout layout);
    RenderTarget& Target() { return transfer.Target(color, false); }
    const ColorTarget& Description() const { return color; }
    bool Valid() const { return valid; }
    bool Dirty() const { return dirty; }
    std::uint64_t Generation() const { return generation; }
    void Invalidate();
    void ReleaseMemory();
    bool SharesPages(const ColorTarget& other) const;

private:
    void resolveCpuAccess(GuestMemoryTracking::Access access);
    Context context;
    ColorTarget color;
    GpuColorTransfer transfer;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool valid = false;
    bool dirty = false;
    std::uint64_t generation = 0;
    std::unique_ptr<GuestMemoryTracking::Watch> memoryWatch;
};

class RenderCache {
public:
    explicit RenderCache(const Context& context) : context(context) {}
    ~RenderCache();
    std::shared_ptr<ResidentColor> Get(const ColorTarget& color, bool blending);
    std::shared_ptr<ResidentColor> Find(std::uint64_t address) const;
    /**
     * @brief Returns the host depth/stencil surface for @p target, creating it on first use.
     * @param target Bound guest surface (must be Bound()).
     * @return Shared surface keyed by its guest write base; a surface whose identity (base,
     *         extent, format) changed is replaced with a fresh, undefined one. Pipelines that
     *         still reference the old surface keep it alive through their shared_ptr.
     * @throws std::runtime_error when the device cannot host the surface.
     */
    std::shared_ptr<ResidentDepth> GetDepth(const DepthTarget& target);
    void Resolve(std::uint64_t address, std::size_t bytes, bool writable);
    void Flush();

private:
    Context context;
    std::map<std::uint64_t, std::shared_ptr<ResidentColor>> entries;
    std::map<std::uint64_t, std::shared_ptr<ResidentDepth>> depthEntries;
    std::map<std::uint64_t, std::uint64_t> depthLastUse;
    std::uint64_t depthClock = 0;
};

}

#endif
