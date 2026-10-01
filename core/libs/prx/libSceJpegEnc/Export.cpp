// core/libs/prx/libSceJpegEnc/Export.cpp
// libSceJpegEnc: PS5 JPEG encoder library on the shared host encoder
// (core/Decoder/Jpeg, docs/spec/image-codecs.md).
//
// Subsystem: image codecs. Lifecycle: sceJpegEncCreate places a small handle
// header inside the guest-supplied work memory (no host allocation, so a
// title that leaks the work buffer leaks nothing on the host);
// sceJpegEncDelete only invalidates it. Threading: stateless apart from the
// handle header; concurrent encodes on one handle only read it.
//
// ABI: every export is APS5_VABI + noexcept. Error policy: argument errors
// return SCE error codes; states we cannot honour (MJPEG mode, restart
// intervals, host out-of-memory, encoder failure after validation) go
// through Unsupported() instead of throwing, because the shared unwinder lets
// guest catch(...) swallow host exceptions.
//
// Guest pointers are untrusted: null/alignment/size arithmetic are validated
// and every guest range the host reads or writes (param structs, handle
// header, work memory, the pixel buffer, the output JPEG and output_info) goes
// through GuestMemoryValidation first. An unreadable or unwritable range
// returns SCE_JPEG_ENC_ERROR_INVALID_ADDR (INVALID_HANDLE for a handle that
// does not point at readable guest memory) instead of faulting in the host.
//
// Ported from AnyPS5 44208261, 16290424, 98a5228c, aa56049f, aad41aa9,
// 6ef4ae3c and f9f02e37 (see the PR description for the per-commit review).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <vector>
#include "Decoder/Jpeg.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/GuestMemoryValidation.hpp"

// Guest-visible layouts; a size change here silently breaks titles.
static_assert(sizeof(JpegEncCreateParam) == 0x8);
static_assert(sizeof(JpegEncEncodeParam) == 0x30);
static_assert(sizeof(JpegEncOutputInfo) == 0x8);

namespace {

constexpr std::int32_t SCE_JPEG_ENC_ERROR_INVALID_ADDR = static_cast<std::int32_t>(0x80650101);
constexpr std::int32_t SCE_JPEG_ENC_ERROR_INVALID_SIZE = static_cast<std::int32_t>(0x80650102);
constexpr std::int32_t SCE_JPEG_ENC_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x80650103);
constexpr std::int32_t SCE_JPEG_ENC_ERROR_INVALID_HANDLE = static_cast<std::int32_t>(0x80650104);

constexpr std::uint32_t ATTRIBUTE_NONE = 0;
constexpr std::uint32_t MEMORY_SIZE = 0x800;        // work memory the title must provide
constexpr std::uintptr_t HANDLE_ALIGNMENT = 0x20;   // handle is carved out at a 32-byte boundary

constexpr std::uint16_t PIXEL_FORMAT_R8G8B8A8 = 0;
constexpr std::uint16_t PIXEL_FORMAT_B8G8R8A8 = 1;
constexpr std::uint16_t PIXEL_FORMAT_Y8U8Y8V8 = 10;
constexpr std::uint16_t PIXEL_FORMAT_Y8 = 11;

constexpr std::uint16_t ENCODE_MODE_NORMAL = 0;
constexpr std::uint16_t ENCODE_MODE_MJPEG = 1;

constexpr std::uint16_t COLOR_SPACE_YCC = 1;
constexpr std::uint16_t COLOR_SPACE_GRAYSCALE = 2;

constexpr std::uint8_t SAMPLING_TYPE_FULL = 0;
constexpr std::uint8_t SAMPLING_TYPE_422 = 1;
constexpr std::uint8_t SAMPLING_TYPE_420 = 2;

constexpr std::uint32_t MAX_IMAGE_DIMENSION = 0xFFFF;
constexpr std::uint32_t MAX_IMAGE_PITCH = 0xFFFFFFF;
constexpr std::uint64_t MAX_IMAGE_SIZE = 0x7FFFFFFF;
// stb's JPEG writer subsamples chroma (4:2:0) only at quality <= 90 and is
// 4:4:4 above that; it offers no sampling selector, so a subsampled request
// caps quality at 90 to get the matching chroma layout.
constexpr int SUBSAMPLED_MAX_QUALITY = 90;

// Handle header living in guest work memory. `self` is a tag: it equals the
// header's own address while the handle is live and is cleared by Delete, so
// stale, forged and misaligned handles are rejected.
struct Encoder {
    Encoder* self;
};

// True when the host may read/write the whole guest range (see GuestMemoryValidation.hpp).
bool guestReadable(const void* pointer, std::size_t bytes) {
    return GuestMemoryValidation::CheckReadable(pointer, bytes) == GuestMemoryValidation::Status::Ok;
}
bool guestWritable(const void* pointer, std::size_t bytes) {
    return GuestMemoryValidation::CheckWritable(pointer, bytes) == GuestMemoryValidation::Status::Ok;
}

std::int32_t validateCreateParam(const JpegEncCreateParam* param) {
    if (!param) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (!guestReadable(param, sizeof(JpegEncCreateParam))) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (param->size != sizeof(JpegEncCreateParam)) return SCE_JPEG_ENC_ERROR_INVALID_SIZE;
    if (param->attr != ATTRIBUTE_NONE) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    return 0;
}

Encoder* toEncoder(void* handle) {
    const auto address = reinterpret_cast<std::uintptr_t>(handle);
    if (address == 0 || address % HANDLE_ALIGNMENT != 0) return nullptr;
    // A forged handle must not make the host fault while reading the tag.
    if (!guestReadable(handle, sizeof(Encoder))) return nullptr;
    auto* encoder = reinterpret_cast<Encoder*>(handle);
    return encoder->self == encoder ? encoder : nullptr;
}

// Validation order mirrors the library: addresses, then sizes, then params.
// Every product is computed in uint64 so a hostile pitch*height cannot wrap.
std::int32_t validateEncodeParam(const JpegEncEncodeParam* param) {
    if (!param) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    const bool grayscaleInput = param->pixel_format == PIXEL_FORMAT_Y8;
    if (!param->image) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (!grayscaleInput && reinterpret_cast<std::uintptr_t>(param->image) % 4 != 0) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (!param->jpeg) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;

    if (param->image_size == 0 || param->jpeg_size == 0) return SCE_JPEG_ENC_ERROR_INVALID_SIZE;

    if (param->image_width == 0 || param->image_height == 0) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (param->image_width > MAX_IMAGE_DIMENSION || param->image_height > MAX_IMAGE_DIMENSION) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (param->image_pitch == 0 || param->image_pitch > MAX_IMAGE_PITCH) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (!grayscaleInput && param->image_pitch % 4 != 0) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    const std::uint64_t requiredSize = static_cast<std::uint64_t>(param->image_height) * param->image_pitch;
    if (requiredSize > MAX_IMAGE_SIZE || requiredSize > param->image_size) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (param->encode_mode != ENCODE_MODE_NORMAL && param->encode_mode != ENCODE_MODE_MJPEG) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (param->color_space != COLOR_SPACE_YCC && param->color_space != COLOR_SPACE_GRAYSCALE) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    if (param->sampling_type != SAMPLING_TYPE_FULL && param->sampling_type != SAMPLING_TYPE_422 && param->sampling_type != SAMPLING_TYPE_420) {
        return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    if (param->restart_interval > static_cast<std::int32_t>(MAX_IMAGE_DIMENSION)) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;

    switch (param->pixel_format) {
    case PIXEL_FORMAT_R8G8B8A8:
    case PIXEL_FORMAT_B8G8R8A8:
        if (param->image_pitch / 4 < param->image_width) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        if (param->color_space != COLOR_SPACE_YCC || param->sampling_type == SAMPLING_TYPE_FULL) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        return 0;
    case PIXEL_FORMAT_Y8U8Y8V8:
        // Packed 4:2:2: two pixels per 4 bytes, odd widths round up.
        if (param->image_pitch / 2 < ((param->image_width + 1) & ~1u)) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        if (param->color_space != COLOR_SPACE_YCC || param->sampling_type == SAMPLING_TYPE_FULL) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        return 0;
    case PIXEL_FORMAT_Y8:
        if (param->image_pitch < param->image_width) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        if (param->color_space != COLOR_SPACE_GRAYSCALE || param->sampling_type != SAMPLING_TYPE_FULL) return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
        return 0;
    default:
        return SCE_JPEG_ENC_ERROR_INVALID_PARAM;
    }
}

// Bytes of the guest image the packing loop reads: (height-1) pitches plus the
// last row's payload. validateEncodeParam already proved this is <= image_size
// and that the arithmetic fits in uint64 (height <= 0xFFFF, pitch <= 0xFFFFFFF).
std::uint64_t imageReadExtent(const JpegEncEncodeParam& param) {
    std::uint64_t rowBytes = param.image_width;
    if (param.pixel_format == PIXEL_FORMAT_R8G8B8A8 || param.pixel_format == PIXEL_FORMAT_B8G8R8A8) rowBytes *= 4;
    else if (param.pixel_format == PIXEL_FORMAT_Y8U8Y8V8) rowBytes = ((rowBytes + 1) & ~std::uint64_t{1}) * 2;
    return static_cast<std::uint64_t>(param.image_height - 1) * param.image_pitch + rowBytes;
}

std::uint8_t clampToByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value + 0.5f, 0.0f, 255.0f));
}

// Full-range BT.601 YCbCr -> RGB (the JFIF convention).
void yuvToRgb(std::uint8_t y, std::uint8_t u, std::uint8_t v, std::uint8_t* rgb) {
    const float cb = static_cast<float>(u) - 128.0f;
    const float cr = static_cast<float>(v) - 128.0f;
    rgb[0] = clampToByte(y + 1.402f * cr);
    rgb[1] = clampToByte(y - 0.344136f * cb - 0.714136f * cr);
    rgb[2] = clampToByte(y + 1.772f * cb);
}

// Repacks the guest image (pitch-strided, any supported pixel format) into
// tightly packed 1- or 3-channel samples for the shared encoder. The guest
// buffer is only read inside [image, image + (height-1)*pitch + rowBytes),
// which validateEncodeParam bounded by image_size. Returns nullopt only on
// host allocation failure.
std::optional<std::vector<std::uint8_t>> toPackedPixels(const JpegEncEncodeParam& param, std::uint32_t channels) {
    const auto* image = static_cast<const std::uint8_t*>(param.image);
    const std::size_t width = param.image_width;
    std::vector<std::uint8_t> pixels;
    try {
        pixels.resize(width * param.image_height * channels);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    for (std::size_t y = 0; y < param.image_height; ++y) {
        const std::uint8_t* row = image + y * param.image_pitch;
        std::uint8_t* out = pixels.data() + y * width * channels;
        for (std::size_t x = 0; x < width; ++x) {
            switch (param.pixel_format) {
            case PIXEL_FORMAT_R8G8B8A8:
                out[x * 3 + 0] = row[x * 4 + 0];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 2];
                break;
            case PIXEL_FORMAT_B8G8R8A8:
                out[x * 3 + 0] = row[x * 4 + 2];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 0];
                break;
            case PIXEL_FORMAT_Y8U8Y8V8: {
                const std::uint8_t* pair = row + (x / 2) * 4;
                yuvToRgb(pair[(x % 2) * 2], pair[1], pair[3], out + x * 3);
                break;
            }
            default:
                out[x] = row[x];
                break;
            }
        }
    }
    return pixels;
}

// compression_ratio 0..255 -> stb quality 100..1 (linear approximation of the
// console's quantiser scale; not bit-exact, see the spec's Open questions).
int toQuality(const JpegEncEncodeParam& param) {
    int quality = 100 - param.compression_ratio * 99 / 255;
    if (param.sampling_type != SAMPLING_TYPE_FULL) quality = std::min(quality, SUBSAMPLED_MAX_QUALITY);
    return quality;
}

}  // namespace

extern "C" {

// Creates an encoder in guest work memory (>= 0x800 bytes). Returns 0; INVALID_ADDR
// (null param/memory/handle), INVALID_SIZE (bad param size or short memory) or
// INVALID_PARAM (non-zero attr). *handle is written only on success.
int32_t APS5_VABI sceJpegEncCreate(const JpegEncCreateParam* param, void* memory, uint32_t memory_size, void** handle) noexcept {
    const std::int32_t result = validateCreateParam(param);
    if (result != 0) return result;
    if (!memory) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (memory_size < MEMORY_SIZE) return SCE_JPEG_ENC_ERROR_INVALID_SIZE;
    if (!handle) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    // The header is written inside the first MEMORY_SIZE bytes (alignment padding included).
    if (!guestWritable(memory, MEMORY_SIZE) || !guestWritable(handle, sizeof(void*))) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    // The work area is exactly MEMORY_SIZE; aligning up to 32 bytes never
    // pushes the 8-byte header outside it for any size >= MEMORY_SIZE.
    const auto address = reinterpret_cast<std::uintptr_t>(memory);
    const std::uintptr_t aligned = (address + HANDLE_ALIGNMENT - 1) & ~(HANDLE_ALIGNMENT - 1);
    auto* encoder = new (reinterpret_cast<void*>(aligned)) Encoder{};
    encoder->self = encoder;
    *handle = encoder;
    return 0;
}

// Invalidates a handle from sceJpegEncCreate. Returns 0, or INVALID_HANDLE for
// null, misaligned, forged or already-deleted handles.
int32_t APS5_VABI sceJpegEncDelete(void* handle) noexcept {
    Encoder* encoder = toEncoder(handle);
    if (!encoder || !guestWritable(handle, sizeof(Encoder))) return SCE_JPEG_ENC_ERROR_INVALID_HANDLE;
    encoder->self = nullptr;  // invalidates the tag so a second Delete fails
    return 0;
}

// Encodes one image into param->jpeg and reports size/height in output_info
// (optional). Returns 0, INVALID_HANDLE, INVALID_ADDR, INVALID_SIZE (zero sizes or
// output buffer too small) or INVALID_PARAM. MJPEG mode and restart intervals abort.
int32_t APS5_VABI sceJpegEncEncode(void* handle, const JpegEncEncodeParam* param, JpegEncOutputInfo* output_info) noexcept {
    if (!toEncoder(handle)) return SCE_JPEG_ENC_ERROR_INVALID_HANDLE;
    if (param && !guestReadable(param, sizeof(JpegEncEncodeParam))) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    const std::int32_t result = validateEncodeParam(param);
    if (result != 0) return result;
    // Argument arithmetic is now sound, so the extents below cannot wrap.
    if (!guestReadable(param->image, static_cast<std::size_t>(imageReadExtent(*param)))) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    if (output_info && !guestWritable(output_info, sizeof(JpegEncOutputInfo))) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    // Truly unsupported modes: abort loudly rather than emit a wrong stream.
    if (param->encode_mode == ENCODE_MODE_MJPEG) Unsupported("sceJpegEncEncode: MJPEG encode mode is not implemented");
    if (param->restart_interval > 0) Unsupported("sceJpegEncEncode: restart interval is not implemented");

    const std::uint32_t channels = param->pixel_format == PIXEL_FORMAT_Y8 ? 1 : 3;
    const std::optional<std::vector<std::uint8_t>> pixels = toPackedPixels(*param, channels);
    if (!pixels) Unsupported("sceJpegEncEncode: host out of memory repacking the image");
    const std::optional<std::vector<std::uint8_t>> jpeg =
        Decoder::Jpeg::Encode(*pixels, param->image_width, param->image_height, channels, toQuality(*param));
    // Arguments were validated, so only allocation/encoder failure lands here.
    if (!jpeg) Unsupported("sceJpegEncEncode: shared JPEG encoder failed");
    if (jpeg->size() > param->jpeg_size) return SCE_JPEG_ENC_ERROR_INVALID_SIZE;

    // Only the bytes actually produced must be writable; jpeg_size is the title's capacity claim.
    if (!guestWritable(param->jpeg, jpeg->size())) return SCE_JPEG_ENC_ERROR_INVALID_ADDR;
    std::memcpy(param->jpeg, jpeg->data(), jpeg->size());
    if (output_info) {
        output_info->size = static_cast<std::uint32_t>(jpeg->size());
        output_info->height = param->image_height;
    }
    return 0;
}

// Returns the work-memory size (0x800) Create needs, or the same error codes
// Create gives for a bad param (INVALID_ADDR / INVALID_SIZE / INVALID_PARAM).
int32_t APS5_VABI sceJpegEncQueryMemorySize(const JpegEncCreateParam* param) noexcept {
    const std::int32_t result = validateCreateParam(param);
    if (result != 0) return result;
    return static_cast<std::int32_t>(MEMORY_SIZE);
}

}
