#include "gpu_edge.h"
namespace av2 {
// 12581 D3DDevice offsets: owner's gpu/guest/d3d_device.h static assertions.
constexpr uint32_t kWritebackPage=0x2A90, kQueuedChunks=0x2B04;
void NullGpuEdge::chunk_queued(uint32_t device) {
  std::lock_guard lock(mutex_);
  // QueueIndirectBuffer -> BlockUntilIdle: retire the queued indirect buffers.
  memory_.write32(device+kQueuedChunks,0); ++counters_.chunks;
}
void NullGpuEdge::fence_issued(uint32_t device,uint32_t fence,uint32_t ring_position) {
  std::lock_guard lock(mutex_);
  ++counters_.fences_issued;
  // SetFence CPU completion writes fence then tagged ring position. Preserve
  // submission order under this lock, including fence counter wraparound.
  if (const auto writeback=memory_.read32(device+kWritebackPage)) {
    memory_.write32(writeback,fence); memory_.write32(writeback+4,ring_position);
    ++counters_.fences_signalled;
  }
}
void NullGpuEdge::primary_kick(uint32_t device,uint32_t write_index) {
  std::lock_guard lock(mutex_);
  // AddCallsToPrimaryBuffer -> BlockOnPrimaryRange: reader follows writer.
  if (const auto writeback=memory_.read32(device+kWritebackPage))
    memory_.write32(writeback+0x3C,write_index);
  ++counters_.kicks;
}
void NullGpuEdge::gamma(const std::array<uint16_t,768>& ramp) {
  std::lock_guard lock(mutex_); ramp_=ramp; ++counters_.gamma_updates;
}
void NullGpuEdge::tile_size(uint32_t pixels) {
  std::lock_guard lock(mutex_); tile_size_=pixels;
}
uint32_t NullGpuEdge::tile_size() const { std::lock_guard lock(mutex_); return tile_size_; }
GpuEdgeCounters NullGpuEdge::counters() const { std::lock_guard lock(mutex_); return counters_; }
std::array<uint16_t,768> NullGpuEdge::gamma_ramp() const { std::lock_guard lock(mutex_); return ramp_; }
}