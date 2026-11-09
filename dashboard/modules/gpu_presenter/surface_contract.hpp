// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>

namespace exbox::presenter {
// Description travels with every decoded picture; no implicit BT.601 default.
enum class Matrix { unknown, bt601, bt709, bt2020 };
enum class Range { unknown, limited, full };
enum class Transfer { unknown, sdr, pq };
enum class Packing { nv12, ten_low, ten_high };
struct Plane { uint64_t bytes; uint32_t stride; };
struct Surface {
    uint32_t width, height;
    Plane y, uv;
    Packing packing;
    Matrix matrix;
    Range range;
    Transfer transfer;
    bool primaries2020;
};
inline void validate(const Surface& s) {
    if (!s.width || !s.height || s.width>3840 || s.height>2160)
        throw std::invalid_argument("unsupported visible extent");
    if (s.matrix==Matrix::unknown || s.range==Range::unknown || s.transfer==Transfer::unknown)
        throw std::invalid_argument("missing colour metadata; explicit policy required");
    const uint64_t unit=s.packing==Packing::nv12?1:2;
    const uint64_t aligned=(uint64_t(s.width)+15)&~uint64_t(15);
    // Preserve decoder padding. Visible width never becomes a row pitch.
    if (s.y.stride<aligned*unit || s.uv.stride<aligned*unit ||
        s.y.stride%(16*unit) || s.uv.stride%(16*unit))
        throw std::invalid_argument("plane stride is not 16-pixel aligned");
    if (s.y.bytes<uint64_t(s.y.stride)*s.height ||
        s.uv.bytes<uint64_t(s.uv.stride)*((uint64_t(s.height)+1)/2))
        throw std::invalid_argument("plane allocation too short");
    if (s.transfer==Transfer::pq && (s.packing==Packing::nv12 ||
        s.matrix!=Matrix::bt2020 || !s.primaries2020))
        throw std::invalid_argument("unsupported PQ colour combination");
}
enum class Output { sdr, pq };
inline Output desired_output(const Surface& s,bool fullscreen,uint64_t title_attributes) {
    validate(s);
    if (fullscreen && s.transfer==Transfer::pq && (title_attributes&0x20000000))
        return Output::pq;
    // Caller must explicitly tone-map an HDR picture if the result is SDR.
    return Output::sdr;
}
inline bool needs_tone_map(const Surface& s,Output output) {
    return s.transfer==Transfer::pq && output==Output::sdr;
}
inline void validate_scanout(Output output,uint64_t format,bool tiled,int actual_range) {
    const uint64_t expected=output==Output::pq?0x8100070422000000ULL:0x8000000022000000ULL;
    if (!tiled || format!=expected || actual_range!=(output==Output::pq?2:1))
        throw std::invalid_argument("scanout layout/format/range mismatch");
}
// Decoder lease is distinct from the destination's flip retirement. Upload/import
// and all GPU sampling must finish before a source can return to the decoder.
class DecoderLease {
    enum class State { free, decoded, sampling };
    State state_=State::free;
    uint64_t token_=0;
public:
    void acquire() {
        if(state_!=State::free)throw std::logic_error("decoder surface still owned");
        state_=State::decoded;
    }
    void submit(uint64_t token) {
        if(state_!=State::decoded || !token)throw std::logic_error("invalid GPU submission");
        token_=token;state_=State::sampling;
    }
    bool release(uint64_t completed) {
        if(state_==State::free)throw std::logic_error("double release");
        if(state_==State::sampling && completed<token_)return false;
        state_=State::free;return true;
    }
};
} // namespace exbox::presenter
