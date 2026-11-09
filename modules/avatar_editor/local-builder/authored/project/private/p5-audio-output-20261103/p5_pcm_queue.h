#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
namespace av2::native {
class PcmQueue {
 public:
  static constexpr std::size_t samples=256, channels=2, capacity=64;
  using Frame=std::array<float,samples*channels>;
  bool push(const Frame& frame) {
    if(count_==capacity) return false;
    frames_[(head_+count_)%capacity]=frame; ++count_; return true;
  }
  struct Read { unsigned consumed=0; bool underrun=false; };
  Read render(float* out,std::size_t stereo_samples) {
    Read result; std::fill_n(out,stereo_samples*channels,0.0f);
    if(!started_) { if(count_<3) return result; started_=true; }
    std::size_t done=0;
    while(done<stereo_samples && count_) {
      const auto n=std::min(samples-offset_,stereo_samples-done);
      std::memcpy(out+done*channels,frames_[head_].data()+offset_*channels,n*channels*sizeof(float));
      done+=n; offset_+=n;
      if(offset_==samples) {offset_=0;head_=(head_+1)%capacity;--count_;++result.consumed;}
    }
    if(done<stereo_samples) {result.underrun=true;started_=false;}
    return result;
  }
  std::size_t queued() const {return count_;}
 private:
  std::array<Frame,capacity> frames_{};
  std::size_t head_=0,count_=0,offset_=0;
  bool started_=false;
};
}
