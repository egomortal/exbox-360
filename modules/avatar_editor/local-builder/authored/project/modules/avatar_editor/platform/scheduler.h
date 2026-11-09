#pragma once
#include "ps5_platform.h"
#include <array>

namespace av2::platform {
// Bounded cooperative queue. The runtime must drive dispatch on its timer
// thread; this object starts no threads and never invokes guest code in signals.
class TimerQueue {
 public:
  using Callback = void (*)(void*);
  int schedule(std::uint64_t deadline_ns, std::uint64_t period_ns,
               Callback callback, void* argument, std::uint64_t& token);
  bool cancel(std::uint64_t token);
  // At most 64 callbacks. Recurrence skips missed intervals without a storm.
  // Cancellation cannot revoke a callback already selected for dispatch.
  int dispatch(std::uint64_t now_ns, std::size_t& fired);
 private:
  struct Item {
    std::uint64_t token = 0, deadline = 0, period = 0;
    Callback callback = nullptr;
    void* argument = nullptr;
  };
  std::mutex mutex_;
  std::array<Item, 64> items_{};
  std::uint64_t serial_ = 0;
};

class AlertableWait {
 public:
  using Callback = TimerQueue::Callback;
  enum class Result { elapsed, alerted, stopped };
  // Bind once from the guest thread; only that thread may run wait().
  int bind_owner();
  int post(Callback callback, void* argument);
  void stop();
  int wait(std::uint64_t deadline_ns, bool alertable, Result& result);
 private:
  struct Call { Callback callback = nullptr; void* argument = nullptr; };
  std::mutex mutex_;
  std::array<Call, 32> pending_{};
  std::size_t first_ = 0, count_ = 0;
  pthread_t owner_{};
  bool bound_ = false, stopped_ = false;
};
} // namespace av2::platform
