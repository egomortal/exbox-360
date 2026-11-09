#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
namespace av2::native {
// Silent PCM output still consumes one 256-sample / 48 kHz frame at a time.
// No PCM allocation or guest-memory reads: the decoder and callback producer
// remain the original runtime. The callback returns one producer credit.
class PacedFrames {
 public:
  explicit PacedFrames(std::function<void()> consumed);
  ~PacedFrames();
  PacedFrames(const PacedFrames&)=delete;
  PacedFrames& operator=(const PacedFrames&)=delete;
  bool submit(); // false only on shutdown or an invalid overfull producer.
 private:
  void run();
  std::function<void()> consumed_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::size_t pending_=0;
  bool stopping_=false;
  std::thread worker_;
};
}
