// core/Decoder/Png/include/Decoder/Png.hpp
// Shared host-side PNG header parser, decoder and encoder (docs/spec/image-codecs.md).
//
// Subsystem: image codecs. Pure host code: no guest ABI, no guest pointers,
// no globals, no throws; reentrant and thread-safe. Backed by stb_image and
// stb_image_write (3rdparty/stb, MIT or public domain).
//
// Failure policy: invalid arguments and codec failures return std::nullopt;
// PRX wrappers translate that into SCE error codes.

#ifndef CORE_DECODER_PNG_INCLUDE_DECODER_PNG_HPP
#define CORE_DECODER_PNG_INCLUDE_DECODER_PNG_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Decoder::Png {

// PNG IHDR colour types (values are the on-disk codes).
enum class ColorType : std::uint8_t {
    Grayscale = 0,
    Rgb = 2,
    Palette = 3,
    GrayscaleAlpha = 4,
    Rgba = 6,
};

// Parsed IHDR plus a tRNS presence flag; no pixel data is touched.
struct Header {
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t bitDepth;
    ColorType colorType;
    bool interlaced;
    bool hasTransparency;  // a tRNS chunk precedes the first IDAT
};

// Decoded raster, always expanded to 8-bit RGBA (4 bytes/pixel, row-major).
struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> pixels;
};

// Validates the signature and IHDR (size limits, legal bit-depth/colour-type
// pair, compression/filter/interlace fields) and scans the chunks before
// IDAT for tRNS. Never reads past the span. @return nullopt if malformed.
std::optional<Header> ParseHeader(std::span<const std::uint8_t> png);

// Decodes to 8-bit RGBA (16-bit sources are reduced to 8 bits).
// @return nullopt for empty, oversized (> INT_MAX bytes), corrupt input, or
//         an image stb refuses to allocate (dimension cap 1<<24).
std::optional<Image> Decode(std::span<const std::uint8_t> png);

// Encodes tightly packed 8-bit pixels (width * channels bytes per row).
// @param channels 1 (gray), 2 (gray+alpha), 3 (RGB) or 4 (RGBA)
// @return the PNG byte stream, or nullopt for bad channels, zero/overflowing
//         size, a short pixel buffer, or allocation/encoding failure.
std::optional<std::vector<std::uint8_t>> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels);

}  // namespace Decoder::Png

#endif  // CORE_DECODER_PNG_INCLUDE_DECODER_PNG_HPP
