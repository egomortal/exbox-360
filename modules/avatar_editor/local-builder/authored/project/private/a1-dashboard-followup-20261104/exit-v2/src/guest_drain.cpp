#include "guest_drain.h"
#include "guest_creation.h"
#include "normal_exit_api.h"
#include <rex/system/xthread.h>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
namespace {
struct Tracked {
 rex::system::object_ref<rex::system::XThread> thread;
 bool creating=true,started=false,completed=false;
};
std::mutex tracked_mutex;
std::mutex drain_mutex;
std::vector<Tracked> tracked;
bool admission_closed=false,lifecycle_failed=false;
auto find_thread(rex::system::XThread*t){return std::find_if(tracked.begin(),tracked.end(),[&](const auto&e){return e.thread.get()==t;});}
}
bool av2_native_begin_guest_creation(rex::system::XThread*thread){
 if(!av2_normal_exit_active()||!thread->is_guest_thread())return true;
 std::lock_guard lock(tracked_mutex);
 if(admission_closed)return false;
 if(find_thread(thread)!=tracked.end()){lifecycle_failed=true;return false;}
 tracked.push_back({rex::system::retain_object(thread),true,false});return true;
}
void av2_native_finish_guest_creation(rex::system::XThread*thread,bool success){
 if(!av2_normal_exit_active()||!thread->is_guest_thread())return;
 rex::system::object_ref<rex::system::XThread> retired;
 {
  std::lock_guard lock(tracked_mutex);auto it=find_thread(thread);
  if(it==tracked.end()||!it->creating){lifecycle_failed=true;return;}
  if(success){it->creating=false;return;}
  // Only a never-runnable failure can be removed. An unexpected failure after
  // host-thread creation remains retained and permanently refuses finalization.
  if(thread->thread()){lifecycle_failed=true;return;}
  retired=std::move(it->thread);tracked.erase(it);
 }
 retired.reset(); // Never release an owner while holding the tracker mutex.
}
void av2_native_guest_start_gate(rex::system::XThread*thread){
 if(!av2_normal_exit_active()||!thread->is_guest_thread())return;
 bool park=false;
 {
  std::lock_guard lock(tracked_mutex);auto it=find_thread(thread);
  if(it==tracked.end()||it->creating){lifecycle_failed=true;park=true;}
  else if(admission_closed)park=true;
  else it->started=true;
 }
 // No tracker/kernel/import lock survives into the permanent park. The callback
 // calls this before TLS binding, naming, APCs or any guest-resource access.
 if(park)av2_normal_exit_park_unstarted(thread);
}
void av2_native_close_guest_admission(){std::lock_guard lock(tracked_mutex);admission_closed=true;}
void av2_native_track_guest(rex::system::XThread*thread){
 if(!av2_normal_exit_active()||!thread->is_guest_thread())return;
 std::lock_guard lock(tracked_mutex);
 const auto it=find_thread(thread);
 if(it==tracked.end()||it->creating||!it->started)lifecycle_failed=true;
}
rex::thread::WaitResult av2_native_poll_guest_completion(rex::system::XThread*thread,std::chrono::milliseconds timeout){
 std::unique_lock drain_lock(drain_mutex,std::try_to_lock);
 if(!drain_lock.owns_lock())return rex::thread::WaitResult::kFailed;
 rex::system::object_ref<rex::system::XThread> retained;
 {
  std::lock_guard lock(tracked_mutex);auto it=find_thread(thread);
  if(it==tracked.end()||it->creating||lifecycle_failed)return rex::thread::WaitResult::kFailed;
  if(it->completed)return rex::thread::WaitResult::kSuccess;
  retained=rex::system::retain_object(thread);
 }
 const auto result=rex::thread::Wait(retained->thread(),false,timeout);
 if(result==rex::thread::WaitResult::kSuccess){
  std::lock_guard lock(tracked_mutex);auto it=find_thread(thread);
  if(it==tracked.end()){lifecycle_failed=true;return rex::thread::WaitResult::kFailed;}
  it->completed=true;
 }else if(result!=rex::thread::WaitResult::kTimeout){
  std::lock_guard lock(tracked_mutex);lifecycle_failed=true;
 }
 return result;
}
bool av2::native::drain_guest_threads(std::chrono::milliseconds timeout){
 // The backend successful Wait consumes thread completion (join/semaphore cleanup).
 // Exactly one frontend may consume these handles. Guests never use this mutex.
 std::unique_lock drain_lock(drain_mutex,std::try_to_lock);
 if(!drain_lock.owns_lock())return false;
 const auto deadline=std::chrono::steady_clock::now()+timeout;
 for(;;){
  if(av2_normal_exit_refused())return false;
  std::vector<rex::system::object_ref<rex::system::XThread>> pending;
  bool creation_pending=false;
  {
   std::lock_guard lock(tracked_mutex);
   if(!admission_closed||lifecycle_failed)return false;
   for(const auto&entry:tracked){
    if(entry.creating){creation_pending=true;continue;}
    if(entry.completed||av2_normal_exit_parked(entry.thread.get()))continue;
    if(!entry.thread->thread()){lifecycle_failed=true;return false;}
    pending.push_back(entry.thread); // Retain across unlocked backend wait/join.
   }
  }
  if(pending.empty()&&!creation_pending)return !av2_normal_exit_refused();
  for(const auto&thread:pending){
   if(av2_normal_exit_parked(thread.get()))continue;
   // No tracker lock across post_execution/pthread_join. A zero-time poll may
   // still join after success; timeout bounds polling, not backend join latency.
   const auto result=rex::thread::Wait(thread->thread(),false,std::chrono::milliseconds(0));
   if(result!=rex::thread::WaitResult::kSuccess&&result!=rex::thread::WaitResult::kTimeout)return false;
   if(result==rex::thread::WaitResult::kSuccess){
    std::lock_guard lock(tracked_mutex);
    auto it=find_thread(thread.get());
    if(it==tracked.end()||it->creating){lifecycle_failed=true;return false;}
    it->completed=true; // Never consume this completed handle a second time.
   }
  }
  {
   std::lock_guard lock(tracked_mutex);
   bool complete=!lifecycle_failed;
   for(const auto&entry:tracked)
    if(entry.creating||(!entry.completed&&!av2_normal_exit_parked(entry.thread.get())))complete=false;
   if(complete)return !av2_normal_exit_refused();
  }
  if(std::chrono::steady_clock::now()>=deadline)return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
}
