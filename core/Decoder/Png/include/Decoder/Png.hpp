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

/**
 * @brief Validates the signature and IHDR and scans for a tRNS chunk.
 *
 * Checks size limits, the legal bit-depth/colour-type pair and the
 * compression/filter/interlace fields, then scans the chunks before IDAT for
 * tRNS. Never reads past the span. Does not verify CRCs (informational only;
 * Decode is the authority).
 *
 * @param png The PNG file bytes (may be truncated).
 * @return The parsed header, or nullopt if malformed.
 */
std::optional<Header> ParseHeader(std::span<const std::uint8_t> png);

/**
 * @brief Decodes a PNG to 8-bit RGBA.
 *
 * Interlaced (Adam7), palette (with or without tRNS), gray and 16-bit sources
 * are decoded; 16-bit samples are reduced to 8 bits. A bad CRC in a critical
 * chunk (IHDR/PLTE/IDAT/IEND) or a missing IEND is rejected; ancillary CRC
 * errors are ignored.
 *
 * @param png The complete PNG file bytes.
 * @return The decoded image, or nullopt for empty, oversized (> INT_MAX
 *         bytes) or corrupt input, or an image stb refuses to allocate
 *         (dimension cap 1<<24).
 */
std::optional<Image> Decode(std::span<const std::uint8_t> png);

/**
 * @brief Encodes tightly packed 8-bit pixels (width * channels bytes per row) to PNG.
 *
 * @param pixels   At least width * height * channels bytes.
 * @param width    Image width (width * channels must fit in int).
 * @param height   Image height.
 * @param channels 1 (gray), 2 (gray+alpha), 3 (RGB) or 4 (RGBA).
 * @return The PNG byte stream, or nullopt for bad channels, zero or
 *         overflowing size, a short pixel buffer, or allocation/encoding
 *         failure.
 */
std::optional<std::vector<std::uint8_t>> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels);

}  // namespace Decoder::Png

#endif  // CORE_DECODER_PNG_INCLUDE_DECODER_PNG_HPP
