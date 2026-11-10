// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "resize_cache.hpp"
#include <array>
namespace filtered_art {
using Key=resize_adapter::Key;
struct Request {Key key;std::shared_ptr<const resize_adapter::Prepared> prepared;};
struct Binding {Key key;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize bytes=0;uint32_t width=0,height=0,pitch=0;uint64_t generation=0;};
class Owner {
public:
 using Unbind=std::function<bool(unsigned)>;
 enum class Phase {Idle,Prepared,Recorded,Quarantined};
 struct Stats {uint64_t uploads=0,uploaded_bytes=0,hits=0,misses=0;};
private:
 struct Impl;
 struct Block {
  Impl*owner;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void*mapped=nullptr;VkDeviceSize bytes=0;ExboxGpuReservation reservation=EXBOX_GPU_RESERVATION_INIT;
  explicit Block(Impl*p):owner(p){}~Block();
 };
 struct Identity {Key key;VkBuffer buffer;uint64_t generation;};
 struct Impl {
  VkDevice device=VK_NULL_HANDLE;VkPhysicalDeviceMemoryProperties memory{};VkDeviceSize storage_limit=0;ExboxGpuBudget*budget=nullptr;ExboxResize*gpu=nullptr;ExboxResizeScratch*scratch=nullptr;
  PFN_vkCreateBuffer CreateBuffer=nullptr;PFN_vkDestroyBuffer DestroyBuffer=nullptr;PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements=nullptr;PFN_vkAllocateMemory AllocateMemory=nullptr;PFN_vkFreeMemory FreeMemory=nullptr;PFN_vkBindBufferMemory BindBufferMemory=nullptr;PFN_vkMapMemory MapMemory=nullptr;PFN_vkUnmapMemory UnmapMemory=nullptr;
  std::unique_ptr<resize_adapter::Cache> cache;Unbind unbind;Phase phase=Phase::Idle;Stats stats;uint64_t serial=0;
  std::vector<Binding> frame;std::vector<Key> fresh;std::vector<Identity> identities;
  ~Impl(){cache.reset();if(scratch)exbox_resize_scratch_destroy(scratch);if(gpu)exbox_resize_destroy(gpu);}
  bool unbind_all(){try{for(unsigned i=0;i<EXBOX_FILTERED_MAX_SLOTS;i++)if(!unbind(i))return false;}catch(...){return false;}return true;}
  void prune(){identities.erase(std::remove_if(identities.begin(),identities.end(),[&](const Identity&i){return !cache->contains(i.key);}),identities.end());}
  void rollback(){cache->cancel_unrecorded_requests();for(const auto&key:fresh)cache->discard_pending(key);fresh.clear();frame.clear();prune();}
  resize_adapter::SourceLease upload(const resize_adapter::Prepared&p){
   uint64_t bytes=uint64_t(p.pixels.size())*4;if(!bytes||bytes>64ull*1024*1024||bytes>storage_limit)return {};
   auto b=std::make_shared<Block>(this);b->bytes=bytes;
   VkBufferCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;ci.size=bytes;ci.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
   if(CreateBuffer(device,&ci,nullptr,&b->buffer)!=VK_SUCCESS)return {};
   VkMemoryRequirements req{};GetBufferMemoryRequirements(device,b->buffer,&req);if(req.size<bytes||!exbox_gpu_budget_reserve(budget,&b->reservation,req.size))return {};
   uint32_t type=UINT32_MAX;const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==unsigned(flags)){type=i;break;}
   if(type==UINT32_MAX)return {};
   VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=req.size;ai.memoryTypeIndex=type;
   if(AllocateMemory(device,&ai,nullptr,&b->memory)!=VK_SUCCESS||BindBufferMemory(device,b->buffer,b->memory,0)!=VK_SUCCESS||MapMemory(device,b->memory,0,bytes,0,&b->mapped)!=VK_SUCCESS)return {};
   std::memcpy(b->mapped,p.pixels.data(),size_t(bytes));stats.uploads++;stats.uploaded_bytes+=bytes;return {b->buffer,0,bytes,b};
  }
 };
 std::unique_ptr<Impl> impl;
 explicit Owner(std::unique_ptr<Impl>p):impl(std::move(p)){}
public:
 Owner(const Owner&)=delete;Owner&operator=(const Owner&)=delete;
 // All calls on the existing serial presenter owner. The callback must unbind
 // x1's filtered slots before any output can be evicted or destroyed.
 static std::unique_ptr<Owner> create(VkDevice device,const VkPhysicalDeviceMemoryProperties&memory,const VkPhysicalDeviceProperties&properties,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get,ExboxGpuBudget*budget,Unbind unbind){
  if(!get||!device||!budget||!unbind||memory.memoryTypeCount>32)return {};
  try{auto p=std::make_unique<Impl>();p->device=device;p->memory=memory;p->storage_limit=properties.limits.maxStorageBufferRange;p->budget=budget;p->unbind=std::move(unbind);
#define LOAD(n) p->n=reinterpret_cast<PFN_vk##n>(get(device,"vk" #n));if(!p->n)return {};
   LOAD(CreateBuffer) LOAD(DestroyBuffer) LOAD(GetBufferMemoryRequirements) LOAD(AllocateMemory) LOAD(FreeMemory) LOAD(BindBufferMemory) LOAD(MapMemory) LOAD(UnmapMemory)
#undef LOAD
   p->gpu=exbox_resize_create(device,&memory,&properties,flags,get,budget);if(!p->gpu)return {};p->scratch=exbox_resize_scratch_create(p->gpu);if(!p->scratch)return {};p->cache=std::make_unique<resize_adapter::Cache>(p->gpu,p->scratch);p->frame.reserve(EXBOX_FILTERED_MAX_SLOTS);p->fresh.reserve(EXBOX_FILTERED_MAX_SLOTS);p->identities.reserve(EXBOX_FILTERED_MAX_SLOTS*2);return std::unique_ptr<Owner>(new Owner(std::move(p)));
  }catch(...){return {};}
 }
 ~Owner(){if(!close())(void)impl.release();} // retain all dependencies on unsafe exit
 Phase phase()const{return impl?impl->phase:Phase::Idle;}
 Stats stats()const{return impl?impl->stats:Stats{};}
 const std::vector<Binding>&bindings()const{static const std::vector<Binding>empty;return impl?impl->frame:empty;}
 bool prepare(const std::vector<Request>&requests){
  if(!impl||impl->phase!=Phase::Idle||requests.size()>EXBOX_FILTERED_MAX_SLOTS)return false;auto&p=*impl;
  // Validate packet identity/dimensions before any callback or cache mutation.
  for(const auto&r:requests){const auto&k=r.key;if(!k.owner||!k.id||!k.generation||k.filter!=1||k.w<1||k.h<1||k.dw<1||k.dh<1||k.w>8192||k.h>8192||k.dw>8192||k.dh>8192||uint64_t(k.dw)*k.dh*4>64ull*1024*1024)return false;if(r.prepared){const auto&s=*r.prepared;if(s.sw!=uint32_t(k.w)||s.sh!=uint32_t(k.h)||s.dw!=uint32_t(k.dw)||s.dh!=uint32_t(k.dh)||s.pixels.size()!=uint64_t(s.sw)*s.sh)return false;}else if(!p.cache->contains(k))return false;}
  if(!p.unbind_all())return false;p.frame.clear();p.fresh.clear();
  try{for(const auto&r:requests){VkBuffer buffer=p.cache->find(r.key);bool miss=!buffer;if(miss){if(!r.prepared){p.rollback();return false;}buffer=p.cache->request(r.key,*r.prepared,[&](const resize_adapter::Prepared&s){return p.upload(s);});if(!buffer){p.rollback();return false;}p.fresh.push_back(r.key);p.stats.misses++;}else p.stats.hits++;
    p.prune();uint64_t generation=0;for(const auto&i:p.identities)if(i.key==r.key&&i.buffer==buffer){generation=i.generation;break;}
    if(!generation){if(p.serial==UINT64_MAX){p.rollback();return false;}generation=++p.serial;p.identities.push_back({r.key,buffer,generation});}
    p.frame.push_back({r.key,buffer,uint64_t(r.key.dw)*r.key.dh*4,uint32_t(r.key.dw),uint32_t(r.key.dh),uint32_t(r.key.dw)*4,generation});
   }p.phase=Phase::Prepared;return true;
  }catch(...){p.rollback();return false;}
 }
 bool record(VkCommandBuffer command){if(!impl||impl->phase!=Phase::Prepared||!command)return false;auto&p=*impl;p.phase=Phase::Recorded;for(const auto&b:p.frame)if(!p.cache->record(b.key,command)){p.phase=Phase::Quarantined;return false;}return true;}
 // Native session must already have proven ALL filter and consumer work complete.
 bool complete_after_wait(bool proven_success){if(!impl||impl->phase!=Phase::Recorded)return false;auto&p=*impl;if(!proven_success){p.phase=Phase::Quarantined;return false;}p.cache->completed_after_wait();p.fresh.clear();p.phase=Phase::Idle;return true;}
 // Discard packet and all descriptor bindings before cancelling pre-record pins.
 bool cancel_unsubmitted(){if(!impl||impl->phase!=Phase::Prepared)return false;auto&p=*impl;if(!p.unbind_all())return false;p.rollback();p.phase=Phase::Idle;return true;}
 bool invalidate(uint64_t owner){if(!impl||impl->phase!=Phase::Idle)return false;if(!impl->unbind_all())return false;impl->cache->invalidate(owner);impl->prune();impl->frame.clear();return true;}
 bool close(){if(!impl)return true;auto&p=*impl;if(p.phase==Phase::Recorded||p.phase==Phase::Quarantined)return false;if(!p.unbind_all())return false;p.cache->cancel_unrecorded_requests();if(!p.cache->close())return false;impl.reset();return true;}
};
inline Owner::Block::~Block(){if(mapped)owner->UnmapMemory(owner->device,memory);if(buffer)owner->DestroyBuffer(owner->device,buffer,nullptr);if(memory)owner->FreeMemory(owner->device,memory,nullptr);if(reservation.owner)exbox_gpu_budget_release(owner->budget,&reservation);}
}
