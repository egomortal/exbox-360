#pragma once
#include "video_q8.hpp"
#include "publication_registry.hpp"
namespace x21video {
// UI decoder owner only. A committed generation must change whenever pixels do.
// Owned publications outlive this cache when a queued frame still references them.
class Committed {
 uint64_t attempted_epoch=0,attempted_generation=0;
 uint32_t width=0,height=0;
 std::shared_ptr<const x21source::Publication> publication;
public:
 uint64_t validated_frames=0,warm_hits=0,converted_bytes=0;
 void clear(){publication.reset();attempted_epoch=attempted_generation=0;width=height=0;x21source::generations.retire_resource(5,1);}
 bool prepare(SDL_Surface* source,uint64_t epoch,uint64_t generation){
  if(!epoch||!generation)return false;
  if(epoch==attempted_epoch&&generation==attempted_generation){++warm_hits;return bool(publication);}
  publication.reset();width=height=0;attempted_epoch=epoch;attempted_generation=generation;
  x21source::generations.retire_resource(5,1);
  if(!source||!source->format||!source->pixels||source->w<1||source->h<1||source->w>3840||source->h>2160||SDL_MUSTLOCK(source))return false;
  Uint8 r,g,b,a;Uint32 key;
  if(SDL_GetSurfaceColorMod(source,&r,&g,&b)||r!=255||g!=255||b!=255||SDL_GetSurfaceAlphaMod(source,&a)||a!=255||SDL_GetColorKey(source,&key)==0)return false;
  struct Surface {SDL_Surface* value=nullptr;~Surface(){if(value)SDL_FreeSurface(value);}} converted;
  SDL_Surface* pixels=source;
  if(source->format->format!=SDL_PIXELFORMAT_ARGB8888){converted.value=SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_ARGB8888,0);pixels=converted.value;if(!pixels)return false;converted_bytes+=uint64_t(pixels->w)*pixels->h*4;}
  video_q8::Frame frame;
  if(!video_q8::prepare(pixels,generation,frame))return false;
  try{publication=x21source::global_cache.unique(frame.pixels.data(),frame.pixels.size()*4);width=frame.width;height=frame.height;++validated_frames;return true;}catch(const std::exception&){return false;}
 }
 std::shared_ptr<const x21source::Publication> source()const{return publication;}
 ExboxGpuCommand command(ExboxGpuRect dst,ExboxGpuRect clip,float alpha)const{
  ExboxGpuCommand c{};c.kind=EXBOX_GPU_VIDEO_Q8;c.dst=dst;c.clip=clip;c.top=0xffffffff;c.opacity=255;c.blend=alpha>=1?7:1;c.transform[8]=float(Uint8(255*alpha))/255;c.width=width;c.height=height;c.pitch=width*4;
  if(publication){c.pixels=publication->data();c.bytes=publication->bytes;}return c;
 }
};
}
