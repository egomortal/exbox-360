#pragma once
#include "tab_sink.hpp"
#include "filtered_cpp.hpp"
#include "direct_lanczos.hpp"
#include "video_publication.hpp"
#include <memory>
#include <atomic>
#include <cstdio>
namespace x21hook {
inline auto&source_cache=x21source::global_cache;
inline uint64_t cpu_fitted_preparations=0,cpu_picture_zoom_resamples=0,audit_picture_zoom_resamples=0;
struct OwnedPacket {
 struct Actor {std::shared_ptr<const std::vector<uint32_t>> words;std::size_t command_index;bool emote=false;};
 std::vector<Actor> actors;
 std::vector<filtered_art::Request> filtered_requests;
 std::vector<std::shared_ptr<ExboxFilteredPacket>> filtered_handles;std::vector<ExboxFilteredRequest> filter_requests;
 std::vector<std::shared_ptr<const x21source::Publication>> publications;std::vector<uint64_t> source_ids;
 std::vector<ExboxGpuCommand> commands;std::vector<uint8_t> bytes;
 uint64_t publication_hashed=0,publication_verified=0,publication_copied=0,generation_hits=0,normalizations=0,normalized_bytes=0;
 bool fullscreen_picture=false,pictures_grid=false;
 uint64_t cpu_fitted=0,cpu_zoom=0,audit_zoom=0;
 int overlay_mode=0,viewer=0;uint64_t cpu_guide_paints=0;
 int width=0,height=0,channel=0;std::vector<uint32_t> reference;
 std::vector<uint32_t> avatar_words,avatar_reference;uint64_t avatar_generation=0;int avatar_width=0,avatar_height=0;bool partial_cpu_background=false;
 unsigned build=0;size_t gpu_primitives=0,resource_copy_bytes=0,unhandled=0;
};
inline std::shared_ptr<OwnedPacket> ready;
inline bool allow_frame=false,skip_frame=false;
inline TabSink* active=nullptr;inline SDL_Surface* target=nullptr;
inline std::atomic<bool> available{true},fatal{false};
inline bool audit_cpu(){
#ifndef __PROSPERO__
 const char*p=std::getenv("NXE_GPU_AUDIT");return p&&!std::strcmp(p,"1");
#else
 return false;
#endif
}
inline bool enabled(){
 if(!available.load())return false;
 const char*p=std::getenv("NXE_GPU_DASHBOARD");
#ifdef __PROSPERO__
 // Read the owner opt-in once. CPU rendering is the default; no frame I/O.
 static const bool native_requested=[](){char value[4]{};FILE*f=std::fopen("/data/nxe/exbox/gpu-dashboard.flag","rb");size_t n=0;if(f){n=std::fread(value,1,sizeof value,f);std::fclose(f);}return n>0&&value[0]=='1'&&(n==1||(n==2&&value[1]=='\n')||(n==3&&value[1]=='\r'&&value[2]=='\n'));}();
 return native_requested||(p&&!std::strcmp(p,"1"));
#else
 return (p&&!std::strcmp(p,"1"))||audit_cpu();
#endif
}
struct Pause{TabSink*saved;explicit Pause(bool yes):saved(yes?active:nullptr){if(yes)active=nullptr;}~Pause(){if(saved)active=saved;}};
inline bool recording(SDL_Surface*s){return active&&s==target;}
inline void sync_clip(){if(active&&target)active->clip=target->clip_rect;}
inline void snapshot_commands(size_t first){
 for(size_t i=first;i<active->packet.commands.size();i++){
  auto&c=active->packet.commands[i];if(!c.bytes||!((c.kind>=EXBOX_GPU_TEXTURE&&c.kind<=EXBOX_GPU_NINE)||c.kind==EXBOX_GPU_DIRECT_LANCZOS||c.kind==EXBOX_GPU_VIDEO_Q8||c.kind==EXBOX_GPU_VERTICAL_STRIP||c.kind==EXBOX_GPU_NXE_REFLECTION))continue;
  if(c.bytes>EXBOX_GPU2D_SOURCE_BYTES){active->packet.unsupported.push_back("source_snapshot_oversize");continue;}
  try{std::shared_ptr<const x21source::Publication> resource;
   for(const auto&r:active->packet.publications)if(r->data()==c.pixels&&r->bytes==c.bytes){resource=r;break;}
   if(!resource)resource=source_cache.intern(c.pixels,c.bytes);c.pixels=resource->data();active->publications.push_back(std::move(resource));}
  catch(const std::exception&){active->packet.unsupported.push_back("immutable_source_capacity");}
 }
}
inline bool record_physical(SDL_Surface*src,const SDL_Rect*sr,SDL_Surface*dst,const SDL_Rect&to,bool nearest=false){
 if(!recording(dst))return false;sync_clip();size_t first=active->packet.commands.size();
 if(!active->blit(src,sr,to,nearest))active->packet.unsupported.push_back("SDL_blit_not_recorded");
 snapshot_commands(first);return !audit_cpu();
}
inline x21video::Committed committed_video;
inline bool video_frame(SDL_Surface*source,uint64_t generation,SDL_Surface*destination,SDL_Rect dst,float alpha){
 if(!recording(destination))return false;sync_clip();
 if(active->target!=0){active->packet.unsupported.push_back("video_nonoutput_target");return false;}
 if(!committed_video.prepare(source,1,generation)){active->packet.unsupported.push_back("video_opaque_source_refused");return false;}
 auto c=committed_video.command({dst.x,dst.y,dst.w,dst.h},{active->clip.x,active->clip.y,active->clip.w,active->clip.h},alpha);
 active->packet.publications.push_back(committed_video.source());active->push(c);return true;
}
inline bool vertical_strip(SDL_Surface*source,SDL_Surface*destination){
 if(!recording(destination))return false;sync_clip();auto c=active->base({0,0,destination->w,destination->h});c.kind=EXBOX_GPU_VERTICAL_STRIP;c.blend=0;auto publication=x21source::surface(source);c.pixels=publication->data();active->packet.publications.push_back(std::move(publication));c.bytes=size_t(source->pitch)*source->h;c.width=unsigned(source->w);c.height=unsigned(source->h);c.pitch=unsigned(source->pitch);c.opacity=255;c.transform[8]=1;
 size_t first=active->packet.commands.size();active->push(c);snapshot_commands(first);return true;
}
inline bool quality(SDL_Surface*source,SDL_Rect from,SDL_Surface*target,SDL_Rect destination,bool narrow_gpu=false){
 if(!recording(target))return false;sync_clip();
 try{
  Uint32 key=0;bool keyed=SDL_GetColorKey(source,&key)==0;
  if(from.w==destination.w&&from.h==destination.h&&!keyed){record_physical(source,&from,target,destination,true);return !audit_cpu();}
  auto request=x21filter::prepare(source,from,destination.w,destination.h);
  if(narrow_gpu){
   x21source::ResourceKey identity{4,1,request.key.generation,1};auto publication=x21source::generations.find(identity);
   if(!publication){std::vector<uint32_t>words;if(!direct_lanczos::packet(*request.prepared,words))throw std::runtime_error("direct narrow filter packet refused");publication=x21source::known(identity,words.data(),words.size()*4);}
   SDL_BlendMode blend;Uint8 alpha,r,g,b;SDL_GetSurfaceBlendMode(source,&blend);SDL_GetSurfaceAlphaMod(source,&alpha);SDL_GetSurfaceColorMod(source,&r,&g,&b);if(blend!=SDL_BLENDMODE_NONE&&blend!=SDL_BLENDMODE_BLEND)throw std::runtime_error("direct filtered blend mode");
   auto c=active->base(destination);c.kind=EXBOX_GPU_DIRECT_LANCZOS;c.pixels=publication->data();c.bytes=publication->bytes;c.width=request.prepared->sw;c.height=request.prepared->sh;c.pitch=c.width*4;c.top=0xff000000u|(unsigned(r)<<16)|(unsigned(g)<<8)|b;c.blend=blend==SDL_BLENDMODE_NONE?7:((active->sdl_layer||active->target==0)&&r==255&&g==255&&b==255?9:1);if(c.blend==9)c.fill_type=0x53444cu;c.transform[8]=float(alpha)/255;active->packet.publications.push_back(std::move(publication));active->push(c);return !audit_cpu();
  }
if(std::none_of(active->filtered_requested.begin(),active->filtered_requested.end(),[&](const auto&key){return key==request.key;}))active->filtered_requested.push_back(request.key);size_t slot=0;
  for(;slot<active->filtered_requests.size();slot++)if(active->filtered_requests[slot].key==request.key)break;
  if(slot==active->filtered_requests.size()){if(slot==EXBOX_FILTERED_MAX_SLOTS)throw std::length_error("filtered output capacity exceeded");active->filtered_requests.push_back(std::move(request));}
  SDL_BlendMode blend;Uint8 alpha,r,g,b;SDL_GetSurfaceBlendMode(source,&blend);SDL_GetSurfaceAlphaMod(source,&alpha);SDL_GetSurfaceColorMod(source,&r,&g,&b);
  if(blend!=SDL_BLENDMODE_NONE&&blend!=SDL_BLENDMODE_BLEND)throw std::runtime_error("filtered blend mode");
  auto c=active->base(destination);c.kind=EXBOX_GPU_FILTERED;c.bytes=1;c.width=unsigned(destination.w);c.height=unsigned(destination.h);c.pitch=c.width*4;c.top=0xff000000u|(unsigned(r)<<16)|(unsigned(g)<<8)|b;c.blend=blend==SDL_BLENDMODE_NONE?7:((active->sdl_layer||active->target==0)&&r==255&&g==255&&b==255?9:1);if(c.blend==9)c.fill_type=0x53444cu;
  c.transform[0]=1;c.transform[4]=1;c.transform[2]=-float(destination.x);c.transform[5]=-float(destination.y);c.transform[8]=float(alpha)/255;c.transform[9]=float(slot);active->push(c);
 }catch(const std::exception&e){active->packet.unsupported.push_back(std::string("filtered_art:")+e.what());}
 return !audit_cpu();
}
inline int upper_blit(SDL_Surface*s,const SDL_Rect*r,SDL_Surface*d,SDL_Rect*t){SDL_Rect to=t?*t:SDL_Rect{0,0,0,0};to.w=r?r->w:s->w;to.h=r?r->h:s->h;if(record_physical(s,r,d,to))return 0;return SDL_UpperBlit(s,r,d,t);}
inline int blit_impl(SDL_Surface*s,const SDL_Rect*r,SDL_Surface*d,SDL_Rect*t,bool scaled){
 if(recording(d)){
  SDL_Rect from=r?render_density::pixel_rect(*r,render_density::density(s)):SDL_Rect{0,0,s->w,s->h};const int dd=render_density::density(d),sd=render_density::density(s);
  SDL_Rect to=t?render_density::pixel_rect(*t,dd):SDL_Rect{0,0,d->w,d->h};
  if(!scaled){to.w=(from.w*dd+sd-1)/sd;to.h=(from.h*dd+sd-1)/sd;}
  if(record_physical(s,&from,d,to,from.w!=to.w||from.h!=to.h))return 0;
 }
 return scaled?render_density::blit_scaled(s,r,d,t):render_density::blit(s,r,d,t);
}
inline int blit(SDL_Surface*s,const SDL_Rect*r,SDL_Surface*d,SDL_Rect*t){return blit_impl(s,r,d,t,false);}
inline int blit_scaled(SDL_Surface*s,const SDL_Rect*r,SDL_Surface*d,SDL_Rect*t){return blit_impl(s,r,d,t,true);}
inline int fill(SDL_Surface*s,const SDL_Rect*r,Uint32 color){
 if(recording(s)){sync_clip();SDL_Rect rect=r?render_density::pixel_rect(*r,render_density::density(s)):SDL_Rect{0,0,s->w,s->h};auto c=active->base(rect);c.kind=EXBOX_GPU_RECT;c.top=color;active->push(c);if(!audit_cpu())return 0;}
 return render_density::fill(s,r,color);
}
inline bool rectangle(SDL_Surface*s,int x,int y,int w,int h,uint32_t color){if(!recording(s))return false;sync_clip();active->rect(x,y,w,h,color);return !audit_cpu();}
inline bool shade(SDL_Surface*s,int x,int y,int w,int h,uint32_t color,uint8_t alpha){if(!recording(s))return false;sync_clip();auto c=active->base({x*active->density,y*active->density,w*active->density,h*active->density});c.kind=EXBOX_GPU_RECT;c.top=(uint32_t(alpha)<<24)|(color&0xffffff);c.blend=8;c.fill_type=0x53444cu;active->push(c);return !audit_cpu();}
inline bool gradient(SDL_Surface*s,int x,int y,int w,int h,uint32_t a,uint32_t b){if(!recording(s))return false;sync_clip();active->gradient(x,y,w,h,a,b);return !audit_cpu();}
inline bool rounded(SDL_Surface*s,int x,int y,int w,int h,int r,uint32_t a,uint32_t b){if(!recording(s))return false;sync_clip();active->rounded(x,y,w,h,r,a,b);return !audit_cpu();}
inline void own_refused(const char*reason,const TabSink&sink){
#ifndef __PROSPERO__
 static unsigned reports=0;const char*enabled=std::getenv("NXE_GPU_PACKET_REASON");
 if(!enabled||std::strcmp(enabled,"1")||reports++>=8)return;
 std::fprintf(stderr,"GPU_PACKET_REFUSED reason=%s commands=%zu filters=%zu\n",reason,sink.packet.commands.size(),sink.filtered_requests.size());
 unsigned counts[24]={};unsigned outside=0;for(const auto&c:sink.packet.commands){if(unsigned(c.kind)<24)counts[c.kind]++;if(int64_t(c.dst.x)+c.dst.w<=c.clip.x||int64_t(c.clip.x)+c.clip.w<=c.dst.x||int64_t(c.dst.y)+c.dst.h<=c.clip.y||int64_t(c.clip.y)+c.clip.h<=c.dst.y)outside++;}for(unsigned k=0;k<24;k++)if(counts[k])std::fprintf(stderr,"GPU_KIND kind=%u count=%u\n",k,counts[k]);std::fprintf(stderr,"GPU_OUTSIDE count=%u\n",outside);
 for(size_t i=0;i<sink.packet.commands.size();++i){size_t bytes=0;const auto&c=sink.packet.commands[i];if(!exbox_gpu2d_validate(&c,1,&bytes))std::fprintf(stderr,"GPU_COMMAND_REFUSED index=%zu kind=%u bytes=%zu width=%u height=%u blend=%u\n",i,unsigned(c.kind),size_t(c.bytes),unsigned(c.width),unsigned(c.height),unsigned(c.blend));}
#else
 (void)reason;(void)sink;
#endif
}
inline std::shared_ptr<OwnedPacket> own(TabSink& sink,unsigned build){
 size_t total=0;if(!sink.packet.unsupported.empty()){own_refused("unsupported",sink);return {};}if(!exbox_gpu2d_validate(sink.packet.commands.data(),sink.packet.commands.size(),&total)){own_refused("validation",sink);return {};}
 auto result=std::make_shared<OwnedPacket>();result->commands=sink.packet.commands;result->source_ids.assign(result->commands.size(),0);
 try{for(size_t i=0;i<result->commands.size();i++){auto&c=result->commands[i];if((c.kind>=EXBOX_GPU_TEXTURE&&c.kind<=EXBOX_GPU_NINE)||c.kind==EXBOX_GPU_DIRECT_LANCZOS||c.kind==EXBOX_GPU_VIDEO_Q8||c.kind==EXBOX_GPU_VERTICAL_STRIP||c.kind==EXBOX_GPU_NXE_REFLECTION){
  std::shared_ptr<const x21source::Publication> resource;
  for(const auto&r:sink.publications)if(r->data()==c.pixels&&r->bytes==c.bytes){resource=r;break;}
  if(!resource)for(const auto&r:sink.packet.publications)if(r->data()==c.pixels&&r->bytes==c.bytes){resource=r;break;}
  if(!resource)resource=source_cache.intern(c.pixels,c.bytes);
  c.pixels=resource->data();result->source_ids[i]=resource->id;result->publications.push_back(std::move(resource));
 }}}catch(const std::exception&e){own_refused(e.what(),sink);return {}; }
 result->filtered_requests=sink.filtered_requests;for(const auto&r:result->filtered_requests){const auto&k=r.key;ExboxFilteredKey key{k.owner,k.id,k.generation,k.x,k.y,k.w,k.h,k.dw,k.dh,k.filter};auto*raw=exbox_filtered_packet_share(key,r.prepared);if(!raw){own_refused("filtered-share",sink);return {};}result->filtered_handles.emplace_back(raw,exbox_filtered_packet_release);result->filter_requests.push_back({key,raw});}result->publication_hashed=source_cache.hashed_bytes;result->publication_verified=source_cache.verified_bytes;result->publication_copied=source_cache.copied_bytes;result->generation_hits=x21source::generations.hits;result->normalizations=x21filter::normalizations;result->normalized_bytes=x21filter::normalized_bytes;result->width=sink.width;result->height=sink.height;result->build=build;result->gpu_primitives=result->commands.size();result->resource_copy_bytes=total;return result;
}
}
