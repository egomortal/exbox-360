#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace av2::legend {
// Existing renderer: two queued frames + two GPU slots + current producer.
// Eight slots exceed that bound; any violated/overwritten association hides UI.
class FrameTags {
 std::array<std::atomic<uint64_t>,8> slots_{};
 public:
 static constexpr uint64_t max_frame=UINT64_MAX>>1;
 bool publish(uint64_t frame,bool root){
  if(!frame||frame>max_frame)return false;
  slots_[frame%slots_.size()].store((frame<<1)|uint64_t(root),std::memory_order_release);return true;
 }
 bool read(uint64_t frame,bool& root)const{
  root=false;if(!frame||frame>max_frame)return false;
  const uint64_t tagged=slots_[frame%slots_.size()].load(std::memory_order_acquire);
  if((tagged>>1)!=frame)return false;root=(tagged&1)!=0;return true;
 }
};
}
extern "C" void av2_legend_publish(uint64_t frame,bool root);
extern "C" bool av2_legend_root(uint64_t frame);
