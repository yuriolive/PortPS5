// core/libs/prx/libSceAgcDriver/Graphics/include/Texture.hpp
// Sampled host image for one guest texture view: either detiled from a guest-memory snapshot or
// copied from a ResidentColor render target. Owned by TextureCache and shared with draws through
// shared_ptr; the image is only released (or recycled into ResidentImagePool) once no draw holds it.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_TEXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_TEXTURE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ResidentImagePool.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureDetiler.hpp"

namespace AgcDriver::Graphics {

class ResidentColor;
class CommandBatch;

class Texture {
public:
    Texture(const Context& context, TextureDetiler& detiler, const GuestTextureResource& descriptor, VkComponentMapping components, std::span<const std::byte> snapshot);
    /**
     * @brief Copies a resident render target into a sampled image.
     * @param context Vulkan device handles; must outlive the Texture.
     * @param source Resident colour target to copy; must be a single-mip, single-layer 2D surface.
     * @param descriptor Guest T# view of @p source: 2D, extent equal to the target, 32-bit non-block-compressed texels.
     * @param components View swizzle applied when sampling.
     * @throws std::runtime_error when the combination is unsupported or a Vulkan call fails; nothing is skipped silently.
     * @param pool Source of recycled destination images; the Texture returns its image on destruction. Null disables recycling.
     */
    Texture(const Context& context, const std::shared_ptr<ResidentColor>& source, const GuestTextureResource& descriptor, VkComponentMapping components, std::shared_ptr<ResidentImagePool> pool);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView View() const;
    VkDeviceSize AllocationBytes() const { return allocationBytes; }

private:
    void release() noexcept;

    Context context;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceSize allocationBytes = 0;
    std::shared_ptr<ResidentColor> source;
    std::shared_ptr<ResidentImagePool> pool;
    ResidentImageKey poolKey;
    /// True only while image and memory are bound and idle-able; guards recycling after a failed construction.
    bool recyclable = false;
    std::unique_ptr<CommandBatch> upload;
};

}

#endif
