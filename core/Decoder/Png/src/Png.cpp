// core/Decoder/Png/src/Png.cpp
// Shared PNG header parser, decoder and encoder over stb
// (see Decoder/Png.hpp for the contract).
//
// stb is compiled into this translation unit only, with *_STATIC so its
// symbols stay internal (decoder_jpeg embeds its own copy). Ported from
// AnyPS5 5853fec9, adapted to drop exceptions and to bound size arithmetic.

#include "Decoder/Png.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <new>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Decoder::Png {

namespace {

constexpr std::array<std::uint8_t, 8> SIGNATURE = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::size_t CHUNK_OVERHEAD = 12;  // length(4) + type(4) + crc(4)
constexpr std::size_t IHDR_SIZE = 13;
constexpr std::uint32_t MAX_DIMENSION = 0x7FFFFFFF;  // PNG spec: 2^31 - 1

std::uint32_t readBigEndian32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) << 24 | static_cast<std::uint32_t>(bytes[1]) << 16
        | static_cast<std::uint32_t>(bytes[2]) << 8 | static_cast<std::uint32_t>(bytes[3]);
}

bool isChunkType(const std::uint8_t* bytes, const char* type) {
    return std::equal(bytes, bytes + 4, type);
}

// Legal (bit depth, colour type) pairs from the PNG specification, table 11.1.
bool isValidFormat(std::uint8_t bitDepth, std::uint8_t colorType) {
    switch (colorType) {
    case 0:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
    case 3:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
    case 2:
    case 4:
    case 6:
        return bitDepth == 8 || bitDepth == 16;
    default:
        return false;
    }
}

// Output sink for stb's streaming writer; see Jpeg.cpp for why the
// allocation failure is latched instead of propagated.
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

std::optional<Header> ParseHeader(std::span<const std::uint8_t> png) {
    const std::size_t firstChunk = SIGNATURE.size();
    if (png.size() < firstChunk + CHUNK_OVERHEAD + IHDR_SIZE) return std::nullopt;
    if (!std::equal(SIGNATURE.begin(), SIGNATURE.end(), png.begin())) return std::nullopt;
    if (readBigEndian32(&png[firstChunk]) != IHDR_SIZE || !isChunkType(&png[firstChunk + 4], "IHDR")) return std::nullopt;

    const std::uint8_t* ihdr = &png[firstChunk + 8];
    Header header{};
    header.width = readBigEndian32(ihdr);
    header.height = readBigEndian32(ihdr + 4);
    header.bitDepth = ihdr[8];
    header.colorType = static_cast<ColorType>(ihdr[9]);
    header.interlaced = ihdr[12] == 1;
    if (header.width == 0 || header.height == 0 || header.width > MAX_DIMENSION || header.height > MAX_DIMENSION) return std::nullopt;
    if (!isValidFormat(ihdr[8], ihdr[9]) || ihdr[10] != 0 || ihdr[11] != 0 || ihdr[12] > 1) return std::nullopt;

    // Walk the ancillary chunks that may precede IDAT looking for tRNS. The
    // per-chunk bound check keeps `offset` inside the span, so the unsigned
    // subtraction in the loop condition cannot underflow.
    std::size_t offset = firstChunk + CHUNK_OVERHEAD + IHDR_SIZE;
    while (png.size() - offset >= CHUNK_OVERHEAD) {
        const std::uint32_t length = readBigEndian32(&png[offset]);
        const std::uint8_t* type = &png[offset + 4];
        if (isChunkType(type, "tRNS")) header.hasTransparency = true;
        if (isChunkType(type, "tRNS") || isChunkType(type, "IDAT") || isChunkType(type, "IEND")) break;
        if (length > png.size() - offset - CHUNK_OVERHEAD) break;
        offset += CHUNK_OVERHEAD + length;
    }
    return header;
}

std::optional<Image> Decode(std::span<const std::uint8_t> png) {
    if (png.empty() || png.size() > INT_MAX) return std::nullopt;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
    if (!decoded) return std::nullopt;

    Image image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
    const std::size_t byteCount = static_cast<std::size_t>(image.width) * image.height * 4;
    try {
        image.pixels.assign(decoded, decoded + byteCount);
    } catch (const std::bad_alloc&) {
        stbi_image_free(decoded);
        return std::nullopt;
    }
    stbi_image_free(decoded);
    return image;
}

std::optional<std::vector<std::uint8_t>> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels) {
    if (channels < 1 || channels > 4) return std::nullopt;
    // The stride is passed to stb as an int, so width * channels must fit.
    if (width == 0 || height == 0 || static_cast<std::uint64_t>(width) * channels > INT_MAX || height > INT_MAX) {
        return std::nullopt;
    }
    const std::uint64_t required = static_cast<std::uint64_t>(width) * height * channels;
    if (pixels.size() < required) return std::nullopt;

    Sink sink;
    const int stride = static_cast<int>(width * channels);
    const int written = stbi_write_png_to_func(appendBytes, &sink, static_cast<int>(width), static_cast<int>(height),
                                               static_cast<int>(channels), pixels.data(), stride);
    if (written == 0 || sink.failed) return std::nullopt;
    return std::move(sink.bytes);
}

}  // namespace Decoder::Png
