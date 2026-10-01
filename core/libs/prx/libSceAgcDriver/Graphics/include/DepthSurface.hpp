// Host-owned depth/stencil surface for the AGC graphics subsystem.
// ResidentDepth owns one VkImage/VkImageView per guest depth surface identity and tracks, per
// aspect, whether its contents are defined. The surface lives entirely on the host: guest
// memory is never read or written (no upload, no retile), see docs/spec/gpu-driver.md
// "Depth/stencil". Pure helpers (format choice, loadOp mapping) are free functions so they
// can be tested without a device. A ResidentDepth is created and used under the device's
// graphics (draw-recording) serialisation only; it is not internally synchronised.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEPTHSURFACE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEPTHSURFACE_HPP

#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include <functional>
#include <optional>
#include <vector>

namespace AgcDriver::Graphics {

/// Render-pass load operations for the two aspects of a depth/stencil attachment.
struct DepthLoadOps {
    VkAttachmentLoadOp depth = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentLoadOp stencil = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    bool operator==(const DepthLoadOps&) const = default;
};

/**
 * @brief Maps a DB_RENDER_CONTROL clear and the aspect's defined-ness to a loadOp.
 * @param clear This draw clears the aspect (DB_RENDER_CONTROL clear enable).
 * @param defined The aspect already holds data written by a previous clear.
 * @return CLEAR when clearing, LOAD when the contents are defined, otherwise DONT_CARE (a
 *         draw that only writes an undefined aspect leaves it undefined).
 */
VkAttachmentLoadOp DepthAspectLoadOp(bool clear, bool defined);

/**
 * @brief Host formats that can carry a guest surface, in preference order.
 * @param depth Guest depth format (None for a stencil-only surface).
 * @param stencil The guest binds a stencil surface.
 * @return Z16: D16 (then D32F); Z32F: D32F; Z16+stencil: D24S8 then D32FS8 (no 16-bit
 *         depth+stencil format exists); Z32F+stencil: D32FS8; stencil only: S8, D24S8, D32FS8.
 */
std::vector<VkFormat> DepthFormatCandidates(DepthSurfaceFormat depth, bool stencil);

/**
 * @brief First candidate accepted by @p supported, or nullopt when none is usable.
 * @param supported Predicate over a VkFormat (device depth-attachment support).
 */
std::optional<VkFormat> SelectDepthFormat(DepthSurfaceFormat depth, bool stencil, const std::function<bool(VkFormat)>& supported);

/// One depth-cache entry as seen by the eviction policy: surface key and last-use stamp.
struct DepthCacheUse {
    std::uint64_t key;
    std::uint64_t lastUse;
    /// The surface holds cleared data (either aspect); evicting it destroys the only copy.
    bool defined = false;
};

/**
 * @brief Picks the depth surface to evict when the cache is full.
 * @param entries Current entries with their last-use stamps (larger is more recent).
 * @param capacity Maximum number of entries; eviction is only requested at or above it.
 * @param keep Key of the surface being requested right now; it is never evicted, because
 *        dropping it would discard the contents (and defined-ness) the draw is about to use.
 * @return Key of the least recently used other entry that holds no defined data; only when
 *         every other entry is defined, the least recently used defined one. Nullopt when the
 *         cache is under capacity or holds nothing but @p keep. A defined surface that has to
 *         go loses its contents; a later read of it is rejected as never-cleared rather than
 *         drawn with garbage.
 */
std::optional<std::uint64_t> SelectDepthEviction(const std::vector<DepthCacheUse>& entries, std::size_t capacity, std::uint64_t keep);

/// True when @p format has a stencil aspect.
bool DepthFormatHasStencil(VkFormat format);

/**
 * @brief Chooses the device format for @p target using optimal-tiling depth-attachment support.
 * @throws std::runtime_error when the device supports none of the candidates.
 */
VkFormat ResolveDepthFormat(const Context& context, const DepthTarget& target);

class ResidentDepth {
public:
    /**
     * @brief Creates the host image for @p target.
     * @throws std::runtime_error when no format is supported or the extent exceeds device limits.
     */
    ResidentDepth(const Context& context, const DepthTarget& target);
    ~ResidentDepth();
    ResidentDepth(const ResidentDepth&) = delete;
    ResidentDepth& operator=(const ResidentDepth&) = delete;

    VkImage Image() const { return image; }
    VkImageView View() const { return view; }
    VkFormat Format() const { return format; }
    const DepthTarget& Description() const { return description; }
    bool DepthDefined() const { return depthDefined; }
    bool StencilDefined() const { return stencilDefined; }
    /// True when @p target names the same surface (addresses, extent, formats).
    bool Matches(const DepthTarget& target) const;
    /// loadOps the next draw against @p target must use (see DepthAspectLoadOp).
    DepthLoadOps LoadOps(const DepthTarget& target) const;
    /**
     * @brief Records the layout transition/barrier for a draw and updates defined-ness.
     * @param commands Command buffer the draw is recorded into (outside a render pass).
     * @param target The draw's surface description; its clear flags define the aspects.
     */
    void Begin(VkCommandBuffer commands, const DepthTarget& target);

private:
    void release() noexcept;
    Context context;
    DepthTarget description;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool depthDefined = false;
    bool stencilDefined = false;
};

}

#endif
