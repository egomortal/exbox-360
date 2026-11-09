#include "paced_frames.h"
#include <cstdint>
#include <utility>
namespace av2::native {
PacedFrames::PacedFrames(std::function<void()> consumed)
    : consumed_(std::move(consumed)),worker_([this]{run();}) {}
PacedFrames::~PacedFrames() {
  { std::lock_guard lock(mutex_); stopping_=true; }
  changed_.notify_all();
  worker_.join(); // No callback survives the driver's semaphore lifetime.
}
bool PacedFrames::submit() {
  { std::lock_guard lock(mutex_);
    if(stopping_ || pending_>=64) return false;
    ++pending_;
  }
  changed_.notify_one();
  return true;
}
void PacedFrames::run() {
  using Clock=std::chrono::steady_clock;
  using FrameDuration=std::chrono::duration<std::int64_t,std::ratio<256,48000>>;
  std::unique_lock lock(mutex_);
  while(!stopping_) {
    changed_.wait(lock,[this]{return stopping_ || pending_!=0;});
    if(stopping_) break;
    auto epoch=Clock::now();
    std::int64_t frame=0;
    while(pending_ && !stopping_) {
      // Derive from the epoch each time, avoiding accumulated fractional-ns
      // truncation. If scheduling stalls, restart pacing instead of bursting.
      const auto deadline=epoch+std::chrono::duration_cast<Clock::duration>(FrameDuration{++frame});
      if(changed_.wait_until(lock,deadline,[this]{return stopping_;})) break;
      --pending_;
      lock.unlock(); consumed_(); lock.lock();
      if(Clock::now()>deadline+FrameDuration{1}) { epoch=Clock::now(); frame=0; }
    }
  }
}
}
