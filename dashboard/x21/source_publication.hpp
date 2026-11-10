#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <stdexcept>
#include <algorithm>
namespace x21source {
struct Accounting {std::atomic<size_t> live{0};};
struct Publication {
 uint64_t id=0,hash=0;size_t bytes=0;std::vector<uint32_t> words;std::shared_ptr<Accounting> accounting;
 ~Publication(){if(accounting)accounting->live.fetch_sub(bytes);}
 const void* data()const{return words.data();}
};
class Cache {
 struct Entry {std::shared_ptr<const Publication> value;uint64_t used;};
 std::vector<Entry> entries;std::shared_ptr<Accounting> accounting=std::make_shared<Accounting>();size_t limit;uint64_t serial=0,clock=0;
public:
 uint64_t copied_bytes=0,hashed_bytes=0,verified_bytes=0,hits=0,misses=0;
 explicit Cache(size_t cap=64u*1024u*1024u):limit(cap){}
 std::shared_ptr<const Publication> intern(const void*data,size_t bytes){
  if(!data||!bytes||bytes>limit)throw std::length_error("immutable source bound");
  uint64_t hash=1469598103934665603ull;const auto*p=static_cast<const unsigned char*>(data);
  hashed_bytes+=bytes;
  for(size_t i=0;i<bytes;i++){hash^=p[i];hash*=1099511628211ull;}
  for(auto&e:entries)if(e.value->bytes==bytes&&e.value->hash==hash){verified_bytes+=bytes;if(!std::memcmp(e.value->data(),data,bytes)){hits++;e.used=++clock;return e.value;}}
  while(accounting->live.load()>limit-bytes){
   auto victim=entries.end();for(auto i=entries.begin();i!=entries.end();++i)if(i->value.use_count()==1&&(victim==entries.end()||i->used<victim->used))victim=i;
   if(victim==entries.end())throw std::length_error("immutable sources pinned at capacity");entries.erase(victim);
  }
  if(serial==UINT64_MAX)throw std::overflow_error("source generation exhausted");
  auto value=std::make_shared<Publication>();value->id=++serial;value->hash=hash;value->words.resize((bytes+3)/4);std::memcpy(value->words.data(),data,bytes);value->bytes=bytes;value->accounting=accounting;accounting->live.fetch_add(bytes);
  entries.push_back({value,++clock});copied_bytes+=bytes;misses++;return value;
 }
 // Explicit new committed content: own a copy, without hashing/dedup or
 // retention in the content cache. Caller/queued packets own its lifetime.
 // IDs and byte accounting share the ordinary cache's domain and cap.
 std::shared_ptr<const Publication> unique(const void*data,size_t bytes){
  if(!data||!bytes||bytes>limit)throw std::length_error("immutable source bound");
  while(accounting->live.load()>limit-bytes){
   auto victim=entries.end();for(auto i=entries.begin();i!=entries.end();++i)if(i->value.use_count()==1&&(victim==entries.end()||i->used<victim->used))victim=i;
   if(victim==entries.end())throw std::length_error("immutable sources pinned at capacity");entries.erase(victim);
  }
  if(serial==UINT64_MAX)throw std::overflow_error("source generation exhausted");
  auto value=std::make_shared<Publication>();value->id=++serial;value->words.resize((bytes+3)/4);std::memcpy(value->words.data(),data,bytes);value->bytes=bytes;value->accounting=accounting;accounting->live.fetch_add(bytes);
  copied_bytes+=bytes;misses++;return value;
 }
 void clear(){entries.clear();}
 size_t live_bytes()const{return accounting->live.load();}
};
}
