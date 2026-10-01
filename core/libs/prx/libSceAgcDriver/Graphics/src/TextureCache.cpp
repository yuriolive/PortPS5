// core/libs/prx/libSceAgcDriver/Graphics/src/TextureCache.cpp
//
// Subsystem: AGC driver Graphics. Owns host images keyed by (guest address, T# words) and revalidates them
// against guest memory (whole-snapshot compare, or render-target generation) on every lookup.
// Threading: called under the GPU mutex from the draw prepare path; not internally synchronised.
// Spec: docs/spec/gpu-driver.md "Per-draw CPU cost".
#include "prx/libSceAgcDriver/Graphics/include/TextureCache.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BytesEqual.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/RenderCache.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureFormat.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace AgcDriver::Graphics {

TextureCache::TextureCache(const Context& context) : context(context) {
    Require(context.detiler != nullptr, "texture cache requires a device detiler");
}

void TextureCache::trim() {
    for (auto it = entries.begin(); it != entries.end() && (retainedBytes > budget || entries.size() > 64);) {
        if (it->texture.use_count() != 1) {
            ++it;
            continue;
        }
        retainedBytes -= it->snapshot.size() + it->texture->AllocationBytes();
        it = entries.erase(it);
    }
}

std::shared_ptr<Texture> TextureCache::Get(std::span<const std::uint32_t> words, const GuestTextureResource& resource, VkComponentMapping components) {
    Require(words.size() == 8, "texture cache descriptor must contain eight DWORDs");
    trim();
    std::array<std::uint32_t, 8> key;
    std::copy(words.begin(), words.end(), key.begin());
    auto source = context.renderCache ? context.renderCache->Find(resource.baseAddress) : nullptr;
    if (source) {
        const auto& color = source->Description();
        const auto compatibleTiling = (color.tileMode == ColorTileMode::RenderTarget && resource.tileMode == TextureTileMode::RenderTarget64KB) || (color.tileMode == ColorTileMode::Linear && resource.tileMode == TextureTileMode::kLinear);
        if (!compatibleTiling || resource.width != color.extent.width || resource.height != color.extent.height || resource.dimension != TextureDimension::k2D || resource.mipCount != 1 || resource.baseLevel != 0 || resource.baseArray != 0 || IsBlockCompressed(resource.format) || BytesPerElement(resource.format) != 4) source.reset();
    }
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->descriptor != key) continue;
        if (source || it->generation != 0) {
            if (source && it->source.lock() == source && it->generation == source->Generation()) {
                auto result = it->texture;
                entries.splice(entries.end(), entries, it);
                return result;
            }
            retainedBytes -= it->snapshot.size() + it->texture->AllocationBytes();
            entries.erase(it);
            break;
        }
        GuestMemory::CheckRange(reinterpret_cast<const void*>(resource.baseAddress), it->snapshot.size(), 1);
        // Whole-texture revalidation on every lookup: BytesEqual is memcmp==0 semantics at SSE2 speed.
        if (BytesEqual(reinterpret_cast<const void*>(resource.baseAddress), it->snapshot.data(), it->snapshot.size())) {
            auto result = it->texture;
            entries.splice(entries.end(), entries, it);
            return result;
        }
        retainedBytes -= it->snapshot.size() + it->texture->AllocationBytes();
        entries.erase(it);
        break;
    }
    if (source) {
        auto texture = std::make_shared<Texture>(context, source, resource, components);
        entries.push_back({key, {}, texture, source, source->Generation()});
        retainedBytes += texture->AllocationBytes();
        trim();
        return texture;
    }
    const auto mips = ComputeMipLayout(resource.tileMode, resource.format, resource.width, resource.height, resource.mipCount);
    const auto layers = resource.dimension == TextureDimension::k2DArray || resource.dimension == TextureDimension::kCube ? resource.depthOrLastArray + 1u : 1u;
    const auto bytes = ComputeSurfaceSize(mips, layers);
    Require(bytes != 0 && bytes <= std::numeric_limits<std::size_t>::max(), "texture cache surface size overflow");
    std::vector<std::byte> snapshot(static_cast<std::size_t>(bytes));
    GuestMemory::Read(resource.baseAddress, snapshot, 1);
    auto texture = std::make_shared<Texture>(context, *context.detiler, resource, components, snapshot);
    const auto retained = snapshot.size() + texture->AllocationBytes();
    entries.push_back({key, std::move(snapshot), texture});
    retainedBytes += retained;
    trim();
    return texture;
}

}
