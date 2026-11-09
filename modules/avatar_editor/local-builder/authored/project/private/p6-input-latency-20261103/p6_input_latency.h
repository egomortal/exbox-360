#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
namespace av2::input_latency {
struct Snapshot { uint64_t total=0,interval=0,quantile_samples=0,dropped=0,clock_failures=0,min_ns=0,max_ns=0,mean_ns=0,p50_ns=0,p90_ns=0,p99_ns=0; };
class Collector {
 static constexpr size_t Pending=256,Window=1024;
 std::mutex mutex_;
 std::array<uint64_t,Pending> pending_{};
 std::array<uint64_t,Window> samples_{};
 size_t head_=0,pending_count_=0,sample_cursor_=0,sample_count_=0;
 uint64_t total_=0,interval_=0,sum_=0,min_=0,max_=0,dropped_=0,clock_failures_=0;
 public:
 void changed(uint64_t now){std::lock_guard<std::mutex> g(mutex_);if(!now){++clock_failures_;return;}if(pending_count_==Pending){head_=(head_+1)%Pending;--pending_count_;++dropped_;}pending_[(head_+pending_count_)%Pending]=now;++pending_count_;}
 void present(uint64_t now){std::lock_guard<std::mutex> g(mutex_);if(!now){++clock_failures_;return;}while(pending_count_){uint64_t change=pending_[head_];if(change>now)break;head_=(head_+1)%Pending;--pending_count_;uint64_t delay=now-change;++total_;++interval_;sum_+=delay;if(interval_==1||delay<min_)min_=delay;if(delay>max_)max_=delay;samples_[sample_cursor_]=delay;sample_cursor_=(sample_cursor_+1)%Window;if(sample_count_<Window)++sample_count_;}}
 Snapshot snapshot(){std::array<uint64_t,Window> sorted{};Snapshot s;size_t count=0;{
  std::lock_guard<std::mutex> g(mutex_);s.total=total_;s.interval=interval_;s.quantile_samples=sample_count_;s.dropped=dropped_;s.clock_failures=clock_failures_;s.min_ns=interval_?min_:0;s.max_ns=interval_?max_:0;s.mean_ns=interval_?sum_/interval_:0;count=sample_count_;std::copy_n(samples_.begin(),count,sorted.begin());interval_=sum_=min_=max_=0;sample_count_=sample_cursor_=0;
 }if(count){std::sort(sorted.begin(),sorted.begin()+count);auto percentile=[&](size_t p){return sorted[(count*p+99)/100-1];};s.p50_ns=percentile(50);s.p90_ns=percentile(90);s.p99_ns=percentile(99);}return s;}
};
}
extern "C" void av2_input_present(uint64_t monotonic_ns);
extern "C" void av2_input_latency_snapshot(av2::input_latency::Snapshot*);
