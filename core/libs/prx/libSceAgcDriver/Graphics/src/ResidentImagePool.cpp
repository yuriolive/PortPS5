// Free list for render-target sampling copies (see ResidentImagePool.hpp).
// Pool-local mutex only; destruction of images always happens outside the lock via the callback.
#include "prx/libSceAgcDriver/Graphics/include/ResidentImagePool.hpp"
#include <iterator>
#include <utility>
#include <list>

namespace AgcDriver::Graphics {

/**
 * Stores the destroy callback and bounds; no device object is touched until an image is
 * released into or evicted from the pool.
 */
ResidentImagePool::ResidentImagePool(Destroy destroy, VkDeviceSize maxBytes, std::size_t maxImages) : destroy(std::move(destroy)), maxBytes(maxBytes), maxImages(maxImages) {}

/**
 * Destroys every image still retained. The owner guarantees the device is alive and the GPU
 * is done with pooled images (they were idle when released).
 */
ResidentImagePool::~ResidentImagePool() {
    for (const auto& entry : entries) destroy(entry.image);
}

/**
 * Most recently released match wins: it is the likeliest to still be resident in the device's
 * caches, and the oldest entries stay first in line for eviction.
 */
std::optional<PooledImage> ResidentImagePool::Acquire(const ResidentImageKey& key) {
    std::lock_guard guard(lock);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (!(it->key == key)) continue;
        const auto image = it->image;
        retainedBytes -= image.bytes;
        entries.erase(std::next(it).base());
        ++hits;
        return image;
    }
    ++misses;
    return std::nullopt;
}

/**
 * Never throws: Texture::release() is noexcept and calls this from a destructor. The only
 * allocation (the list node for the incoming image) happens first and outside the lock; if it
 * fails the image is destroyed instead of retained, so ownership is never lost. Eviction
 * moves existing nodes with splice, which does not allocate, and the evicted images are
 * destroyed after the lock is released so a slow vkFreeMemory never blocks Acquire.
 */
void ResidentImagePool::Release(const ResidentImageKey& key, const PooledImage& image) noexcept {
    std::list<Entry> incoming;
    if (image.bytes <= maxBytes) {
        try {
            incoming.push_back({key, image});
        } catch (...) {
            incoming.clear();
        }
    }
    if (incoming.empty()) {
        destroy(image);
        return;
    }
    std::list<Entry> victims;
    {
        std::lock_guard guard(lock);
        entries.splice(entries.end(), incoming);
        retainedBytes += image.bytes;
        while (retainedBytes > maxBytes || entries.size() > maxImages) {
            retainedBytes -= entries.front().image.bytes;
            victims.splice(victims.end(), entries, entries.begin());
        }
    }
    for (const auto& victim : victims) destroy(victim.image);
}

/** Statistics accessors take the pool lock so they are safe from any thread. */
std::size_t ResidentImagePool::Size() const {
    std::lock_guard guard(lock);
    return entries.size();
}

VkDeviceSize ResidentImagePool::RetainedBytes() const {
    std::lock_guard guard(lock);
    return retainedBytes;
}

std::uint64_t ResidentImagePool::Hits() const {
    std::lock_guard guard(lock);
    return hits;
}

std::uint64_t ResidentImagePool::Misses() const {
    std::lock_guard guard(lock);
    return misses;
}

}
