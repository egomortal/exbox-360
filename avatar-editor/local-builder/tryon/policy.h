#pragma once
#include <array>
#include <cstdint>

namespace closet_tryon {
using Metadata = std::array<std::uint8_t,1000>;
struct Scope {
  std::uint32_t context=0, loop=0, scene=0;
  std::uint64_t thread=0, epoch=0;
  bool operator==(const Scope& b) const noexcept {
    return context==b.context && loop==b.loop && scene==b.scene &&
           thread==b.thread && epoch==b.epoch;
  }
  explicit operator bool() const noexcept {return context && loop && scene && thread && epoch;}
};

// Bookkeeping only. All pointers, clocks and guest calls belong to the adapter.
class Pending {
 public:
  static constexpr std::uint64_t delay_ns=125000000;
  void cancel() noexcept {active_=false;}
  bool active() const noexcept {return active_;}
  bool observe(Scope scope, const Metadata& target, const Metadata& baseline,
               std::uint64_t now, bool eligible) noexcept {
    if (!eligible || !scope || target==baseline) {cancel();return false;}
    if (!active_ || !(scope==scope_) || target!=target_ || baseline!=baseline_) {
      scope_=scope;target_=target;baseline_=baseline;since_=now;active_=true;
    } else if (now<since_) since_=now;
    if (now-since_>=delay_ns) {cancel();return false;}
    return true;
  }
  // Detach before the adapter invokes the original function; never hold a lock
  // or leave a second request queued while guest code runs.
  bool take(Scope scope, const Metadata& working, const Metadata& baseline,
            std::uint64_t now, bool running) noexcept {
    if (!active_) return false;
    if (!running || !(scope==scope_) || working!=target_ || baseline!=baseline_) {
      cancel();return false;
    }
    if (now<since_) {since_=now;return false;}
    if (now-since_<delay_ns) return false;
    cancel();return true;
  }
 private:
  Scope scope_{};
  Metadata target_{},baseline_{};
  std::uint64_t since_=0;
  bool active_=false;
};
}
