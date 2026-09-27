#ifndef CORE_LIBS_PRX_LIBSCEAJM_NATIVE_SRC_AJMINTERNAL_HPP
#define CORE_LIBS_PRX_LIBSCEAJM_NATIVE_SRC_AJMINTERNAL_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "SceTypes.hpp"

// Shared by the AJM batch implementation and its unit tests (ported from
// AnyPS5 PR #5 as general mechanisms; no per-title branches).
//
// Only ATRAC9 (codec 1) has a decoder, via LibAtrac9. Jobs on any other codec
// log once at instance creation and then report a codec error per job; they
// never silently produce zeros.

constexpr int SCE_AJM_ERROR_INVALID_INSTANCE = static_cast<int>(0x80930003);
constexpr int SCE_AJM_ERROR_INVALID_PARAMETER = static_cast<int>(0x80930005);
constexpr int SCE_AJM_ERROR_OUT_OF_RESOURCES = static_cast<int>(0x80930007);

constexpr std::int32_t AJM_RESULT_NOT_INITIALIZED = 0x00000001;
constexpr std::int32_t AJM_RESULT_INVALID_DATA = 0x00000002;
constexpr std::int32_t AJM_RESULT_INVALID_PARAMETER = 0x00000004;
constexpr std::int32_t AJM_RESULT_PARTIAL_INPUT = 0x00000008;
constexpr std::int32_t AJM_RESULT_NOT_ENOUGH_ROOM = 0x00000010;
constexpr std::int32_t AJM_RESULT_CODEC_ERROR = 0x40000000;

constexpr std::uint32_t AJM_CODEC_AT9 = 1;

constexpr std::uint64_t AJM_RUN_MULTIPLE_FRAMES = 1ull << 12;
constexpr std::uint64_t AJM_SIDEBAND_GAPLESS_DECODE = 1ull << 45;
constexpr std::uint64_t AJM_SIDEBAND_FORMAT = 1ull << 46;
constexpr std::uint64_t AJM_SIDEBAND_STREAM = 1ull << 47;

enum class AjmJobKind : std::uint32_t {
    Initialize = 1,
    ClearContext = 2,
    SetGaplessDecode = 3,
    Run = 4,
    GetStatistics = 5,
};

struct AjmJobHeader {
    AjmJobKind kind;
    std::uint32_t bytes;
    std::uint32_t instance;
    std::uint32_t reserved;
    std::uint64_t flags;
    void* sideband;
    std::uint64_t sidebandSize;
    std::uint64_t parameterSize;
    std::uint32_t inputCount;
    std::uint32_t outputCount;
    std::uint8_t parameters[16];
};

inline AjmJobHeader AjmMakeHeader(AjmJobKind kind, std::uint32_t instance,
                                  void* sideband, std::uint64_t sidebandSize) {
    AjmJobHeader header{};
    header.kind = kind;
    header.instance = instance;
    header.sideband = sideband;
    header.sidebandSize = sidebandSize;
    return header;
}

// Appends one job record plus its buffer descriptors to the batch. The batch
// buffer is library-private: the guest reserves the memory and only reads the
// used-bytes field back.
inline int AjmAppend(AjmBatchInfo* info, const AjmJobHeader& header,
                     const AjmBuffer* inputs, const AjmBuffer* outputs) {
    if (!info || !info->p_buffer) return SCE_AJM_ERROR_INVALID_PARAMETER;
    const std::size_t bytes =
        sizeof(AjmJobHeader) + (header.inputCount + header.outputCount) * sizeof(AjmBuffer);
    if (bytes > info->size - info->offset) return SCE_AJM_ERROR_OUT_OF_RESOURCES;
    auto* cursor = static_cast<std::uint8_t*>(info->p_buffer) + info->offset;
    AjmJobHeader record = header;
    record.bytes = static_cast<std::uint32_t>(bytes);
    std::memcpy(cursor, &record, sizeof(record));
    cursor += sizeof(record);
    if (header.inputCount) std::memcpy(cursor, inputs, header.inputCount * sizeof(AjmBuffer));
    cursor += header.inputCount * sizeof(AjmBuffer);
    if (header.outputCount) std::memcpy(cursor, outputs, header.outputCount * sizeof(AjmBuffer));
    info->offset += bytes;
    return 0;
}

// The SDK's speaker masks for a decoded channel count.
inline std::uint32_t AjmChannelMask(std::size_t channels) {
    switch (channels) {
    case 1: return 0x4;
    case 2: return 0x3;
    case 4: return 0x33;
    case 6: return 0x3F;
    case 8: return 0x63F;
    default: return 0;
    }
}

// Titles may hand the decoder a whole .at9 file. Like the console's decoder, a
// RIFF/WAVE header at a frame boundary is skipped up to the data chunk's
// payload and counted as consumed input. Returns the payload offset, or 0 when
// the bytes do not start a complete RIFF header.
inline std::size_t AjmRiffDataOffset(const std::uint8_t* data, std::size_t size) {
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return 0;
    std::size_t cursor = 12;
    while (cursor + 8 <= size) {
        std::uint32_t chunkBytes = 0;
        std::memcpy(&chunkBytes, data + cursor + 4, sizeof(chunkBytes));
        if (std::memcmp(data + cursor, "data", 4) == 0) return cursor + 8;
        cursor += 8 + static_cast<std::size_t>(chunkBytes) + (chunkBytes & 1u);
    }
    return 0;
}

#endif
