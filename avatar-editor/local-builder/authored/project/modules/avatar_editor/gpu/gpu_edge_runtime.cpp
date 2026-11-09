#include "gpu_edge_runtime.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <rex/types.h>
#include <atomic>
#include <rex/system/interfaces/graphics.h>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
namespace av2 {
#ifdef AV2_OWNER_RENDERER
void rendered_script_tick(uint64_t frame);
#endif
namespace {
class RuntimeMemory final : public GpuGuestMemory {
  rex::be<uint32_t>* word(uint32_t address,bool writing) {
    auto* mem=rex::system::kernel_memory();
    auto* heap=mem?mem->LookupHeap(address):nullptr;
    uint32_t size=0,protect=0;
    if ((address&3) || address<0x10000 || !heap || !heap->QuerySize(address,&size) || !size ||
        !heap->QueryProtect(address,&protect) ||
        !(protect & (writing?rex::memory::kMemoryProtectWrite:rex::memory::kMemoryProtectRead))) {
      std::fprintf(stderr,"GPU edge invalid guest word %08x write=%d\n",address,writing);
      std::_Exit(71);
    }
    return mem->TranslateVirtual<rex::be<uint32_t>*>(address);
  }
 public:
  uint32_t read32(uint32_t a) override { return *word(a,false); }
  void write32(uint32_t a,uint32_t v) override { *word(a,true)=v; }
};
// Vulkan owns command consumption through the SDK's original MMIO path.
// These hooks observe guest fences; they never synthesize completion writes.
// Guest fence visibility alone does not prove native GPU completion/readback.
class VulkanGpuEdge final : public GpuEdge {
 public:
  explicit VulkanGpuEdge(GpuGuestMemory& memory): memory_(memory) {}
  void chunk_queued(uint32_t) override { std::lock_guard lock(mutex_); ++counts_.chunks; }
  void primary_kick(uint32_t,uint32_t) override { std::lock_guard lock(mutex_); ++counts_.kicks; }
  void fence_issued(uint32_t device,uint32_t fence,uint32_t) override {
    std::lock_guard lock(mutex_); collect(); ++counts_.fences_issued;
    const auto address=memory_.read32(device+0x2A90);
    if (address) pending_.push_back({address,fence});
    if (pending_.size()>65536) { std::fprintf(stderr,"VULKAN pending fence bound exceeded\n"); std::_Exit(73); }
  }
  void gamma(const std::array<uint16_t,768>& ramp) override {
    std::lock_guard lock(mutex_); ramp_=ramp; ++counts_.gamma_updates;
  }
  void tile_size(uint32_t pixels) override { std::lock_guard lock(mutex_); tile_=pixels; }
  uint32_t tile_size() const override { std::lock_guard lock(mutex_); return tile_; }
  GpuEdgeCounters counters() const override {
    std::lock_guard lock(mutex_); collect(); return counts_;
  }
 private:
  void collect() const {
    while (!pending_.empty()) {
      const auto [address,value]=pending_.front();
      // Modular comparison, bounded far below half of the 32-bit sequence space.
      if (int32_t(memory_.read32(address)-value)<0) break;
      pending_.pop_front(); ++counts_.fences_signalled;
    }
  }
  GpuGuestMemory& memory_;
  mutable std::mutex mutex_;
  mutable GpuEdgeCounters counts_{};
  mutable std::deque<std::pair<uint32_t,uint32_t>> pending_;
  std::array<uint16_t,768> ramp_{}; // Retained; renderer gamma binding remains to verify.
  uint32_t tile_=64;
};
RuntimeMemory memory;
NullGpuEdge edge(memory);
VulkanGpuEdge vulkan_edge(memory);
rex::system::IGraphicsSystem* graphics=nullptr;

std::mutex counts_mutex;
std::map<std::string,uint64_t> hook_counts;
std::atomic<uint64_t> frames{0};
}
void use_vulkan_gpu(rex::system::IGraphicsSystem* value) { graphics=value; }
GpuEdge& active_gpu_edge() { return graphics?static_cast<GpuEdge&>(vulkan_edge):edge; }
void initialize_gpu_ring(uint32_t ring,int32_t size) {
  if (graphics) { std::fprintf(stderr,"VULKAN ring=%08x size_log2=%d\n",ring,size); graphics->InitializeRingBuffer(ring,size); }
}
void enable_gpu_writeback(uint32_t address,int32_t block) {
  if (graphics) graphics->EnableReadPointerWriteBack(address,block);
}
void set_gpu_interrupt(uint32_t callback,uint32_t data) {
  if (graphics) graphics->SetInterruptCallback(callback,data);
}
void note_title_hook(const char* name) {
  std::lock_guard lock(counts_mutex);
  if (++hook_counts[name]==1) std::fprintf(stderr,"HEADLESS first hook: %s\n",name);
}
uint64_t completed_frames() { return frames.load(std::memory_order_relaxed); }
void frame_completed() {
  const auto count=++frames;
#if defined(__PROSPERO__)
  // Interactive native title: the desktop 300-frame completion gate must not
  // terminate a session. The native frontend polls completed_frames().
  if(count==1 || count%300==0)
    std::fprintf(stderr,"AV2 native editor frame=%llu\n",(unsigned long long)count);
  return;
#else
#ifdef AV2_OWNER_RENDERER
  rendered_script_tick(count);
#endif
  std::fprintf(stderr,"HEADLESS AE_XuiFrameTick frame=%llu\n",(unsigned long long)count);
  #ifdef AV2_SHARED_SAVE_TEST
  constexpr uint64_t frame_limit=900;
  #else
  constexpr uint64_t frame_limit=300;
  #endif
  // Extended local playback observations leave the native session unlimited
  // and retain the normal default gate. Bound overrides to five minutes.
  static const uint64_t observation_limit=[] {
    const char* value=std::getenv("AV2_FRAME_LIMIT");
    if (!value) return frame_limit;
    char* end=nullptr;
    const auto parsed=std::strtoul(value,&end,10);
    if (!*value || *end || parsed<300 || parsed>18000) {
      std::fprintf(stderr,"Invalid AV2_FRAME_LIMIT (expected 300..18000)\n");
      std::_Exit(2);
    }
    return uint64_t(parsed);
  }();
  if (count<observation_limit) return;
  const auto c=active_gpu_edge().counters();
  std::fprintf(stderr,"HEADLESS GPU chunks=%llu fences=%llu signalled=%llu kicks=%llu frames=%llu\n",
    (unsigned long long)c.chunks,(unsigned long long)c.fences_issued,
    (unsigned long long)c.fences_signalled,(unsigned long long)c.kicks,(unsigned long long)count);
  { std::lock_guard lock(counts_mutex);
    for (const auto& [name,n]:hook_counts)
      std::fprintf(stderr,"HEADLESS hook %s count=%llu\n",name.c_str(),(unsigned long long)n);
  }
  rex::FlushLogging();
  std::_Exit(c.chunks && c.fences_issued && c.fences_signalled && c.kicks?0:72);
#endif // desktop bounded harness
}
}