// One-file decode session. Original packets or lossless decoded pixels; no NXEF/JPEG/WAV staging.
#pragma once
#include "local_source.hpp"
#include <cmath>
namespace exbox::video {
class Session {
    std::unique_ptr<OmniMedia::VideoDecoder> decoder_;
    std::string error_;
public:
    using Result = OmniMedia::VideoDecoder::Result;
    // Packet mode retains the original coded video up to 4K for a native decoder.
    // Software mode is the established bounded 1080p desktop reference path.
    bool open(const std::string& path, bool packets) {
        close();
        auto source = LocalSource::open(path);
        if (!source) { error_ = "Cannot open a nonempty local regular file"; return false; }
        auto candidate = std::make_unique<OmniMedia::VideoDecoder>();
        bool ok = packets ? candidate->openSourcePackets(source) : candidate->openSource(source);
        if (!ok) { error_ = candidate->error(); return false; }
        if (packets && !candidate->packetMode()) { error_ = "No direct hardware packet path for this codec"; return false; }
        decoder_ = std::move(candidate);
        return true;
    }
    void close() { decoder_.reset(); error_.clear(); }
    bool seek(double seconds) { return decoder_ && std::isfinite(seconds) && decoder_->seek(seconds); }
    bool packets() const { return decoder_ && decoder_->packetMode(); }
    double duration() const { return decoder_ ? decoder_->duration() : 0; }
    bool hasAudio() const { return decoder_ && decoder_->hasAudio(); }
    const std::string& error() const { return decoder_ ? decoder_->error() : error_; }
    const OmniMedia::DemuxTrack* track() const { return packets() ? &decoder_->videoTrack() : nullptr; }
    Result next(OmniMedia::VideoFrame& frame) {
        return decoder_ && !packets() ? decoder_->next(frame) : Result::Error;
    }
    Result nextPacket(OmniMedia::DemuxPacket& packet) {
        return packets() ? decoder_->nextPacket(packet) : Result::Error;
    }
    std::vector<int16_t> audio() { return decoder_ ? decoder_->takeAudio() : std::vector<int16_t>{}; }
};
}
