// Ex-Box addition: one joined producer; only the UI owner calls start/take/join.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>
namespace exbox {
template<class Result> class StartupJob {
 std::thread worker_;std::atomic<bool> done_{false};std::atomic<std::uint64_t> completed_ms_{0};Result result_{};bool active_=false;
public:
 ~StartupJob(){join();}
 StartupJob()=default;StartupJob(const StartupJob&)=delete;
 template<class F> bool start(F work){
  if(active_)return false;
  done_.store(false,std::memory_order_relaxed);completed_ms_.store(0,std::memory_order_relaxed);active_=true;
  try {worker_=std::thread([this,work]{
   try {result_=work();} catch(...) {result_=Result{};}
   completed_ms_.store(std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),std::memory_order_release);
   done_.store(true,std::memory_order_release);
  });} catch(...) {active_=false;return false;}
  return true;
 }
 bool take(Result& out){if(!active_||!done_.load(std::memory_order_acquire))return false;worker_.join();out=std::move(result_);active_=false;return true;}
 bool active()const{return active_;}
 std::uint64_t completion_ms()const{return completed_ms_.load(std::memory_order_acquire);}
 void join(){if(worker_.joinable())worker_.join();active_=false;}
};
}
