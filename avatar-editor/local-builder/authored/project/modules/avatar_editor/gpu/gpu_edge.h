#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace av2 {
// The backend sees guest words, never a platform pointer. Implementations must
// preserve big-endian access and validate guest ranges before dereferencing.
class GpuGuestMemory {
 public:
  virtual ~GpuGuestMemory() = default;
  virtual uint32_t read32(uint32_t address) = 0;
  virtual void write32(uint32_t address, uint32_t value) = 0;
};
struct GpuEdgeCounters {
  uint64_t chunks=0, fences_issued=0, fences_signalled=0, kicks=0, gamma_updates=0;
};
class GpuEdge {
 public:
  virtual ~GpuEdge() = default;
  virtual void chunk_queued(uint32_t device) = 0;
  virtual void fence_issued(uint32_t device,uint32_t fence,uint32_t ring_position) = 0;
  virtual void primary_kick(uint32_t device,uint32_t write_index) = 0;
  virtual void gamma(const std::array<uint16_t,768>& ramp) = 0;
  virtual void tile_size(uint32_t pixels) = 0;
  virtual uint32_t tile_size() const = 0;
  virtual GpuEdgeCounters counters() const = 0;
};
// Implements the working Windows editor's CPU-completion edge semantics, not
// rendering. Vulkan must retire the corresponding submission before signalling.
class NullGpuEdge final : public GpuEdge {
 public:
  explicit NullGpuEdge(GpuGuestMemory& memory): memory_(memory) {}
  void chunk_queued(uint32_t device) override;
  void fence_issued(uint32_t device,uint32_t fence,uint32_t ring_position) override;
  void primary_kick(uint32_t device,uint32_t write_index) override;
  void gamma(const std::array<uint16_t,768>& ramp) override;
  void tile_size(uint32_t pixels) override;
  uint32_t tile_size() const override;
  GpuEdgeCounters counters() const override;
  std::array<uint16_t,768> gamma_ramp() const;
 private:
  GpuGuestMemory& memory_;
  mutable std::mutex mutex_;
  GpuEdgeCounters counters_{};
  std::array<uint16_t,768> ramp_{};
  uint32_t tile_size_=64;
};
}