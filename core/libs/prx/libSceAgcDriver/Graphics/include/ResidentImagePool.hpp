// core/libs/prx/libSceAgcDriver/Graphics/include/ResidentImagePool.hpp
// Free list of destination images used when a resident render target is sampled as a texture.
// Owned by TextureCache and shared with every Texture built from a ResidentColor; a Texture returns
// its image here on destruction instead of freeing the device memory, so the next read of a target
// of the same extent and format skips vkCreateImage/vkAllocateMemory/vkBindImageMemory.
// Threading: guarded by a pool-local mutex because a Texture may be released from whichever thread
// drops the last reference. No Vulkan call is made by the pool itself; destruction goes through the
// callback supplied at construction, which keeps the logic testable without a device.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENTIMAGEPOOL_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENTIMAGEPOOL_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>

namespace AgcDriver::Graphics {

/// Identity under which an image may be reused: every property baked into the VkImage itself.
struct ResidentImageKey {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool operator==(const ResidentImageKey&) const = default;
};

/// An image and its dedicated memory, owned by whoever holds the value.
struct PooledImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize bytes = 0;
};

class ResidentImagePool {
public:
    using Destroy = std::function<void(const PooledImage&)>;

    /**
     * @brief Creates an empty pool.
     * @param destroy Frees one image and its memory; called for evicted entries and on destruction.
     * @param maxBytes Upper bound on retained device memory.
     * @param maxImages Upper bound on retained image count.
     */
    ResidentImagePool(Destroy destroy, VkDeviceSize maxBytes, std::size_t maxImages);
    ~ResidentImagePool();
    ResidentImagePool(const ResidentImagePool&) = delete;
    ResidentImagePool& operator=(const ResidentImagePool&) = delete;

    /**
     * @brief Takes a retained image with an identical key out of the pool.
     * @param key Extent and format the caller needs.
     * @return The image (ownership transfers to the caller), or nullopt on a miss.
     */
    std::optional<PooledImage> Acquire(const ResidentImageKey& key);

    /**
     * @brief Hands an image back. The caller guarantees the GPU no longer reads or writes it.
     * @param key Key the image was created under.
     * @param image The image; ownership transfers to the pool. Oldest entries are destroyed first
     *        when the byte or count bound would be exceeded; an image larger than the byte bound
     *        is destroyed immediately.
     */
    void Release(const ResidentImageKey& key, const PooledImage& image);

    /// Number of retained images.
    std::size_t Size() const;
    /// Device bytes retained.
    VkDeviceSize RetainedBytes() const;
    /// Acquire calls answered from the pool / missed since construction.
    std::uint64_t Hits() const;
    std::uint64_t Misses() const;

private:
    struct Entry {
        ResidentImageKey key;
        PooledImage image;
    };
    Destroy destroy;
    VkDeviceSize maxBytes;
    std::size_t maxImages;
    mutable std::mutex lock;
    std::list<Entry> entries;  // oldest first
    VkDeviceSize retainedBytes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

}

#endif
