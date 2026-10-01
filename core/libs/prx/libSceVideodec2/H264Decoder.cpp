// core/libs/prx/libSceVideodec2/H264Decoder.cpp
// FFmpeg-backed implementation of Videodec2::H264Decoder. Compiled with APS5_HAVE_FFMPEG=1 when the
// pinned LGPL FFmpeg build is linked; otherwise only the "unavailable" stubs exist, and Create()
// returns null so libSceVideodec2 reports an error instead of fabricating pictures.
//
// The codec runs single-threaded (FFmpeg is configured without threading) and receives Annex B
// access units. Caller metadata rides on AVPacket::opaque_ref (AV_CODEC_FLAG_COPY_OPAQUE), which
// FFmpeg copies to the output frame even when B-frames reorder the pictures.

#include "H264Decoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>

#if APS5_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace Videodec2 {

#if APS5_HAVE_FFMPEG

namespace {

struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

/** Logs an FFmpeg error code with its message; failures here must never be silent (no-title-hacks rule). */
void LogAvError(const char* what, int code) {
    char err[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, err, sizeof(err));
    std::fprintf(stderr, "[VIDEODEC2] %s failed: %s\n", what, err);
}

/** Copies `rows` rows of `bytes` bytes from a strided FFmpeg plane into a packed destination. */
void CopyRows(std::uint8_t* dst, const std::uint8_t* src, std::ptrdiff_t stride, std::uint32_t rows, std::uint32_t bytes) {
    for (std::uint32_t r = 0; r < rows; ++r) {
        std::memcpy(dst + static_cast<std::size_t>(r) * bytes, src + static_cast<std::ptrdiff_t>(r) * stride, bytes);
    }
}

/**
 * Converts an FFmpeg frame to the packed Picture. Only 8-bit 4:2:0 (planar or NV12) is converted;
 * any other format returns false and the caller reports a corrupt picture rather than guessing.
 */
bool ToPicture(const AVFrame& frame, Picture& out) {
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    const auto width = static_cast<std::uint32_t>(frame.width);
    const auto height = static_cast<std::uint32_t>(frame.height);
    const std::uint32_t chromaRows = (height + 1) / 2;
    const std::uint32_t chromaWidth = (width + 1) / 2;
    const std::uint32_t chromaRowBytes = chromaWidth * 2;
    const auto format = static_cast<AVPixelFormat>(frame.format);
    out.width = width;
    out.height = height;
    out.luma.assign(static_cast<std::size_t>(width) * height, 0);
    out.chroma.assign(static_cast<std::size_t>(chromaRowBytes) * chromaRows, 0);
    if (format != AV_PIX_FMT_YUV420P && format != AV_PIX_FMT_YUVJ420P && format != AV_PIX_FMT_NV12) {
        return false;
    }
    CopyRows(out.luma.data(), frame.data[0], frame.linesize[0], height, width);
    if (format == AV_PIX_FMT_NV12) {
        CopyRows(out.chroma.data(), frame.data[1], frame.linesize[1], chromaRows, chromaRowBytes);
    } else {
        for (std::uint32_t r = 0; r < chromaRows; ++r) {
            const std::uint8_t* u = frame.data[1] + static_cast<std::ptrdiff_t>(r) * frame.linesize[1];
            const std::uint8_t* v = frame.data[2] + static_cast<std::ptrdiff_t>(r) * frame.linesize[2];
            std::uint8_t* row = out.chroma.data() + static_cast<std::size_t>(r) * chromaRowBytes;
            for (std::uint32_t c = 0; c < chromaWidth; ++c) {  // Interleave Cb/Cr.
                row[2 * c] = u[c];
                row[2 * c + 1] = v[c];
            }
        }
    }
    return true;
}

}  // namespace

struct H264Decoder::Impl {
    AVCodecContext* context = nullptr;
    std::deque<FramePtr> ready;
    bool draining = false;  // Drain() sent end-of-stream; the codec must be flushed before new input.

    ~Impl() { avcodec_free_context(&context); }

    /** Moves every frame the decoder can currently produce into `ready`. */
    void Collect() {
        for (;;) {
            FramePtr frame(av_frame_alloc());
            if (!frame) {
                std::fprintf(stderr, "[VIDEODEC2] av_frame_alloc failed\n");
                return;
            }
            const int rc = avcodec_receive_frame(context, frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                return;  // Normal end states: more input needed, or the stream was fully drained.
            }
            if (rc < 0) {
                LogAvError("avcodec_receive_frame", rc);
                return;
            }
            ready.push_back(std::move(frame));
        }
    }
};

bool H264DecoderAvailable() { return true; }

const char* H264DecoderLicense() { return avcodec_license(); }

std::unique_ptr<H264Decoder> H264Decoder::Create() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->context = avcodec_alloc_context3(codec);
    if (impl->context == nullptr) {
        return nullptr;
    }
    impl->context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
    impl->context->thread_count = 1;
    if (avcodec_open2(impl->context, codec, nullptr) < 0) {
        return nullptr;
    }
    return std::unique_ptr<H264Decoder>(new H264Decoder(std::move(impl)));
}

H264Decoder::H264Decoder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
H264Decoder::~H264Decoder() = default;

DecodeStatus H264Decoder::Decode(const std::uint8_t* data, std::size_t size, const AccessUnitInfo& info) {
    if (data == nullptr || size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max() - AV_INPUT_BUFFER_PADDING_SIZE)) {
        return DecodeStatus::BadAccessUnit;
    }
    // Annex B requires a start code up front; raw length-prefixed data would be mis-parsed silently.
    const bool startCode3 = size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1;
    const bool startCode4 = size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1;
    if (!startCode3 && !startCode4) {
        return DecodeStatus::BadAccessUnit;
    }
    if (impl_->draining) {
        avcodec_flush_buffers(impl_->context);
        impl_->draining = false;
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr || av_new_packet(packet, static_cast<int>(size)) < 0) {
        av_packet_free(&packet);
        return DecodeStatus::BadAccessUnit;
    }
    std::memcpy(packet->data, data, size);
    packet->pts = static_cast<std::int64_t>(info.pts);
    packet->dts = static_cast<std::int64_t>(info.dts);
    packet->opaque_ref = av_buffer_alloc(sizeof(AccessUnitInfo));
    if (packet->opaque_ref == nullptr) {
        av_packet_free(&packet);
        return DecodeStatus::BadAccessUnit;
    }
    std::memcpy(packet->opaque_ref->data, &info, sizeof(info));

    int result = avcodec_send_packet(impl_->context, packet);
    if (result == AVERROR(EAGAIN)) {
        // Output is full: drain it first, then the packet must be accepted.
        impl_->Collect();
        result = avcodec_send_packet(impl_->context, packet);
    }
    av_packet_free(&packet);
    if (result < 0) {
        LogAvError("avcodec_send_packet", result);
        return DecodeStatus::BadAccessUnit;
    }
    impl_->Collect();
    return DecodeStatus::Ok;
}

bool H264Decoder::PopPicture(Picture& out) {
    if (impl_->ready.empty()) {
        return false;
    }
    FramePtr frame = std::move(impl_->ready.front());
    impl_->ready.pop_front();
    out = Picture{};
    const bool converted = ToPicture(*frame, out);
    if (frame->opaque_ref != nullptr && frame->opaque_ref->size >= sizeof(AccessUnitInfo)) {
        std::memcpy(&out.info, frame->opaque_ref->data, sizeof(AccessUnitInfo));
    }
    out.keyFrame = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
    out.corrupt = !converted || (frame->flags & AV_FRAME_FLAG_CORRUPT) != 0;
    // FFmpeg ORs constraint flags above bit 8 into the profile; the low byte is profile_idc.
    out.profile = impl_->context->profile > 0 ? static_cast<std::uint32_t>(impl_->context->profile) & 0xFFu : 0;
    out.level = impl_->context->level > 0 ? static_cast<std::uint32_t>(impl_->context->level) : 0;
    out.sarWidth = frame->sample_aspect_ratio.num > 0 ? static_cast<std::uint16_t>(std::min(frame->sample_aspect_ratio.num, 65535)) : 0;
    out.sarHeight = frame->sample_aspect_ratio.den > 0 ? static_cast<std::uint16_t>(std::min(frame->sample_aspect_ratio.den, 65535)) : 0;
    out.colorRange = static_cast<std::uint8_t>(frame->color_range);
    out.colorPrimaries = static_cast<std::uint8_t>(frame->color_primaries);
    out.colorTransfer = static_cast<std::uint8_t>(frame->color_trc);
    out.colorMatrix = static_cast<std::uint8_t>(frame->colorspace);
    return true;
}

void H264Decoder::Drain() {
    if (impl_->draining) {
        return;
    }
    impl_->draining = true;
    const int rc = avcodec_send_packet(impl_->context, nullptr);
    if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
        LogAvError("avcodec_send_packet(drain)", rc);
    }
    impl_->Collect();
}

void H264Decoder::Reset() {
    impl_->ready.clear();
    impl_->draining = false;
    avcodec_flush_buffers(impl_->context);
}

#else  // !APS5_HAVE_FFMPEG

struct H264Decoder::Impl {};

bool H264DecoderAvailable() { return false; }
const char* H264DecoderLicense() { return ""; }
std::unique_ptr<H264Decoder> H264Decoder::Create() { return nullptr; }
H264Decoder::H264Decoder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
H264Decoder::~H264Decoder() = default;
DecodeStatus H264Decoder::Decode(const std::uint8_t*, std::size_t, const AccessUnitInfo&) { return DecodeStatus::BadAccessUnit; }
bool H264Decoder::PopPicture(Picture&) { return false; }
void H264Decoder::Drain() {}
void H264Decoder::Reset() {}

#endif

}  // namespace Videodec2
