/**
 * @file ResidentImagePool.cpp
 * @brief GoogleTest coverage for the render-target sampling image pool
 *        (Graphics/src/ResidentImagePool.cpp).
 *
 * Hermetic and host-only: images are fake handles and "destruction" is a recording callback, so
 * no Vulkan device, guest memory or game data is involved. The invariant under test is that a
 * read of a resident render target reuses a retained image of identical extent and format
 * (no new allocation) and never one of a different shape, and that retention is bounded.
 */
#include "prx/libSceAgcDriver/Graphics/include/ResidentImagePool.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

PooledImage fake(std::uintptr_t id, VkDeviceSize bytes) {
    return {reinterpret_cast<VkImage>(id), reinterpret_cast<VkDeviceMemory>(id), bytes};
}

class ResidentImagePoolTest : public ::testing::Test {
protected:
    std::vector<VkDeviceSize> destroyedBytes;
    std::vector<std::uintptr_t> destroyedIds;
    ResidentImagePool::Destroy recorder() {
        return [this](const PooledImage& image) {
            destroyedBytes.push_back(image.bytes);
            destroyedIds.push_back(reinterpret_cast<std::uintptr_t>(image.image));
        };
    }
    static constexpr ResidentImageKey k1080{1920, 1080, VK_FORMAT_R8G8B8A8_UNORM};
};

// A released image is handed back for the identical key (this is the allocation the pool removes),
// and is gone from the pool afterwards so two live Textures can never share one VkImage.
TEST_F(ResidentImagePoolTest, ReusesImageForIdenticalKey) {
    ResidentImagePool pool(recorder(), 1u << 30, 16);
    EXPECT_FALSE(pool.Acquire(k1080).has_value());
    pool.Release(k1080, fake(1, 100));
    const auto first = pool.Acquire(k1080);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->image, reinterpret_cast<VkImage>(std::uintptr_t{1}));
    EXPECT_FALSE(pool.Acquire(k1080).has_value());
    EXPECT_EQ(pool.Hits(), 1u);
    EXPECT_EQ(pool.Misses(), 2u);
    EXPECT_TRUE(destroyedIds.empty());
}

// Extent and format are baked into the VkImage, so any difference must miss.
TEST_F(ResidentImagePoolTest, DifferentExtentOrFormatMisses) {
    ResidentImagePool pool(recorder(), 1u << 30, 16);
    pool.Release(k1080, fake(1, 100));
    EXPECT_FALSE(pool.Acquire({1280, 1080, VK_FORMAT_R8G8B8A8_UNORM}).has_value());
    EXPECT_FALSE(pool.Acquire({1920, 720, VK_FORMAT_R8G8B8A8_UNORM}).has_value());
    EXPECT_FALSE(pool.Acquire({1920, 1080, VK_FORMAT_B8G8R8A8_UNORM}).has_value());
    EXPECT_EQ(pool.Size(), 1u);
}

// The newest matching image is preferred and the oldest is evicted first when the count bound is hit.
TEST_F(ResidentImagePoolTest, EvictsOldestBeyondCountBound) {
    ResidentImagePool pool(recorder(), 1u << 30, 2);
    pool.Release(k1080, fake(1, 10));
    pool.Release(k1080, fake(2, 10));
    pool.Release(k1080, fake(3, 10));
    EXPECT_EQ(destroyedIds, (std::vector<std::uintptr_t>{1}));
    const auto next = pool.Acquire(k1080);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->image, reinterpret_cast<VkImage>(std::uintptr_t{3}));
}

// Retained device memory is bounded; an image larger than the bound is never retained.
TEST_F(ResidentImagePoolTest, ByteBoundEvictsAndRejectsOversized) {
    ResidentImagePool pool(recorder(), 100, 16);
    pool.Release(k1080, fake(1, 60));
    pool.Release(k1080, fake(2, 60));
    EXPECT_EQ(pool.RetainedBytes(), 60u);
    EXPECT_EQ(destroyedIds, (std::vector<std::uintptr_t>{1}));
    pool.Release(k1080, fake(3, 101));
    EXPECT_EQ(destroyedIds.back(), 3u);
    EXPECT_EQ(pool.RetainedBytes(), 60u);
}

// Destroying the pool frees what it still retains, so no device memory leaks at shutdown.
TEST_F(ResidentImagePoolTest, DestructorFreesRetainedImages) {
    {
        ResidentImagePool pool(recorder(), 1u << 30, 16);
        pool.Release(k1080, fake(1, 10));
        pool.Release({64, 64, VK_FORMAT_R8G8B8A8_UNORM}, fake(2, 10));
    }
    EXPECT_EQ(destroyedIds.size(), 2u);
}

}
