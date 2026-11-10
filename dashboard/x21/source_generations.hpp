#pragma once
#include "source_publication.hpp"
#include <map>
#include <tuple>
namespace x21source {
struct ResourceKey {
 uint64_t owner=0,epoch=0,resource=0,generation=0;
 bool valid()const{return owner&&epoch&&resource&&generation;}
 bool operator<(const ResourceKey&b)const{return std::tie(owner,epoch,resource,generation)<std::tie(b.owner,b.epoch,b.resource,b.generation);}
};
class Generations {
 struct Entry {std::shared_ptr<const Publication> value;uint64_t used;};
 Cache&bytes;size_t capacity;uint64_t clock=0;
 std::map<ResourceKey,Entry> entries;
 uint64_t tick(){if(clock==UINT64_MAX){for(auto&e:entries)e.second.used=0;clock=0;}return ++clock;}
 bool evict_index(){if(entries.empty())return false;auto old=entries.begin();for(auto i=entries.begin();i!=entries.end();++i)if(i->second.used<old->second.used)old=i;entries.erase(old);++index_evictions;return true;}
public:
 uint64_t lookups=0,hits=0,publishes=0,explicit_verification_bytes=0,index_evictions=0;
 explicit Generations(Cache&storage,size_t max_entries=4096):bytes(storage),capacity(max_entries){}
 std::shared_ptr<const Publication> find(ResourceKey key){++lookups;auto i=entries.find(key);if(i==entries.end())return {};++hits;i->second.used=tick();return i->second.value;}
 std::shared_ptr<const Publication> publish(ResourceKey key,const void*data,size_t count){
  if(!key.valid()||!data||!count)throw std::invalid_argument("invalid resource publication");
  auto i=entries.find(key);
  if(i!=entries.end()){
   if(i->second.value->bytes!=count)throw std::logic_error("resource generation changed size");
   explicit_verification_bytes+=count;
   if(std::memcmp(i->second.value->data(),data,count))throw std::logic_error("resource generation changed content");
   i->second.used=tick();return i->second.value;
  }
  if(!capacity)throw std::length_error("resource generation table disabled");
  while(entries.size()>=capacity)evict_index();
  std::shared_ptr<const Publication> value;
  for(;;){
   try{value=bytes.intern(data,count);break;}
   catch(const std::length_error&e){
    if(std::strcmp(e.what(),"immutable sources pinned at capacity")||!evict_index())throw;
   }
  }
  entries.emplace(key,Entry{value,tick()});++publishes;return value;
 }
 // Index eviction is NOT retirement of a GPU resource. Queued shared bytes
 // survive; presenter retirement waits for all referencing frames as before.
 // Pressure may cool this lookup cache even when new publication fails.
 void retire(ResourceKey key){entries.erase(key);}
 void retire_resource(uint64_t owner,uint64_t resource){for(auto i=entries.begin();i!=entries.end();)if(i->first.owner==owner&&i->first.resource==resource)i=entries.erase(i);else ++i;}
 void retire_owner(uint64_t owner){for(auto i=entries.begin();i!=entries.end();)if(i->first.owner==owner)i=entries.erase(i);else ++i;}
 size_t size()const{return entries.size();}
};
}
