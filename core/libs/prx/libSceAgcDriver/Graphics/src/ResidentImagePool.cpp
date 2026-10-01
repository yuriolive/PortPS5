// Free list for render-target sampling copies (see ResidentImagePool.hpp).
#include "prx/libSceAgcDriver/Graphics/include/ResidentImagePool.hpp"
#include <iterator>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

ResidentImagePool::ResidentImagePool(Destroy destroy, VkDeviceSize maxBytes, std::size_t maxImages) : destroy(std::move(destroy)), maxBytes(maxBytes), maxImages(maxImages) {}

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
 * Victims are collected under the lock and destroyed after it is released, so a slow
 * vkFreeMemory never blocks a concurrent Acquire.
 */
void ResidentImagePool::Release(const ResidentImageKey& key, const PooledImage& image) {
    std::vector<PooledImage> victims;
    {
        std::lock_guard guard(lock);
        if (image.bytes > maxBytes) {
            victims.push_back(image);
        } else {
            entries.push_back({key, image});
            retainedBytes += image.bytes;
            while (retainedBytes > maxBytes || entries.size() > maxImages) {
                victims.push_back(entries.front().image);
                retainedBytes -= entries.front().image.bytes;
                entries.pop_front();
            }
        }
    }
    for (const auto& victim : victims) destroy(victim);
}

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
