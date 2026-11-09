#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
namespace av2::exit_handoff {
enum class Route : uint32_t { title=1, launch, launch_ex, discard, main_exit, main_terminate, not_started };
enum class Commit { parked, already_parked, exhausted };
template<size_t Capacity> class Registry {
 struct Slot { std::atomic<uintptr_t> identity{0}; std::atomic<bool> ready{false}; Route route{}; int status{}; };
 std::array<Slot,Capacity> slots_{};
 std::atomic<size_t> first_{Capacity};
 std::atomic<bool> blocked_{false},failure_{false};
 public:
 struct Request { bool pending{},blocked{},failed{}; Route route{}; int status{}; };
 // Caller must make no guest/resource access after this commit and enter its
 // permanent native park. This registry never frees or reuses a slot.
 Commit commit(uintptr_t identity, Route route, int status) noexcept {
  if(!identity){blocked_.store(true,std::memory_order_release);return Commit::exhausted;}
  for(size_t i=0;i<Capacity;++i){
   auto existing=slots_[i].identity.load(std::memory_order_acquire);
   if(existing==identity){
    if(!slots_[i].ready.load(std::memory_order_acquire)){blocked_.store(true,std::memory_order_release);return Commit::exhausted;}
    return Commit::already_parked;
   }
   if(existing)continue;
   if(!slots_[i].identity.compare_exchange_strong(existing,identity,std::memory_order_acq_rel)){
    --i;continue;
   }
   slots_[i].route=route;slots_[i].status=status;
   if(status)failure_.store(true,std::memory_order_release);
   slots_[i].ready.store(true,std::memory_order_release);
   size_t empty=Capacity;first_.compare_exchange_strong(empty,i,std::memory_order_release,std::memory_order_relaxed);
   return Commit::parked;
  }
  blocked_.store(true,std::memory_order_release);return Commit::exhausted;
 }
 bool parked(uintptr_t identity)const noexcept {
  if(!identity)return false;
  for(const auto&s:slots_)if(s.ready.load(std::memory_order_acquire)&&s.identity.load(std::memory_order_relaxed)==identity)return true;
  return false;
 }
 Request request()const noexcept {
  const size_t i=first_.load(std::memory_order_acquire);
  Request r;r.blocked=blocked_.load(std::memory_order_acquire);r.failed=failure_.load(std::memory_order_acquire);
  if(i<Capacity){r.pending=true;r.route=slots_[i].route;r.status=slots_[i].status;}
  return r;
 }
 bool may_finalize()const noexcept {auto r=request();return r.pending&&!r.blocked&&!r.failed;}
};
inline bool intercept_thread_completion(bool native_enabled,bool guest,bool main)noexcept{return native_enabled&&guest&&main;}
}
