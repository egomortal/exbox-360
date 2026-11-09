// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "vblank_state.hpp"
#include <chrono>
#include <functional>
#include <memory>
namespace rex { class Runtime; }
namespace av2::vblank {
// Counter publication runs before each interrupt on the bound worker. It must
// write only the renderer's verified vblank counter, never fence/wait state.
using CounterSink=std::function<void(uint32_t)>;
// Returns a display sequence or zero after a bounded timeout. Never synthesize ticks.
using DisplayWait=std::function<uint64_t()>;
using LogSink=std::function<void(const char*)>;
class Source {
 public:
  Source(rex::Runtime&,CounterSink,DisplayWait,LogSink={});
  ~Source();
  Source(const Source&)=delete;Source& operator=(const Source&)=delete;
  bool start(); // after Runtime::Setup; independent of callback registration
  bool register_callback(uint32_t callback,uint32_t user_data);
  void request_stop();
  // False means the guest callback is still active. Caller MUST retain runtime
  // and module storage and use the blocked-thread process-exit path.
  bool stop_and_join(std::chrono::milliseconds timeout);
  Snapshot snapshot() const;
 private:
  struct Impl;std::shared_ptr<Impl> impl_;
};
}
