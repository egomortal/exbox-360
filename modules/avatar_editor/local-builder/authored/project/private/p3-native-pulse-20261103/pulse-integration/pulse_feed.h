#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>
namespace av2::pulse {
class Feed {
 std::mutex mutex_;std::condition_variable cv_;uint64_t sequence_=0,consumed_=0;bool stopped_=false;int error_=0;
 public:
 bool publish(int result){std::lock_guard lock(mutex_);if(stopped_)return false;if(result){error_=result;stopped_=true;}else ++sequence_;cv_.notify_all();return !stopped_;}
 uint64_t wait(){std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(20),[&]{return stopped_||sequence_!=consumed_;});if(stopped_||sequence_==consumed_)return 0;consumed_=sequence_;return consumed_;}
 void stop(){std::lock_guard lock(mutex_);stopped_=true;cv_.notify_all();}
 int error(){std::lock_guard lock(mutex_);return error_;}
 bool stopped(){std::lock_guard lock(mutex_);return stopped_;}
};
}
