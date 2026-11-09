#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
namespace av2::diagnostics {
// Inputs are snapshots: never query a possibly blocked guest or GPU from here.
struct Snapshot { uint64_t presents, submit; int fence_state; };
using ReadSnapshot = Snapshot (*)();
using Report = void (*)(Snapshot, bool hang);
using RequestStop = void (*)();
struct Timing { uint64_t report_ms=2000, stall_ms=20000, poll_ms=50; };
class HangMonitor {
 public:
  HangMonitor()=default;
  ~HangMonitor(){stop();}
  HangMonitor(const HangMonitor&)=delete;
  HangMonitor& operator=(const HangMonitor&)=delete;
  bool start(ReadSnapshot read,Report report,RequestStop request,Timing timing={});
  void stop(); // owner calls before tearing down the callback providers
 private:
  std::atomic<bool> stopping_{false};
  std::thread worker_;
};
// Missing file is off. Legacy empty file remains on. Explicit zero is off.
// Reject malformed values rather than silently enabling a synchronous wait.
bool parse_gpu_sync(const char* bytes,bool exists,bool& enabled);
}
