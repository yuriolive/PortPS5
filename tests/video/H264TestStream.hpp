// tests/video/H264TestStream.hpp
// Hand-built, synthetic H.264 Annex B streams for the libSceVideodec2 tests. No encoder and no media
// are involved: the streams are Baseline profile, CAVLC, with I_PCM macroblocks (raw samples, so
// decoding is lossless and a decoded picture must equal the generated planes bit for bit) and
// P_Skip frames (a copy of the previous picture). The bit layouts follow ITU-T H.264 7.3 (SPS, PPS,
// slice header, macroblock layer); each writer notes the syntax element it emits.

#ifndef TESTS_VIDEO_H264TESTSTREAM_HPP
#define TESTS_VIDEO_H264TESTSTREAM_HPP

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace H264Test {

/** MSB-first bit writer with Exp-Golomb helpers. */
class BitWriter {
public:
    void Bit(unsigned b) {
        if (used_ == 0) bytes_.push_back(0);
        if (b) bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (0x80u >> used_));
        used_ = (used_ + 1) % 8;
    }
    void Bits(std::uint32_t value, unsigned count) {
        for (unsigned i = count; i-- > 0;) Bit((value >> i) & 1u);
    }
    void Ue(std::uint32_t v) {
        const std::uint32_t x = v + 1;
        unsigned len = 0;
        while ((x >> (len + 1)) != 0) ++len;
        Bits(0, len);
        Bits(x, len + 1);
    }
    void Se(std::int32_t v) { Ue(v > 0 ? static_cast<std::uint32_t>(2 * v - 1) : static_cast<std::uint32_t>(-2 * v)); }
    void AlignZero() {
        while (used_ != 0) Bit(0);
    }
    /** rbsp_trailing_bits: a one bit, then zero bits up to the byte boundary. */
    void Trailing() {
        Bit(1);
        AlignZero();
    }
    void Byte(std::uint8_t b) { Bits(b, 8); }
    bool Aligned() const { return used_ == 0; }
    const std::vector<std::uint8_t>& Bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    unsigned used_ = 0;
};

/** Wraps an RBSP into an Annex B NAL unit: 4-byte start code, header, emulation prevention. */
inline std::vector<std::uint8_t> Nal(unsigned refIdc, unsigned type, const std::vector<std::uint8_t>& rbsp) {
    std::vector<std::uint8_t> out = {0, 0, 0, 1, static_cast<std::uint8_t>((refIdc << 5) | type)};
    int zeros = 0;
    for (std::uint8_t b : rbsp) {
        if (zeros >= 2 && b <= 3) {  // 00 00 0x would look like a start code: insert 03.
            out.push_back(3);
            zeros = 0;
        }
        out.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
    return out;
}

/** Geometry of a test stream. Sizes are in 16x16 macroblocks; crop offsets are in 2-sample units. */
struct Geometry {
    unsigned mbWidth = 2;
    unsigned mbHeight = 2;
    unsigned cropRight = 0;
    unsigned cropBottom = 0;
    unsigned Width() const { return mbWidth * 16 - cropRight * 2; }
    unsigned Height() const { return mbHeight * 16 - cropBottom * 2; }
};

/** A planar 4:2:0 picture covering the whole macroblock grid. */
struct Planes {
    unsigned width = 0;   // Macroblock-aligned.
    unsigned height = 0;
    std::vector<std::uint8_t> y, cb, cr;  // cb/cr are (width/2) x (height/2).
};

/** Deterministic pattern: every sample differs from its neighbours, so any layout error shows. */
inline Planes Pattern(const Geometry& g, unsigned seed) {
    Planes p;
    p.width = g.mbWidth * 16;
    p.height = g.mbHeight * 16;
    p.y.resize(static_cast<std::size_t>(p.width) * p.height);
    p.cb.resize(static_cast<std::size_t>(p.width / 2) * (p.height / 2));
    p.cr.resize(p.cb.size());
    for (unsigned r = 0; r < p.height; ++r)
        for (unsigned c = 0; c < p.width; ++c) p.y[r * p.width + c] = static_cast<std::uint8_t>(16 + (c * 7 + r * 13 + seed) % 220);
    for (unsigned r = 0; r < p.height / 2; ++r)
        for (unsigned c = 0; c < p.width / 2; ++c) {
            p.cb[r * (p.width / 2) + c] = static_cast<std::uint8_t>(16 + (c * 5 + r * 3 + seed * 2) % 220);
            p.cr[r * (p.width / 2) + c] = static_cast<std::uint8_t>(16 + (c * 11 + r * 9 + seed * 3) % 220);
        }
    return p;
}

/** SPS NAL (type 7): Baseline profile, level 1.0, one reference frame, POC type 0, optional cropping. */
inline std::vector<std::uint8_t> Sps(const Geometry& g) {
    BitWriter w;
    w.Byte(66);  // profile_idc: Baseline
    w.Byte(0);   // constraint_set flags + reserved_zero_2bits
    w.Byte(10);  // level_idc: 1.0
    w.Ue(0);     // seq_parameter_set_id
    w.Ue(0);     // log2_max_frame_num_minus4 -> frame_num is 4 bits
    w.Ue(0);     // pic_order_cnt_type
    w.Ue(0);     // log2_max_pic_order_cnt_lsb_minus4 -> 4 bits
    w.Ue(1);     // max_num_ref_frames
    w.Bit(0);    // gaps_in_frame_num_value_allowed_flag
    w.Ue(g.mbWidth - 1);
    w.Ue(g.mbHeight - 1);  // pic_height_in_map_units_minus1 (frame_mbs_only_flag = 1)
    w.Bit(1);    // frame_mbs_only_flag
    w.Bit(0);    // direct_8x8_inference_flag
    const bool crop = g.cropRight != 0 || g.cropBottom != 0;
    w.Bit(crop);  // frame_cropping_flag
    if (crop) {
        w.Ue(0);
        w.Ue(g.cropRight);
        w.Ue(0);
        w.Ue(g.cropBottom);
    }
    w.Bit(0);  // vui_parameters_present_flag
    w.Trailing();
    return Nal(3, 7, w.Bytes());
}

/** PPS NAL (type 8): CAVLC, one slice group, default QP, no deblocking control. */
inline std::vector<std::uint8_t> Pps() {
    BitWriter w;
    w.Ue(0);  // pic_parameter_set_id
    w.Ue(0);  // seq_parameter_set_id
    w.Bit(0); // entropy_coding_mode_flag: CAVLC
    w.Bit(0); // bottom_field_pic_order_in_frame_present_flag
    w.Ue(0);  // num_slice_groups_minus1
    w.Ue(0);  // num_ref_idx_l0_default_active_minus1
    w.Ue(0);  // num_ref_idx_l1_default_active_minus1
    w.Bit(0); // weighted_pred_flag
    w.Bits(0, 2);  // weighted_bipred_idc
    w.Se(0);  // pic_init_qp_minus26
    w.Se(0);  // pic_init_qs_minus26
    w.Se(0);  // chroma_qp_index_offset
    w.Bit(0); // deblocking_filter_control_present_flag
    w.Bit(0); // constrained_intra_pred_flag
    w.Bit(0); // redundant_pic_cnt_present_flag
    w.Trailing();
    return Nal(3, 8, w.Bytes());
}

/** IDR slice NAL (type 5) whose macroblocks are all I_PCM copies of `planes`. */
inline std::vector<std::uint8_t> IdrPcm(const Geometry& g, const Planes& planes) {
    BitWriter w;
    w.Ue(0);        // first_mb_in_slice
    w.Ue(7);        // slice_type: I (all slices)
    w.Ue(0);        // pic_parameter_set_id
    w.Bits(0, 4);   // frame_num
    w.Ue(0);        // idr_pic_id
    w.Bits(0, 4);   // pic_order_cnt_lsb
    w.Bit(0);       // no_output_of_prior_pics_flag
    w.Bit(0);       // long_term_reference_flag
    w.Se(0);        // slice_qp_delta
    for (unsigned mby = 0; mby < g.mbHeight; ++mby) {
        for (unsigned mbx = 0; mbx < g.mbWidth; ++mbx) {
            w.Ue(25);  // mb_type: I_PCM
            w.AlignZero();  // pcm_alignment_zero_bit
            for (unsigned r = 0; r < 16; ++r)
                for (unsigned c = 0; c < 16; ++c) w.Byte(planes.y[(mby * 16 + r) * planes.width + mbx * 16 + c]);
            for (const auto* plane : {&planes.cb, &planes.cr})
                for (unsigned r = 0; r < 8; ++r)
                    for (unsigned c = 0; c < 8; ++c) w.Byte((*plane)[(mby * 8 + r) * (planes.width / 2) + mbx * 8 + c]);
        }
    }
    w.Trailing();  // rbsp_slice_trailing_bits
    return Nal(3, 5, w.Bytes());
}

/** Non-IDR P slice NAL (type 1) in which every macroblock is skipped: a copy of the previous picture. */
inline std::vector<std::uint8_t> PSkip(const Geometry& g, unsigned frameNum) {
    BitWriter w;
    w.Ue(0);                    // first_mb_in_slice
    w.Ue(5);                    // slice_type: P (all slices)
    w.Ue(0);                    // pic_parameter_set_id
    w.Bits(frameNum & 15, 4);   // frame_num
    w.Bits((frameNum * 2) & 15, 4);  // pic_order_cnt_lsb
    w.Bit(0);                   // num_ref_idx_active_override_flag
    w.Bit(0);                   // ref_pic_list_modification_flag_l0
    w.Bit(0);                   // adaptive_ref_pic_marking_mode_flag
    w.Se(0);                    // slice_qp_delta
    w.Ue(g.mbWidth * g.mbHeight);  // mb_skip_run: the whole picture
    w.Trailing();
    return Nal(2, 1, w.Bytes());
}

/** Concatenates NAL units into one access unit. */
inline std::vector<std::uint8_t> Join(std::initializer_list<std::vector<std::uint8_t>> nals) {
    std::vector<std::uint8_t> out;
    for (const auto& n : nals) out.insert(out.end(), n.begin(), n.end());
    return out;
}

}  // namespace H264Test

#endif
