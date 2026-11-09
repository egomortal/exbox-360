#include "scheduler.h"
#include <algorithm>
#include <cerrno>
#include <limits>

namespace av2::platform {
int TimerQueue::schedule(std::uint64_t deadline, std::uint64_t period,
                         Callback callback, void* argument, std::uint64_t& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback) return EINVAL;
  if (serial_ == std::numeric_limits<std::uint64_t>::max()) return EOVERFLOW;
  for (auto& item : items_) if (!item.token) {
    token = ++serial_;
    item = {token, deadline, period, callback, argument};
    return 0;
  }
  return EAGAIN;
}
bool TimerQueue::cancel(std::uint64_t token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!token) return false;
  for (auto& item : items_) if (item.token == token) { item = {}; return true; }
  return false;
}
int TimerQueue::dispatch(std::uint64_t now, std::size_t& fired) {
  fired = 0;
  // Freeze the token set: a callback scheduling another timer cannot cause
  // that new callback to run in the same dispatch pass.
  std::array<std::uint64_t, 64> tokens{};
  { std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < items_.size(); ++i) tokens[i] = items_[i].token;
  }
  for (std::size_t i = 0; i < items_.size(); ++i) {
    Item call;
    { std::lock_guard<std::mutex> lock(mutex_);
      auto& item = items_[i];
      if (!tokens[i] || item.token != tokens[i] || item.deadline > now) continue;
      call = item;
      if (item.period) {
        const auto remainder = (now - item.deadline) % item.period;
        const auto delta = item.period - remainder;
        if (delta > std::numeric_limits<std::uint64_t>::max() - now) item = {};
        else item.deadline = now + delta;
      } else item = {};
    }
    call.callback(call.argument); ++fired;
  }
  return 0;
}
int AlertableWait::bind_owner() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (bound_) return EBUSY;
  owner_ = pthread_self(); bound_ = true;
  return 0;
}
int AlertableWait::post(Callback callback, void* argument) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback) return EINVAL;
  if (!bound_ || stopped_) return ECANCELED;
  if (count_ == pending_.size()) return EAGAIN;
  pending_[(first_ + count_) % pending_.size()] = {callback, argument}; ++count_;
  return 0;
}
void AlertableWait::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  stopped_ = true;
}
int AlertableWait::wait(std::uint64_t deadline, bool alertable, Result& result) {
  for (;;) {
    Call call;
    { std::lock_guard<std::mutex> lock(mutex_);
      if (!bound_ || !pthread_equal(owner_, pthread_self())) return EPERM;
      if (stopped_) { result = Result::stopped; return 0; }
      if (alertable && count_) {
        call = pending_[first_]; first_ = (first_ + 1) % pending_.size(); --count_;
      }
    }
    if (call.callback) { call.callback(call.argument); result = Result::alerted; return 0; }
    std::uint64_t now = 0;
    int error = monotonic_ticks(now);
    if (error) return error;
    if (now >= deadline) { result = Result::elapsed; return 0; }
    // Bounded stop/APC latency without Linux realtime signals or forced cancel.
    error = sleep_ns(std::min<std::uint64_t>(deadline - now, 1000000));
    if (error) return error;
  }
}
} // namespace av2::platform
