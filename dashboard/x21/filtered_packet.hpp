#pragma once
#include "filtered_owner.hpp"
#include "source_surfaces.hpp"
namespace x21filter {
struct Metadata {uint64_t source=0;int width=0,height=0,pitch=0;uint32_t format=0,key=0;bool keyed=false;SDL_Rect crop{};int outw=0,outh=0;
 bool operator==(const Metadata&o)const{return source==o.source&&width==o.width&&height==o.height&&pitch==o.pitch&&format==o.format&&key==o.key&&keyed==o.keyed&&crop.x==o.crop.x&&crop.y==o.crop.y&&crop.w==o.crop.w&&crop.h==o.crop.h&&outw==o.outw&&outh==o.outh;}};
struct Entry {Metadata metadata;filtered_art::Request request;size_t bytes=0;uint64_t used=0;};
inline std::vector<Entry> prepared_cache;inline uint64_t clock=0,serial=0,normalizations=0,normalized_bytes=0;
inline filtered_art::Request prepare(SDL_Surface*source,SDL_Rect crop,int w,int h){
 if(!source||source->pitch<1||source->h<1)throw std::invalid_argument("filtered source");
 // Untracked mutable SDL data remains byte-verified. Stable decoded-owner hooks
 // may later replace this lookup with explicit resource generations.
 auto original=x21source::surface(source);
 Uint32 color_key=0;bool keyed=SDL_GetColorKey(source,&color_key)==0;
 Metadata metadata{original->id,source->w,source->h,source->pitch,source->format->format,color_key,keyed,crop,w,h};
 for(auto&e:prepared_cache)if(e.metadata==metadata){e.used=++clock;return e.request;}
 auto prepared=std::make_shared<resize_adapter::Prepared>();if(!resize_adapter::prepare(source,crop,w,h,*prepared))throw std::runtime_error("filtered normalization refused");
 size_t bytes=prepared->pixels.size()*4+prepared->taps.size()*4;if(bytes>64u*1024u*1024u)throw std::length_error("filtered CPU packet bound");
 for(;;){size_t live=bytes;for(const auto&e:prepared_cache)live+=e.bytes;if(live<=64u*1024u*1024u&&prepared_cache.size()<EXBOX_FILTERED_MAX_SLOTS)break;
  auto victim=prepared_cache.end();for(auto i=prepared_cache.begin();i!=prepared_cache.end();++i)if(i->request.prepared.use_count()==1&&(victim==prepared_cache.end()||i->used<victim->used))victim=i;
  if(victim==prepared_cache.end())throw std::length_error("filtered CPU packets pinned");prepared_cache.erase(victim);
 }
 if(serial==UINT64_MAX)throw std::overflow_error("filtered generation exhausted");
 resize_adapter::Key key{2,original->id,++serial,crop.x,crop.y,crop.w,crop.h,w,h,1};filtered_art::Request request{key,prepared};prepared_cache.push_back({metadata,request,bytes,++clock});normalizations++;normalized_bytes+=prepared->pixels.size()*4;return request;
}
}
