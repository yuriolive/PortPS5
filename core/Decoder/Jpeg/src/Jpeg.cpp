// core/Decoder/Jpeg/src/Jpeg.cpp
// Shared JPEG encoder/decoder over stb (see Decoder/Jpeg.hpp for the contract).
//
// stb is compiled into this translation unit only, with *_STATIC so its
// symbols are internal; decoder_jpeg and decoder_png can therefore both be
// linked into one binary without duplicate-symbol clashes. Ported from
// AnyPS5 94c73192, adapted to drop exceptions and to bound allocation math.

#include "Decoder/Jpeg.hpp"

#include <climits>
#include <cstddef>
#include <new>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Decoder::Jpeg {

namespace {

// Collects stb's streamed output. `failed` latches an allocation failure
// because the callback is a C-style hook and must not let bad_alloc escape
// through stb's frames.
struct Sink {
    std::vector<std::uint8_t> bytes;
    bool failed = false;
};

void appendBytes(void* context, void* data, int size) {
    auto* sink = static_cast<Sink*>(context);
    if (sink->failed || size <= 0) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    try {
        sink->bytes.insert(sink->bytes.end(), bytes, bytes + size);
    } catch (const std::bad_alloc&) {
        sink->failed = true;
    }
}

}  // namespace

std::optional<std::vector<std::uint8_t>> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels, int quality) {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) return std::nullopt;
    if (channels != 1 && channels != 3) return std::nullopt;
    if (quality < 1 || quality > 100) return std::nullopt;
    // Compared in uint64 so the product can never wrap on any size_t width.
    const std::uint64_t required = static_cast<std::uint64_t>(width) * height * channels;
    if (pixels.size() < required) return std::nullopt;

    Sink sink;
    const int written = stbi_write_jpg_to_func(appendBytes, &sink, static_cast<int>(width), static_cast<int>(height),
                                               static_cast<int>(channels), pixels.data(), quality);
    if (written == 0 || sink.failed) return std::nullopt;
    return std::move(sink.bytes);
}

std::optional<Image> Decode(std::span<const std::uint8_t> jpeg) {
    if (jpeg.empty() || jpeg.size() > INT_MAX) return std::nullopt;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &width, &height, &channels, 0);
    if (!decoded) return std::nullopt;

    Image image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<std::uint32_t>(channels), {}};
    const std::size_t byteCount = static_cast<std::size_t>(image.width) * image.height * image.channels;
    try {
        image.pixels.assign(decoded, decoded + byteCount);
    } catch (const std::bad_alloc&) {
        stbi_image_free(decoded);
        return std::nullopt;
    }
    stbi_image_free(decoded);
    return image;
}

}  // namespace Decoder::Jpeg
