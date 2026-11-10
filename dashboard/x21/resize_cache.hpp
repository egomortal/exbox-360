// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "filtered_limits.h"
#include "resize_adapter.hpp"
#include "resize_gpu.h"
#include <functional>
namespace resize_adapter {
// Upload callback may reuse immutable residency. It must return a lease keeping
// the VkBuffer allocation alive; the caller charges that allocation to the SAME
// shared ledger. No public offsets into another helper's private cache are used.
struct SourceLease {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0,bytes=0;std::shared_ptr<void> keep;};
class Cache {
 struct Entry {Key key;ExboxResizeJob*job=nullptr;SourceLease source;VkFence fence=VK_NULL_HANDLE;uint64_t used=0,bytes=0;bool pending=true,inflight=false,stale=false,pinned=false;};
 struct State {ExboxResize*gpu=nullptr;ExboxResizeScratch*scratch=nullptr;std::vector<Entry> entries;uint64_t clock=0;};
 std::unique_ptr<State> state;
 static constexpr size_t limit=EXBOX_FILTERED_MAX_SLOTS;
 static constexpr uint64_t byte_limit=64ull*1024*1024;
 static bool idle(const Entry&e){return !e.inflight&&!e.pinned;}
 void erase(size_t i){auto&s=*state;if(!exbox_resize_release(s.entries[i].job))throw std::logic_error("resize lease still in flight");s.entries.erase(s.entries.begin()+i);}
public:
 explicit Cache(ExboxResize*g,ExboxResizeScratch*scratch=nullptr):state(new State){state->gpu=g;state->scratch=scratch;}
 Cache(const Cache&)=delete;Cache&operator=(const Cache&)=delete;
 // Shutdown must call close after known fence completion. If the caller cannot
 // prove completion, retain/quarantine the entire state, including source leases.
 // The borrowed device/helper/ledger must likewise remain alive in that case.
 ~Cache(){if(!close())(void)state.release();}
 using Upload=std::function<SourceLease(const Prepared&)>;
 VkBuffer find(const Key&key){if(!state)return VK_NULL_HANDLE;for(auto&e:state->entries)if(!e.stale&&e.key==key){e.used=++state->clock;e.pinned=true;return exbox_resize_output(e.job,nullptr);}return VK_NULL_HANDLE;}
 // Prepared is an owned immutable publication packet, built on the SDL owner.
 // This presenter-side cache never receives SDL pointers or invokes SDL.
 VkBuffer request(const Key&key,const Prepared&p,const Upload&upload){
  if(!state||!state->gpu||!key.owner||!key.id||!key.generation||key.filter!=1)return VK_NULL_HANDLE;
  auto&s=*state;for(auto&e:s.entries)if(!e.stale&&e.key==key){e.used=++s.clock;e.pinned=true;return exbox_resize_output(e.job,nullptr);}
  if(key.dw<1||key.dh<1||uint64_t(key.dw)*key.dh*4>byte_limit)return VK_NULL_HANDLE;
  uint64_t bytes=uint64_t(key.dw)*key.dh*4,total=bytes;for(auto&e:s.entries)total+=e.bytes;
  std::vector<size_t> victims,order;for(size_t i=0;i<s.entries.size();i++)if(idle(s.entries[i]))order.push_back(i);
  std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return s.entries[a].used<s.entries[b].used;});size_t count=s.entries.size()+1;
  for(size_t i:order){if(count<=limit&&total<=byte_limit)break;victims.push_back(i);count--;total-=s.entries[i].bytes;}
  if(count>limit||total>byte_limit)return VK_NULL_HANDLE;
  // All fallible CPU preparation/allocation is staged before LRU eviction.
  if(key.w<1||key.h<1||p.sw!=uint32_t(key.w)||p.sh!=uint32_t(key.h)||p.dw!=uint32_t(key.dw)||p.dh!=uint32_t(key.dh)||p.pixels.size()!=uint64_t(p.sw)*p.sh)return VK_NULL_HANDLE;
  SourceLease lease;try{lease=upload(p);}catch(...){return VK_NULL_HANDLE;}if(!lease.buffer||!lease.keep)return VK_NULL_HANDLE;
  auto*j=s.scratch?exbox_resize_prepare_shared(s.gpu,s.scratch,lease.buffer,lease.offset,lease.bytes,p.sw,p.sh,p.dw,p.dh,p.alpha,p.taps.data(),p.taps.size()):exbox_resize_prepare(s.gpu,lease.buffer,lease.offset,lease.bytes,p.sw,p.sh,p.dw,p.dh,p.alpha,p.taps.data(),p.taps.size());if(!j)return VK_NULL_HANDLE;
  try{s.entries.reserve(limit+1);}catch(...){exbox_resize_release(j);return VK_NULL_HANDLE;}
  std::sort(victims.rbegin(),victims.rend());for(size_t i:victims)erase(i);
  Entry e;e.key=key;e.job=j;e.source=std::move(lease);e.bytes=bytes;e.used=++s.clock;e.pinned=true;s.entries.push_back(std::move(e));return exbox_resize_output(j,nullptr);
 }
 // Call before downstream draw commands for every referenced output. A new miss
 // records its two GPU passes; a cache hit only acquires a completion lease.
 bool record(const Key&key,VkCommandBuffer command){if(!state||!command)return false;for(auto&e:state->entries)if(!e.stale&&e.key==key){if(e.inflight)return !e.fence;if(e.pending){if(!exbox_resize_record(e.job,command))return false;e.pending=false;}else if(!exbox_resize_begin_use(e.job))return false;e.inflight=true;e.fence=VK_NULL_HANDLE;return true;}return false;}
 // The fence covers the filter AND all later reads of the cached output.
 void submitted(VkFence fence){if(!state||!fence)return;for(auto&e:state->entries)if(e.inflight&&!e.fence)e.fence=fence;}
 void poll(){if(!state)return;for(auto&e:state->entries)if(e.inflight&&e.fence&&exbox_resize_complete(e.job,e.fence)){e.inflight=false;e.pinned=false;e.fence=VK_NULL_HANDLE;e.source={};}for(size_t i=state->entries.size();i-->0;)if(state->entries[i].stale&&idle(state->entries[i]))erase(i);}
 // Only after discarding the entire unrecorded packet; never cancel GPU-recorded work.
 void cancel_unrecorded_requests(){if(!state)return;for(auto&e:state->entries)if(!e.inflight)e.pinned=false;poll();}
 // Session success means every downstream use in this serial submission completed.
 // Unknown completion must stop further reuse and retain/quarantine this cache.
 void completed_after_wait(){if(!state)return;for(auto&e:state->entries)if(e.inflight&&!e.fence&&exbox_resize_complete_after_wait(e.job)){e.inflight=false;e.pinned=false;e.source={};}poll();}
 void invalidate(uint64_t owner){if(!state)return;for(auto&e:state->entries)if(e.key.owner==owner)e.stale=true;poll();}
 bool close(){if(!state)return true;for(auto&e:state->entries)if(e.inflight||e.pinned)return false;while(!state->entries.empty())erase(state->entries.size()-1);state.reset();return true;}
 bool contains(const Key&key)const{if(!state)return false;for(auto&e:state->entries)if(!e.stale&&e.key==key)return true;return false;}
 // Roll back only newly prepared, unrecorded jobs after the owner unbound slots.
 bool discard_pending(const Key&key){if(!state)return false;for(size_t i=0;i<state->entries.size();i++){auto&e=state->entries[i];if(e.key==key){if(e.inflight||e.pinned||!e.pending)return false;erase(i);return true;}}return false;}
 size_t size()const{return state?state->entries.size():0;}
};
}
