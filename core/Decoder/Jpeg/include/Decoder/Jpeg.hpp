// core/Decoder/Jpeg/include/Decoder/Jpeg.hpp
// Shared host-side JPEG encoder/decoder (docs/spec/image-codecs.md).
//
// Subsystem: image codecs. Pure host code: no guest ABI, no guest pointers,
// no globals, no throws. Every function is reentrant and thread-safe (all
// state lives on the caller's stack or in the returned vectors). Backed by
// stb_image / stb_image_write (3rdparty/stb, MIT or public domain).
//
// Failure policy: invalid arguments and codec failures return std::nullopt.
// PRX wrappers map that to the SCE error code the real library would return.
// We deliberately do not throw: the shared unwinder lets guest catch(...)
// swallow host exceptions (.agents/rules/cpp-style.md).

#ifndef CORE_DECODER_JPEG_INCLUDE_DECODER_JPEG_HPP
#define CORE_DECODER_JPEG_INCLUDE_DECODER_JPEG_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Decoder::Jpeg {

// Largest width or height Encode accepts (the JPEG SOF field is 16 bits).
inline constexpr std::uint32_t kMaxDimension = 0xFFFF;

// Decoded raster: tightly packed 8-bit interleaved samples, row-major.
struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;  // 1 (grayscale) or 3 (RGB) as stored in the file
    std::vector<std::uint8_t> pixels;
};

// Encodes tightly packed 8-bit pixels (width * channels bytes per row).
//
// @param pixels   at least width * height * channels bytes
// @param channels 1 (grayscale) or 3 (RGB). Note: stb always writes a
//                 3-component YCbCr stream, so grayscale input decodes as
//                 neutral RGB (R == G == B), not as a 1-channel image.
// @param quality  1..100 (stb quality scale, 100 = best)
// @return the JFIF byte stream, or nullopt when any argument is out of range
//         (zero or > kMaxDimension size, bad channels/quality, short buffer)
//         or when allocation/encoding fails.
std::optional<std::vector<std::uint8_t>> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels, int quality);

// Decodes a JPEG byte stream keeping its native channel count.
// @return nullopt for empty, oversized (> INT_MAX bytes), malformed or
//         unsupported input.
std::optional<Image> Decode(std::span<const std::uint8_t> jpeg);

}  // namespace Decoder::Jpeg

#endif  // CORE_DECODER_JPEG_INCLUDE_DECODER_JPEG_HPP
