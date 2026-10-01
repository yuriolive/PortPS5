// core/libs/prx/libSceVideodec2/Export.cpp
// libSceVideodec2: the PS5 hardware video decoder API, backed by the host H.264 decoder
// (H264Decoder.hpp, FFmpeg built LGPL-only from 3rdparty/FFmpeg). Only AVC (codec type 1) is
// supported; any other codec type is rejected at config validation with SCE_VIDEODEC2_ERROR_CODEC_TYPE,
// and when the host decoder is not built every CreateDecoder fails with API_FAIL. The library never
// returns black or invented pictures.
//
// Guest structure layouts, validation order, error codes and the NV12 frame layout (256-byte pitch,
// chroma plane directly after pitch * height luma rows) follow KytyPS5 src/libs/libVideoDec2.cpp and
// videoDec2Decoder.cpp (GPL-2.0), cross-checked against shadPS4 videodec2.h (GPL-2.0-or-later); the
// export names hash to the same NIDs those projects register. shadPS4 uses a 64-byte pitch instead:
// that disagreement is recorded in docs/spec/video-fmv.md.
//
// Threading: one registry mutex guards the handle table; each decoder has its own mutex, so calls on
// different decoders run in parallel and a Delete cannot free a decoder that another thread is in.
// All exports are APS5_VABI + noexcept and never throw across the boundary.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "GuestRangeCheck.hpp"
#include "H264Decoder.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

// SCE_VIDEODEC2_ERROR_* (KytyPS5 errno table; shadPS4 agrees).
constexpr int kErrApiFail = static_cast<int>(0x811D0100u);
constexpr int kErrStructSize = static_cast<int>(0x811D0101u);
constexpr int kErrArgumentPointer = static_cast<int>(0x811D0102u);
constexpr int kErrDecoderInstance = static_cast<int>(0x811D0103u);
constexpr int kErrMemorySize = static_cast<int>(0x811D0104u);
constexpr int kErrMemoryPointer = static_cast<int>(0x811D0105u);
constexpr int kErrFrameBufferSize = static_cast<int>(0x811D0106u);
constexpr int kErrFrameBufferPointer = static_cast<int>(0x811D0107u);
constexpr int kErrAccessUnitSize = static_cast<int>(0x811D010Du);
constexpr int kErrAccessUnitPointer = static_cast<int>(0x811D010Eu);
constexpr int kErrOutputInfo = static_cast<int>(0x811D010Fu);
constexpr int kErrComputeQueue = static_cast<int>(0x811D0110u);
constexpr int kErrConfigInfo = static_cast<int>(0x811D0200u);
constexpr int kErrComputePipeId = static_cast<int>(0x811D0201u);
constexpr int kErrComputeQueueId = static_cast<int>(0x811D0202u);
constexpr int kErrResourceType = static_cast<int>(0x811D0203u);
constexpr int kErrCodecType = static_cast<int>(0x811D0204u);
constexpr int kErrInputQueueDepth = static_cast<int>(0x811D0206u);
constexpr int kErrDpbFrameCount = static_cast<int>(0x811D0209u);
constexpr int kErrFrameWidthHeight = static_cast<int>(0x811D020Au);
constexpr int kErrAccessUnit = static_cast<int>(0x811D0301u);
constexpr int kErrOversizeDecode = static_cast<int>(0x811D0302u);

constexpr std::uint32_t kResourceTypeCompute = 1;
constexpr std::uint32_t kCodecAvc = 1;
constexpr std::uint64_t kMinMemorySize = 16ull * 1024 * 1024;
constexpr std::uint32_t kPitchAlignment = 256;
constexpr std::uint32_t kMaxDimension = 8192;  // Sanity ceiling when the config leaves the maximum open (-1).
constexpr std::size_t kMaxTrackedPictures = 128;

// Guest layouts. Pointers are uint64_t so the structs are explicit-width.
struct ComputeMemoryInfo {
    std::uint64_t thisSize;
    std::uint64_t cpuGpuMemorySize;
    std::uint64_t cpuGpuMemory;
};
struct ComputeConfigInfo {
    std::uint64_t thisSize;
    std::uint16_t computePipeId;
    std::uint16_t computeQueueId;
    std::uint8_t checkMemoryType;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};
struct DecoderConfigInfo {
    std::uint64_t thisSize;
    std::uint32_t resourceType;
    std::uint32_t codecType;
    std::uint32_t profile;
    std::uint32_t maxLevel;
    std::int32_t maxFrameWidth;
    std::int32_t maxFrameHeight;
    std::int32_t maxDpbFrameCount;
    std::uint32_t decodeInputQueueDepth;
    std::uint64_t computeQueue;
    std::uint64_t cpuAffinityMask;
    std::int32_t cpuThreadPriority;
    std::uint8_t optimizeProgressiveVideo;
    std::uint8_t checkMemoryType;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
    std::uint64_t extraConfigInfo;
};
struct DecoderMemoryInfo {
    std::uint64_t thisSize;
    std::uint64_t cpuMemorySize;
    std::uint64_t cpuMemory;
    std::uint64_t gpuMemorySize;
    std::uint64_t gpuMemory;
    std::uint64_t cpuGpuMemorySize;
    std::uint64_t cpuGpuMemory;
    std::uint64_t maxFrameBufferSize;
    std::uint32_t frameBufferAlignment;
    std::uint32_t reserved0;
};
struct InputData {
    std::uint64_t thisSize;
    std::uint64_t auData;
    std::uint64_t auSize;
    std::uint64_t ptsData;
    std::uint64_t dtsData;
    std::uint64_t attachedData;
};
struct FrameBuffer {
    std::uint64_t thisSize;
    std::uint64_t frameBuffer;
    std::uint64_t frameBufferSize;
    std::uint8_t isAccepted;
    std::uint8_t pad[7];
};
struct OutputInfo {
    std::uint64_t thisSize;
    std::uint8_t isValid;
    std::uint8_t isErrorFrame;
    std::uint8_t pictureCount;
    std::uint8_t isDiscardedFrame;
    std::uint32_t codecType;
    std::uint32_t frameWidth;
    std::uint32_t framePitch;
    std::uint32_t frameHeight;
    std::uint64_t frameBuffer;
    std::uint64_t frameBufferSize;
    std::uint32_t frameFormat;
    std::uint32_t framePitchInBytes;
};
struct AvcPictureInfo {
    std::uint64_t thisSize;
    std::uint8_t isValid;
    std::uint64_t ptsData;
    std::uint64_t dtsData;
    std::uint64_t attachedData;
    std::uint8_t idrPictureFlag;
    std::uint8_t profileIdc;
    std::uint8_t levelIdc;
    std::uint32_t picWidthInMbsMinus1;
    std::uint32_t picHeightInMapUnitsMinus1;
    std::uint8_t frameMbsOnlyFlag;
    std::uint8_t frameCroppingFlag;
    std::uint32_t frameCropLeftOffset, frameCropRightOffset, frameCropTopOffset, frameCropBottomOffset;
    std::uint8_t aspectRatioInfoPresentFlag;
    std::uint8_t aspectRatioIdc;
    std::uint16_t sarWidth, sarHeight;
    std::uint8_t videoSignalTypePresentFlag, videoFormat, videoFullRangeFlag, colourDescriptionPresentFlag;
    std::uint8_t colourPrimaries, transferCharacteristics, matrixCoefficients;
    std::uint8_t timingInfoPresentFlag;
    std::uint32_t numUnitsInTick, timeScale;
    std::uint8_t fixedFrameRateFlag, bitstreamRestrictionFlag, maxDecFrameBuffering;
    std::uint8_t picStructPresentFlag, picStruct, fieldPicFlag, bottomFieldFlag;
    std::uint8_t flags[8];  // sequence/picture parameter set, AUD, end of sequence/stream, filler, timing and buffering SEI.
    std::uint8_t constraintSetFlags[6];
};
static_assert(sizeof(ComputeMemoryInfo) == 24 && sizeof(ComputeConfigInfo) == 16, "Videodec2 compute structs");
static_assert(sizeof(DecoderConfigInfo) == 72 && sizeof(DecoderMemoryInfo) == 72, "Videodec2 decoder structs");
static_assert(sizeof(InputData) == 48 && sizeof(FrameBuffer) == 32 && sizeof(OutputInfo) == 56, "Videodec2 frame structs");
static_assert(sizeof(AvcPictureInfo) == 120, "SceVideodec2AvcPictureInfo layout");
static_assert(offsetof(DecoderConfigInfo, computeQueue) == 40 && offsetof(DecoderConfigInfo, extraConfigInfo) == 64, "config offsets");
static_assert(offsetof(OutputInfo, frameBuffer) == 32 && offsetof(OutputInfo, framePitchInBytes) == 52, "output offsets");
static_assert(offsetof(AvcPictureInfo, idrPictureFlag) == 40 && offsetof(AvcPictureInfo, sarWidth) == 74, "avc info offsets");

std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

/** Output-info structs from older SDKs are 48 bytes (no frame_format / pitch_in_bytes). */
bool OutputInfoSizeValid(std::uint64_t size) { return size == sizeof(OutputInfo) || (size | 8u) == sizeof(OutputInfo); }

const void* AsPtr(std::uint64_t value) { return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(value)); }
void* AsMutPtr(std::uint64_t value) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)); }

/** Per-picture data GetPictureInfo reports, remembered by output frame buffer address. */
struct PictureMeta {
    Videodec2::Picture picture;  // Pixel data cleared; only metadata is kept.
    std::uint64_t owner = 0;
};

struct Instance {
    std::mutex lock;
    std::unique_ptr<Videodec2::H264Decoder> decoder;
    std::int32_t maxWidth = -1;
    std::int32_t maxHeight = -1;
    std::optional<Videodec2::Picture> pending;  // A popped picture that did not fit its frame buffer yet.
};

std::mutex g_registryLock;
std::map<std::uint64_t, std::shared_ptr<Instance>> g_decoders;
std::map<std::uint64_t, PictureMeta> g_pictures;  // Keyed by frame buffer address.
std::deque<std::uint64_t> g_pictureOrder;  // Insertion order, to cap g_pictures.

std::shared_ptr<Instance> Find(std::uint64_t handle) {
    std::lock_guard lock(g_registryLock);
    const auto it = g_decoders.find(handle);
    return it == g_decoders.end() ? nullptr : it->second;
}

void ForgetPictures(std::uint64_t owner) {
    std::lock_guard lock(g_registryLock);
    for (auto it = g_pictures.begin(); it != g_pictures.end();) {
        it = it->second.owner == owner ? g_pictures.erase(it) : std::next(it);
    }
    std::erase_if(g_pictureOrder, [&](std::uint64_t key) { return !g_pictures.contains(key); });
}

void RememberPicture(std::uint64_t owner, std::uint64_t frameBuffer, Videodec2::Picture picture) {
    picture.luma.clear();
    picture.luma.shrink_to_fit();
    picture.chroma.clear();
    picture.chroma.shrink_to_fit();
    std::lock_guard lock(g_registryLock);
    if (!g_pictures.contains(frameBuffer)) {
        g_pictureOrder.push_back(frameBuffer);
    }
    g_pictures[frameBuffer] = PictureMeta{std::move(picture), owner};
    while (g_pictureOrder.size() > kMaxTrackedPictures) {
        g_pictures.erase(g_pictureOrder.front());
        g_pictureOrder.pop_front();
    }
}

int ValidateDecoderConfig(const DecoderConfigInfo& c, bool requireComputeQueue) {
    if (c.resourceType != kResourceTypeCompute) return kErrResourceType;
    if (c.codecType != kCodecAvc) return kErrCodecType;
    if (c.reserved0 != 0 || c.reserved1 != 0) return kErrConfigInfo;
    if (c.decodeInputQueueDepth == 0) return kErrInputQueueDepth;
    if (c.maxDpbFrameCount < -1 || c.maxDpbFrameCount == 0) return kErrDpbFrameCount;
    if (c.maxFrameWidth < -1 || c.maxFrameHeight < -1 || c.maxFrameWidth == 0 || c.maxFrameHeight == 0) return kErrFrameWidthHeight;
    if (requireComputeQueue && c.computeQueue == 0) return kErrComputeQueue;
    return 0;
}

/** Clears the output/frame-buffer reply for "no picture produced". */
void FillNoPicture(FrameBuffer* frame, OutputInfo* out) {
    frame->isAccepted = 0;
    out->isValid = 0;
    out->isErrorFrame = 0;
    out->pictureCount = 0;
    out->isDiscardedFrame = 0;
    out->codecType = kCodecAvc;
    out->frameWidth = out->framePitch = out->frameHeight = 0;
    out->frameBuffer = frame->frameBuffer;
    out->frameBufferSize = frame->frameBufferSize;
    if (out->thisSize == sizeof(OutputInfo)) {
        out->frameFormat = 0;
        out->framePitchInBytes = 0;
    }
}

/**
 * Writes the next ready picture (a leftover one first) into the guest frame buffer as NV12 and fills
 * the output reply. Returns 0 (also when no picture is ready), or an error with the picture kept
 * for the next call when only the frame buffer was unusable.
 */
int EmitNext(Instance& inst, std::uint64_t handle, FrameBuffer* frame, OutputInfo* out) {
    if (!inst.pending) {
        Videodec2::Picture next;
        if (!inst.decoder->PopPicture(next)) {
            return 0;
        }
        inst.pending = std::move(next);
    }
    const Videodec2::Picture& pic = *inst.pending;
    if (pic.width == 0 || pic.height == 0) {
        inst.pending.reset();
        return kErrApiFail;
    }
    const std::uint32_t limitW = inst.maxWidth > 0 ? static_cast<std::uint32_t>(inst.maxWidth) : kMaxDimension;
    const std::uint32_t limitH = inst.maxHeight > 0 ? static_cast<std::uint32_t>(inst.maxHeight) : kMaxDimension;
    if (pic.width > limitW || pic.height > limitH) {
        inst.pending.reset();
        return kErrOversizeDecode;
    }
    const std::uint32_t pitch = AlignUp(pic.width, kPitchAlignment);
    const std::uint32_t chromaRows = (pic.height + 1) / 2;
    const std::uint64_t required = static_cast<std::uint64_t>(pitch) * (pic.height + chromaRows);
    if (required > frame->frameBufferSize) {
        return kErrFrameBufferSize;  // Picture stays pending; the title may retry with a larger buffer.
    }
    auto* dst = static_cast<std::uint8_t*>(AsMutPtr(frame->frameBuffer));
    if (!GuestRangeUsable(dst, static_cast<std::size_t>(required), true)) {
        return kErrFrameBufferPointer;
    }
    // Zero first so the pitch padding is deterministic, then copy luma rows and the interleaved chroma rows.
    std::memset(dst, 0, static_cast<std::size_t>(required));
    for (std::uint32_t y = 0; y < pic.height; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * pitch, pic.luma.data() + static_cast<std::size_t>(y) * pic.width, pic.width);
    }
    const std::uint32_t chromaRowBytes = (pic.width + 1) / 2 * 2;
    std::uint8_t* chroma = dst + static_cast<std::size_t>(pitch) * pic.height;
    for (std::uint32_t y = 0; y < chromaRows; ++y) {
        std::memcpy(chroma + static_cast<std::size_t>(y) * pitch, pic.chroma.data() + static_cast<std::size_t>(y) * chromaRowBytes, chromaRowBytes);
    }

    frame->isAccepted = 1;
    out->isValid = 1;
    out->isErrorFrame = pic.corrupt ? 1 : 0;
    out->pictureCount = 1;
    out->codecType = kCodecAvc;
    out->frameWidth = pic.width;
    out->framePitch = pitch;
    out->frameHeight = pic.height;
    out->frameBuffer = frame->frameBuffer;
    out->frameBufferSize = frame->frameBufferSize;
    if (out->thisSize == sizeof(OutputInfo)) {
        out->frameFormat = 0;
        out->framePitchInBytes = pitch;
    }
    RememberPicture(handle, frame->frameBuffer, std::move(*inst.pending));
    inst.pending.reset();
    return 0;
}

/** Common argument checks of Decode and Flush on the frame buffer and output reply. */
int CheckFrameAndOutput(const FrameBuffer* frame, const OutputInfo* out) {
    if (!GuestRangeUsable(frame, sizeof(FrameBuffer), true) || !GuestRangeUsable(out, sizeof(std::uint64_t), false)) {
        return kErrArgumentPointer;
    }
    if (frame->thisSize != sizeof(FrameBuffer) || !OutputInfoSizeValid(out->thisSize)) return kErrStructSize;
    // Size validated: the whole reply block is written by FillNoPicture/EmitNext.
    if (!GuestRangeUsable(out, static_cast<std::size_t>(out->thisSize), true)) return kErrArgumentPointer;
    if (frame->frameBufferSize == 0) return kErrFrameBufferSize;
    if (frame->frameBuffer == 0) return kErrFrameBufferPointer;
    return 0;
}

}  // namespace

extern "C" {

/** Reports the compute scratch size a queue needs. Returns 0, ARGUMENT_POINTER or STRUCT_SIZE. */
int APS5_VABI sceVideodec2QueryComputeMemoryInfo(ComputeMemoryInfo* info) noexcept {
    if (info == nullptr) return kErrArgumentPointer;
    if (info->thisSize != sizeof(ComputeMemoryInfo)) return kErrStructSize;
    info->cpuGpuMemorySize = kMinMemorySize;
    info->cpuGpuMemory = 0;
    return 0;
}

/**
 * Allocates a compute queue: the handle is the caller's own scratch pointer (no GPU work happens).
 * Returns 0, ARGUMENT_POINTER, STRUCT_SIZE, CONFIG_INFO, COMPUTE_PIPE_ID, COMPUTE_QUEUE_ID,
 * MEMORY_SIZE or MEMORY_POINTER.
 */
int APS5_VABI sceVideodec2AllocateComputeQueue(const ComputeConfigInfo* config, const ComputeMemoryInfo* memory, std::uint64_t* queue) noexcept {
    if (config == nullptr || memory == nullptr || queue == nullptr) return kErrArgumentPointer;
    if (config->thisSize != sizeof(ComputeConfigInfo) || memory->thisSize != sizeof(ComputeMemoryInfo)) return kErrStructSize;
    if (config->reserved0 != 0 || config->reserved1 != 0) return kErrConfigInfo;
    if (config->computePipeId > 4) return kErrComputePipeId;
    if (config->computeQueueId > 7) return kErrComputeQueueId;
    if (memory->cpuGpuMemorySize < kMinMemorySize) return kErrMemorySize;
    if (memory->cpuGpuMemory == 0) return kErrMemoryPointer;
    *queue = memory->cpuGpuMemory;
    return 0;
}

/** Releases a compute queue. Returns 0, or COMPUTE_QUEUE_ID for a null queue. */
int APS5_VABI sceVideodec2ReleaseComputeQueue(std::uint64_t queue) noexcept { return queue != 0 ? 0 : kErrComputeQueueId; }

/**
 * Reports the memory a decoder needs for `config` (fixed minimums; no real GPU memory is used).
 * Returns 0, ARGUMENT_POINTER, STRUCT_SIZE, or a config validation error (RESOURCE_TYPE, CODEC_TYPE,
 * CONFIG_INFO, INPUT_QUEUE_DEPTH, DPB_FRAME_COUNT, FRAME_WIDTH_HEIGHT).
 */
int APS5_VABI sceVideodec2QueryDecoderMemoryInfo(const DecoderConfigInfo* config, DecoderMemoryInfo* memory) noexcept {
    if (config == nullptr || memory == nullptr) return kErrArgumentPointer;
    if (config->thisSize != sizeof(DecoderConfigInfo) || memory->thisSize != sizeof(DecoderMemoryInfo)) return kErrStructSize;
    if (const int e = ValidateDecoderConfig(*config, false)) return e;
    memory->cpuMemorySize = memory->gpuMemorySize = memory->cpuGpuMemorySize = kMinMemorySize;
    memory->cpuMemory = memory->gpuMemory = memory->cpuGpuMemory = 0;
    memory->maxFrameBufferSize = kMinMemorySize;
    memory->frameBufferAlignment = 0x100;
    memory->reserved0 = 0;
    return 0;
}

/**
 * Creates an AVC decoder and returns its handle. Returns 0, the QueryDecoderMemoryInfo errors plus
 * COMPUTE_QUEUE (null queue), MEMORY_SIZE, MEMORY_POINTER, or API_FAIL when the host H.264 decoder is
 * not built or fails to open.
 */
int APS5_VABI sceVideodec2CreateDecoder(const DecoderConfigInfo* config, const DecoderMemoryInfo* memory, std::uint64_t* handle) noexcept {
    if (config == nullptr || memory == nullptr || handle == nullptr) return kErrArgumentPointer;
    if (config->thisSize != sizeof(DecoderConfigInfo) || memory->thisSize != sizeof(DecoderMemoryInfo)) return kErrStructSize;
    if (const int e = ValidateDecoderConfig(*config, true)) return e;
    if (memory->cpuMemorySize < kMinMemorySize || memory->gpuMemorySize < kMinMemorySize || memory->cpuGpuMemorySize < kMinMemorySize ||
        memory->maxFrameBufferSize < kMinMemorySize) {
        return kErrMemorySize;
    }
    if (memory->cpuMemory == 0 || memory->gpuMemory == 0 || memory->cpuGpuMemory == 0) return kErrMemoryPointer;
    auto inst = std::make_shared<Instance>();
    inst->decoder = Videodec2::H264Decoder::Create();
    if (!inst->decoder) {
        APS5_LOG_OUT("sceVideodec2CreateDecoder: no host H.264 decoder (FFmpeg not built), failing with API_FAIL%s", "");
        return kErrApiFail;
    }
    inst->maxWidth = config->maxFrameWidth;
    inst->maxHeight = config->maxFrameHeight;
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(inst.get()));
    {
        std::lock_guard lock(g_registryLock);
        g_decoders.emplace(id, std::move(inst));
    }
    *handle = id;
    return 0;
}

/** Deletes a decoder. Returns 0, or DECODER_INSTANCE for an unknown or already deleted handle. */
int APS5_VABI sceVideodec2DeleteDecoder(std::uint64_t handle) noexcept {
    std::shared_ptr<Instance> inst;
    {
        std::lock_guard lock(g_registryLock);
        const auto it = g_decoders.find(handle);
        if (it == g_decoders.end()) return kErrDecoderInstance;
        inst = std::move(it->second);
        g_decoders.erase(it);
    }
    ForgetPictures(handle);
    return 0;
}

/**
 * Decodes one Annex B access unit and, when a picture is ready (decode order may differ from output
 * order), writes the next one as NV12 into the guest frame buffer.
 * Returns 0 (output->isValid tells whether a picture was produced), DECODER_INSTANCE,
 * ARGUMENT_POINTER, STRUCT_SIZE, ACCESS_UNIT_SIZE, ACCESS_UNIT_POINTER, FRAME_BUFFER_SIZE (also when
 * the picture does not fit), FRAME_BUFFER_POINTER, ACCESS_UNIT (rejected bytes) or OVERSIZE_DECODE.
 */
int APS5_VABI sceVideodec2Decode(std::uint64_t handle, const InputData* input, FrameBuffer* frame, OutputInfo* output) noexcept {
    const auto inst = Find(handle);
    if (!inst) return kErrDecoderInstance;
    if (input == nullptr || frame == nullptr || output == nullptr) return kErrArgumentPointer;
    if (!GuestRangeUsable(input, sizeof(InputData), false)) return kErrArgumentPointer;
    if (input->thisSize != sizeof(InputData)) return kErrStructSize;
    if (const int e = CheckFrameAndOutput(frame, output)) return e;
    if (input->auSize == 0) return kErrAccessUnitSize;
    if (input->auData == 0) return kErrAccessUnitPointer;
    if (input->auSize > 0x7FFFFFFFull || !GuestRangeUsable(AsPtr(input->auData), static_cast<std::size_t>(input->auSize), false)) {
        return kErrAccessUnitPointer;
    }
    FillNoPicture(frame, output);
    std::lock_guard lock(inst->lock);
    const Videodec2::AccessUnitInfo info{input->ptsData, input->dtsData, input->attachedData};
    if (inst->decoder->Decode(static_cast<const std::uint8_t*>(AsPtr(input->auData)), static_cast<std::size_t>(input->auSize), info) !=
        Videodec2::DecodeStatus::Ok) {
        return kErrAccessUnit;
    }
    return EmitNext(*inst, handle, frame, output);
}

/**
 * Drains reordered pictures, one per call. Returns 0 (output->isValid false once none are left) or
 * the Decode errors that apply (DECODER_INSTANCE, ARGUMENT_POINTER, STRUCT_SIZE, FRAME_BUFFER_*).
 */
int APS5_VABI sceVideodec2Flush(std::uint64_t handle, FrameBuffer* frame, OutputInfo* output) noexcept {
    const auto inst = Find(handle);
    if (!inst) return kErrDecoderInstance;
    if (frame == nullptr || output == nullptr) return kErrArgumentPointer;
    if (const int e = CheckFrameAndOutput(frame, output)) return e;
    FillNoPicture(frame, output);
    std::lock_guard lock(inst->lock);
    inst->decoder->Drain();
    return EmitNext(*inst, handle, frame, output);
}

/** Resets a decoder (a seek): drops queued pictures; the next access unit must be an IDR. Returns 0 or DECODER_INSTANCE. */
int APS5_VABI sceVideodec2Reset(std::uint64_t handle) noexcept {
    const auto inst = Find(handle);
    if (!inst) return kErrDecoderInstance;
    {
        std::lock_guard lock(inst->lock);
        inst->decoder->Reset();
        inst->pending.reset();
    }
    ForgetPictures(handle);
    return 0;
}

/**
 * Fills the AVC picture info for a picture a previous Decode/Flush returned (found by its frame
 * buffer address; the title keeps the buffer until it has read the info).
 * Returns 0, ARGUMENT_POINTER, STRUCT_SIZE (first/second block size), OUTPUT_INFO (no picture, or the
 * buffer is unknown).
 */
int APS5_VABI sceVideodec2GetPictureInfo(const OutputInfo* output, void* first, void* second) noexcept {
    if (output == nullptr || first == nullptr) return kErrArgumentPointer;
    if (!OutputInfoSizeValid(output->thisSize)) return kErrStructSize;
    if (output->isValid == 0 || output->pictureCount == 0 || output->frameBuffer == 0) return kErrOutputInfo;
    PictureMeta meta;
    {
        std::lock_guard lock(g_registryLock);
        const auto it = g_pictures.find(output->frameBuffer);
        if (it == g_pictures.end() || output->codecType != kCodecAvc) return kErrOutputInfo;
        meta = it->second;
    }
    if (!GuestRangeUsable(first, sizeof(std::uint64_t), false)) return kErrArgumentPointer;
    std::uint64_t requested = 0;
    std::memcpy(&requested, first, sizeof(requested));
    // Older SDKs use a 104-byte block (the reserved tail is absent).
    if (requested != sizeof(AvcPictureInfo) && (requested | 16u) != sizeof(AvcPictureInfo)) return kErrStructSize;
    if (!GuestRangeUsable(first, static_cast<std::size_t>(requested), true)) return kErrArgumentPointer;

    const Videodec2::Picture& p = meta.picture;
    AvcPictureInfo info{};
    info.thisSize = requested;
    info.isValid = 1;
    info.ptsData = p.info.pts;
    info.dtsData = p.info.dts;
    info.attachedData = p.info.attached;
    info.idrPictureFlag = p.keyFrame ? 1 : 0;
    info.profileIdc = static_cast<std::uint8_t>(p.profile);
    info.levelIdc = static_cast<std::uint8_t>(p.level);
    info.picWidthInMbsMinus1 = (p.width + 15) / 16 - 1;
    info.picHeightInMapUnitsMinus1 = (p.height + 15) / 16 - 1;
    info.frameMbsOnlyFlag = 1;  // Interlaced streams are not reported separately.
    // The decoder returns already cropped pictures; the SPS crop is recovered from the padding up to the
    // macroblock grid. Offsets are in 4:2:0 crop units (2 luma samples) as in the SPS syntax.
    {
        const std::uint32_t gridW = (info.picWidthInMbsMinus1 + 1) * 16;
        const std::uint32_t gridH = (info.picHeightInMapUnitsMinus1 + 1) * 16;
        info.frameCropRightOffset = (gridW - p.width) / 2;
        info.frameCropBottomOffset = (gridH - p.height) / 2;
        info.frameCroppingFlag = info.frameCropRightOffset != 0 || info.frameCropBottomOffset != 0 ? 1 : 0;
    }
    info.aspectRatioInfoPresentFlag = p.sarWidth != 0 && p.sarHeight != 0 ? 1 : 0;
    info.aspectRatioIdc = info.aspectRatioInfoPresentFlag ? 255 : 0;  // 255 = Extended_SAR.
    info.sarWidth = p.sarWidth;
    info.sarHeight = p.sarHeight;
    info.videoSignalTypePresentFlag = 1;
    info.videoFormat = 5;  // Unspecified.
    info.videoFullRangeFlag = p.colorRange == 2 ? 1 : 0;
    info.colourDescriptionPresentFlag = p.colorPrimaries > 2 || p.colorTransfer > 2 || p.colorMatrix > 2 ? 1 : 0;  // 0/2 = unspecified.
    info.colourPrimaries = p.colorPrimaries;
    info.transferCharacteristics = p.colorTransfer;
    info.matrixCoefficients = p.colorMatrix;
    std::memcpy(first, &info, static_cast<std::size_t>(requested));

    if (second != nullptr) {
        // No second field is ever produced: mark the optional second block invalid.
        if (!GuestRangeUsable(second, sizeof(std::uint64_t), false)) return kErrArgumentPointer;
        std::uint64_t size = 0;
        std::memcpy(&size, second, sizeof(size));
        if (size < 40 || size > 256) return kErrStructSize;
        if (!GuestRangeUsable(second, static_cast<std::size_t>(size), true)) return kErrArgumentPointer;
        std::memset(static_cast<std::uint8_t*>(second) + sizeof(size), 0, static_cast<std::size_t>(size) - sizeof(size));
    }
    return 0;
}

/** AVC-specific alias of sceVideodec2GetPictureInfo (same NID family; HEVC pictures cannot exist here). */
int APS5_VABI sceVideodec2GetAvcPictureInfo(const OutputInfo* output, void* first, void* second) noexcept {
    return sceVideodec2GetPictureInfo(output, first, second);
}

}  // extern "C"
