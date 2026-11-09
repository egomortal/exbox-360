// Shared desktop/reference transport: one decoder, one pending picture, bounded PCM.
// Native hardware presentation will consume the packet-mode session separately.
#pragma once
#include "session.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <atomic>
namespace exbox::video {
class Transport {
    Session session_;
    OmniMedia::VideoFrame pending_,shown_;
    SDL_AudioDeviceID audio_=0;
    // Dashboard mode feeds the existing callback; standalone mode owns an SDL queue.
    std::array<int16_t,288000> pcm_{};
    size_t head_=0,count_=0;
    bool external_=false;
    std::atomic<bool> mixing_{false};
    std::mutex pcmMutex_;
    Uint32 queued() {
        if(!external_)return audio_?SDL_GetQueuedAudioSize(audio_):0;
        std::lock_guard<std::mutex> lock(pcmMutex_);return Uint32(count_*2);
    }
    void clearAudio() {
        if(audio_)SDL_ClearQueuedAudio(audio_);
        std::lock_guard<std::mutex> lock(pcmMutex_);head_=count_=0;
    }
    void pauseAudio(bool value) {
        mixing_.store(!value);if(audio_)SDL_PauseAudioDevice(audio_,value?1:0);
    }
    bool queue(const std::vector<int16_t>& pcm) {
        if(!external_)return !audio_||SDL_QueueAudio(audio_,pcm.data(),Uint32(pcm.size()*2))==0;
        std::lock_guard<std::mutex> lock(pcmMutex_);
        if(pcm.size()>pcm_.size()-count_)return false;
        for(auto sample:pcm){pcm_[(head_+count_)%pcm_.size()]=sample;++count_;}
        return true;
    }
    bool opened_=false,ready_=false,ended_=false,paused_=false,muted_=false;
    uint64_t anchor_=0,pauseTick_=0,generation_=0;
    double origin_=0;
    std::string failure_;
    bool fail(const std::string& error) { failure_=error; pauseAudio(true); return false; }
public:
    ~Transport(){close();}
    Transport()=default;
    Transport(const Transport&)=delete;
    Transport& operator=(const Transport&)=delete;
    // No allocation in the dashboard audio callback; only this bounded ring is shared.
    void mix(int* acc,size_t frames) {
        if(!mixing_.load())return;
        std::lock_guard<std::mutex> lock(pcmMutex_);
        if(!mixing_.load())return;
        size_t samples=std::min(frames*2,count_);
        for(size_t i=0;i<samples;++i){acc[i]+=pcm_[head_];head_=(head_+1)%pcm_.size();}
        count_-=samples;
    }
    void close() {
        pauseAudio(true);clearAudio();
        if(audio_){SDL_ClearQueuedAudio(audio_);SDL_CloseAudioDevice(audio_);audio_=0;}
        session_.close();pending_={};shown_={};opened_=ready_=ended_=paused_=muted_=false;
        anchor_=pauseTick_=generation_=0;origin_=0;failure_.clear();
    }
    bool open(const std::string& path,bool dashboardMixer=false) {
        close();external_=dashboardMixer;
        if(!session_.open(path,false))return fail(session_.error());
        if(!external_ && session_.hasAudio() && SDL_WasInit(SDL_INIT_AUDIO)) {
            SDL_AudioSpec spec{};spec.freq=48000;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
            audio_=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
            if(!audio_)return fail(SDL_GetError());
        }
        opened_=true;anchor_=SDL_GetTicks64();return true;
    }
    double duration()const{return session_.duration();}
    double elapsed()const{return origin_+double((paused_?pauseTick_:SDL_GetTicks64())-anchor_)/1000.;}
    bool paused()const{return paused_;}
    bool ended(){return ended_&&!ready_&&queued()==0;}
    const std::string& error()const{return failure_;}
    uint64_t generation()const{return generation_;}
    const OmniMedia::VideoFrame& frame()const{return shown_;}
    void pause(bool value) {
        if(!opened_||paused_==value)return;
        if(value)pauseTick_=SDL_GetTicks64();else anchor_+=SDL_GetTicks64()-pauseTick_;
        paused_=value;pauseAudio(value||muted_);
    }
    bool seek(double seconds) {
        if(!opened_||!session_.seek(seconds))return false;
        pauseAudio(true);clearAudio();
        origin_=seconds;anchor_=SDL_GetTicks64();pauseTick_=anchor_;ready_=ended_=false;pending_={};
        return true;
    }
    // Muting discards queued PCM and repositions decoding on resume. No stale audio burst.
    void mute(bool value) {
        if(value==muted_)return;
        muted_=value;pauseAudio(true);clearAudio();
        if(!value)seek(std::min(elapsed(),duration()));
    }
    bool tick() {
        if(!opened_||!failure_.empty())return false;
        if(paused_)return true;
        // Bounded work in the reference renderer. A worker is required before native default use.
        for(int budget=0;budget<8;++budget) {
            if(ready_) {
                if(pending_.seconds>elapsed()+0.001)break;
                shown_=std::move(pending_);pending_={};++generation_;ready_=false;
            }
            if(ended_ || (!muted_&&queued()>=192000))break;
            auto result=session_.next(pending_);auto pcm=session_.audio();
            if(result==Session::Result::Error)return fail(session_.error());
            if((audio_||external_)&&!muted_&&!pcm.empty()) {
                size_t bytes=pcm.size()*sizeof(int16_t);
                if(bytes>384000||queued()+bytes>576000)return fail("Audio queue bound exceeded");
                if(!queue(pcm))return fail(SDL_GetError());
                pauseAudio(false);
            }
            ready_=result==Session::Result::Frame;ended_=result==Session::Result::End;
        }
        return true;
    }
};
}
