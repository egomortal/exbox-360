// SPDX-License-Identifier: GPL-3.0-or-later
#include "filtered_cpp.hpp"
#include "filtered_owner.hpp"
#include <atomic>
#include <limits>
struct ExboxFilteredPacket {std::atomic<size_t>references{1};ExboxFilteredKey key{};std::shared_ptr<const resize_adapter::Prepared>prepared;};
struct ExboxFilteredOwner {std::unique_ptr<filtered_art::Owner>owner;};
static resize_adapter::Key key(const ExboxFilteredKey&k){return {k.owner,k.id,k.generation,k.x,k.y,k.w,k.h,k.dw,k.dh,k.filter};}
static ExboxFilteredKey key(const resize_adapter::Key&k){return {k.owner,k.id,k.generation,k.x,k.y,k.w,k.h,k.dw,k.dh,k.filter};}
static bool dimensions(const ExboxFilteredKey&k,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,size_t pixels,size_t taps){return k.owner&&k.id&&k.generation&&k.filter==1&&k.w>0&&k.h>0&&k.dw>0&&k.dh>0&&sw==uint32_t(k.w)&&sh==uint32_t(k.h)&&dw==uint32_t(k.dw)&&dh==uint32_t(k.dh)&&sw<=8192&&sh<=8192&&dw<=8192&&dh<=8192&&uint64_t(sw)*sh==pixels&&pixels<=16777216&&uint64_t(dw)*dh<=16777216&&taps<=1048576;}
ExboxFilteredPacket* exbox_filtered_packet_share(const ExboxFilteredKey&k,std::shared_ptr<const resize_adapter::Prepared>p){if(!p||!dimensions(k,p->sw,p->sh,p->dw,p->dh,p->pixels.size(),p->taps.size()))return nullptr;try{auto result=std::make_unique<ExboxFilteredPacket>();result->key=k;result->prepared=std::move(p);return result.release();}catch(...){return nullptr;}}
extern "C" ExboxFilteredPacket* exbox_filtered_packet_create(const ExboxFilteredKey*k,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int alpha,const uint32_t*pixels,size_t count,const uint32_t*taps,size_t words){if(!k||!pixels||(words&&!taps)||(alpha!=0&&alpha!=1)||!dimensions(*k,sw,sh,dw,dh,count,words))return nullptr;try{auto p=std::make_shared<resize_adapter::Prepared>();p->sw=sw;p->sh=sh;p->dw=dw;p->dh=dh;p->alpha=alpha!=0;p->pixels.assign(pixels,pixels+count);if(words)p->taps.assign(taps,taps+words);return exbox_filtered_packet_share(*k,std::move(p));}catch(...){return nullptr;}}
extern "C" int exbox_filtered_packet_retain(ExboxFilteredPacket*p){if(!p)return 0;size_t n=p->references.load(std::memory_order_relaxed);while(n&&n!=std::numeric_limits<size_t>::max()){if(p->references.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return 1;}return 0;}
extern "C" void exbox_filtered_packet_release(ExboxFilteredPacket*p){if(p&&p->references.fetch_sub(1,std::memory_order_acq_rel)==1)delete p;}
extern "C" ExboxFilteredOwner* exbox_filtered_owner_create(VkDevice d,const VkPhysicalDeviceMemoryProperties*m,const VkPhysicalDeviceProperties*p,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get,ExboxGpuBudget*budget,ExboxFilteredUnbind unbind,void*context){if(!m||!p||!unbind)return nullptr;try{auto result=std::make_unique<ExboxFilteredOwner>();result->owner=filtered_art::Owner::create(d,*m,*p,flags,get,budget,[=](unsigned slot){return unbind(context,slot)!=0;});return result->owner?result.release():nullptr;}catch(...){return nullptr;}}
extern "C" int exbox_filtered_prepare(ExboxFilteredOwner*o,const ExboxFilteredRequest*r,size_t n,ExboxFilteredBinding*out,size_t capacity,size_t*written){if(written)*written=0;if(!o||!written||n>EXBOX_FILTERED_MAX_SLOTS||capacity<n||(n&&(!r||!out)))return 0;try{std::vector<filtered_art::Request>requests;requests.reserve(n);for(size_t i=0;i<n;i++){if(r[i].packet&&!(key(r[i].packet->key)==key(r[i].key)))return 0;requests.push_back({key(r[i].key),r[i].packet?r[i].packet->prepared:nullptr});}if(!o->owner->prepare(requests))return 0;const auto&bindings=o->owner->bindings();for(size_t i=0;i<bindings.size();i++){const auto&b=bindings[i];out[i]={key(b.key),b.buffer,b.bytes,b.width,b.height,b.pitch,b.generation};}*written=bindings.size();return 1;}catch(...){return 0;}}
extern "C" int exbox_filtered_record(ExboxFilteredOwner*o,VkCommandBuffer c){try{return o&&o->owner->record(c);}catch(...){return 0;}}
extern "C" int exbox_filtered_complete_after_wait(ExboxFilteredOwner*o,int success){try{return o&&o->owner->complete_after_wait(success!=0);}catch(...){return 0;}}
extern "C" int exbox_filtered_cancel_unsubmitted(ExboxFilteredOwner*o){try{return o&&o->owner->cancel_unsubmitted();}catch(...){return 0;}}
extern "C" int exbox_filtered_invalidate(ExboxFilteredOwner*o,uint64_t id){try{return o&&o->owner->invalidate(id);}catch(...){return 0;}}
extern "C" int exbox_filtered_phase(const ExboxFilteredOwner*o){return o?int(o->owner->phase()):EXBOX_FILTERED_IDLE;}
extern "C" int exbox_filtered_stats(const ExboxFilteredOwner*o,ExboxFilteredStats*s){if(!o||!s)return 0;const auto a=o->owner->stats();*s={a.uploads,a.uploaded_bytes,a.hits,a.misses};return 1;}
extern "C" int exbox_filtered_owner_destroy(ExboxFilteredOwner*o){if(!o)return 1;try{if(!o->owner->close())return 0;delete o;return 1;}catch(...){return 0;}}

extern "C" unsigned exbox_filtered_capacity(void){return EXBOX_FILTERED_MAX_SLOTS;}
