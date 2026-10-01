// core/libs/prx/libSceAgcDriver/Graphics/include/TextureCache.hpp
//
// Subsystem: AGC driver Graphics. Cache of host images keyed by (guest address, T# words); see TextureCache.cpp.
// Threading: not internally synchronised; callers hold the GPU mutex. Spec: docs/spec/gpu-driver.md.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_TEXTURECACHE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_TEXTURECACHE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include <array>
#include <list>
#include <memory>
#include <vector>

namespace AgcDriver::Graphics {

class TextureCache {
public:
    explicit TextureCache(const Context& context);
    std::shared_ptr<Texture> Get(std::span<const std::uint32_t> words, const GuestTextureResource& resource, VkComponentMapping components);

private:
    struct Entry {
        std::array<std::uint32_t, 8> descriptor;
        std::vector<std::byte> snapshot;
        std::shared_ptr<Texture> texture;
        std::weak_ptr<ResidentColor> source;
        std::uint64_t generation = 0;
        // Write-tracker generation the snapshot was taken at (0 = unknown: always compare bytes).
        std::uint64_t guestGeneration = 0;
    };
    void trim();
    Context context;
    std::list<Entry> entries;
    std::uint64_t retainedBytes = 0;
    static constexpr std::uint64_t budget = 256ull * 1024 * 1024;
};

}

#endif
