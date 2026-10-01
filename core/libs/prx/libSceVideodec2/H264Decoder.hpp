// core/libs/prx/libSceVideodec2/H264Decoder.hpp
// Host-side H.264 decoder used by libSceVideodec2. Wraps FFmpeg's libavcodec H.264 decoder (built
// from the pinned 3rdparty/FFmpeg submodule as LGPL-2.1+, see docs/spec/build-toolchain.md) behind
// a small interface that knows nothing about guest structures or FFmpeg headers, so the guest ABI
// code and the unit tests stay independent of libav*.
//
// Threading: an H264Decoder is not thread-safe; libSceVideodec2 serialises calls per instance.
// Lifetime: owns the codec context and every queued picture. Pictures are copied out as plain
// planar 4:2:0 byte vectors, so nothing here points into FFmpeg memory after a call returns.

#ifndef CORE_LIBS_PRX_LIBSCEVIDEODEC2_H264DECODER_HPP
#define CORE_LIBS_PRX_LIBSCEVIDEODEC2_H264DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Videodec2 {

/** @brief Caller metadata that travels with an access unit through frame reordering. */
struct AccessUnitInfo {
    std::uint64_t pts = 0;
    std::uint64_t dts = 0;
    std::uint64_t attached = 0;
};

/**
 * @brief One decoded picture, as tightly packed 8-bit 4:2:0 data.
 * @details Dimensions are the visible (already cropped) size. `luma` holds width*height bytes. `chroma` holds the interleaved Cb/Cr plane (NV12): width
 *          2 * ((width + 1) / 2) bytes per row for (height + 1) / 2 rows, so odd sizes round both up.
 */
struct Picture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;
    AccessUnitInfo info;
    bool keyFrame = false;
    bool corrupt = false;  // FFmpeg flagged the picture as damaged.
    std::uint32_t profile = 0;  // H.264 profile_idc once known, else 0.
    std::uint32_t level = 0;  // H.264 level_idc once known, else 0.
    std::uint16_t sarWidth = 0, sarHeight = 0;  // Sample aspect ratio, 0/0 when unspecified.
    std::uint8_t colorRange = 0;  // AVColorRange value (2 = full range).
    std::uint8_t colorPrimaries = 0, colorTransfer = 0, colorMatrix = 0;  // H.273 code points.
};

/** @brief Result of feeding one access unit. */
enum class DecodeStatus : std::uint8_t {
    Ok,           ///< Accepted; zero or more pictures may now be ready.
    BadAccessUnit ///< The decoder rejected the bytes (not Annex B, truncated, unsupported stream).
};

/** @brief Whether this build links a real H.264 decoder (false when FFmpeg is not built). */
bool H264DecoderAvailable();

/** @brief License string reported by the linked libavcodec, or an empty string when unavailable. */
const char* H264DecoderLicense();

class H264Decoder {
public:
    /**
     * @brief Creates a decoder.
     * @return The decoder, or null when FFmpeg is not available or the codec fails to open.
     */
    static std::unique_ptr<H264Decoder> Create();

    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    /**
     * @brief Feeds one Annex B access unit and collects the pictures it makes available.
     * @param data Access unit bytes (start-code delimited NAL units).
     * @param size Byte count; must be non-zero and fit in an int.
     * @param info Metadata returned with the picture this unit produces, after reordering.
     * @return Ok, or BadAccessUnit when the bytes are rejected.
     */
    DecodeStatus Decode(const std::uint8_t* data, std::size_t size, const AccessUnitInfo& info);

    /**
     * @brief Pops the oldest ready picture.
     * @param out Receives the picture.
     * @return False when none is ready.
     */
    bool PopPicture(Picture& out);

    /** @brief Marks end of stream so reordered pictures become ready; call PopPicture to drain them. */
    void Drain();

    /** @brief Drops queued pictures and decoder state (a seek): the next access unit must be an IDR. */
    void Reset();

private:
    struct Impl;
    explicit H264Decoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace Videodec2

#endif
