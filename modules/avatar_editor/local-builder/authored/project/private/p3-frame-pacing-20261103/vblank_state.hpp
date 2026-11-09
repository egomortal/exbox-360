// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
namespace av2::vblank {
struct Tick { uint32_t counter; uint64_t ordinal; uint32_t callback, data; uint64_t generation; };
struct Snapshot { uint64_t ticks=0, skipped=0, entered=0, returned=0; uint32_t callback=0,data=0; uint64_t generation=0; bool stopping=false; };
// A single worker owns advance(); registration and stop may run on other
// threads. An admitted callback may finish after unregister; stop+join is the
// required quiescence barrier before unloading code or freeing runtime memory.
class State {
 public:
  bool start(uint64_t now_ns,uint32_t rate_millihz) {
    std::lock_guard lock(mutex_);
    if(started_ || rate_millihz<1000 || rate_millihz>240000)return false;
    epoch_=now_ns;rate_=rate_millihz;started_=true;return true;
  }
  bool replace(uint32_t callback,uint32_t data) {
    std::lock_guard lock(mutex_);if(s_.stopping)return false;
    s_.callback=callback;s_.data=callback?data:0;++s_.generation;return true;
  }
  // Count all elapsed vblanks but cap guest catch-up work to four callbacks.
  // Discarded callbacks are explicit in skipped; no guest wait is signalled.
  uint32_t advance(uint64_t now_ns) {
    std::lock_guard lock(mutex_);if(!started_||s_.stopping||now_ns<epoch_)return 0;
    const uint64_t elapsed=now_ns-epoch_;
    const uint64_t expected=(elapsed/1000000000ull)*rate_/1000+
        (((elapsed/1000000000ull)*rate_)%1000*1000000000ull+(elapsed%1000000000ull)*rate_)/1000000000000ull;
    if(expected<=s_.ticks)return 0;
    const uint64_t due=expected-s_.ticks;const uint32_t count=uint32_t(std::min<uint64_t>(due,4));
    s_.skipped+=due-count;s_.ticks=expected;next_=expected-count+1;remaining_=count;return count;
  }
  uint32_t display_pulse(uint64_t sequence) {
    std::lock_guard lock(mutex_);
    if(!started_ || s_.stopping || sequence<=display_sequence_) return 0;
    const uint64_t delta=sequence-display_sequence_; display_sequence_=sequence;
    const uint32_t count=uint32_t(std::min<uint64_t>(delta,4));
    s_.skipped+=delta-count; s_.ticks+=delta;
    next_=s_.ticks-count+1; remaining_=count; return count;
  }
  std::optional<Tick> next() {
    std::lock_guard lock(mutex_);if(!remaining_||s_.stopping)return {};
    --remaining_;const uint64_t n=next_++;
    return Tick{uint32_t(n),n,s_.callback,s_.data,s_.generation};
  }
  void entered(){std::lock_guard lock(mutex_);++s_.entered;}
  void returned(){std::lock_guard lock(mutex_);++s_.returned;}
  void stop(){std::lock_guard lock(mutex_);s_.stopping=true;s_.callback=0;s_.data=0;++s_.generation;remaining_=0;}
  Snapshot snapshot() const {std::lock_guard lock(mutex_);return s_;}
 private:
  mutable std::mutex mutex_;Snapshot s_;bool started_=false;uint64_t epoch_=0,next_=0,display_sequence_=0;uint32_t rate_=0,remaining_=0;
};
}
