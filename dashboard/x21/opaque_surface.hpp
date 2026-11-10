#pragma once
#include "surface_generations.hpp"
#include <cstring>
namespace x21opaque {
// UI owner only. Cache a pixel-opacity proof only for sealed immutable pixels.
class Proof {
 surface_generations::Identity cached{};bool value=false;
public:
 uint64_t scans=0,pixels_scanned=0;
 bool opaque(SDL_Surface*s){
  if(!s||!s->format||!s->pixels||s->w<1||s->h<1||s->w>3840||s->h>2160||SDL_MUSTLOCK(s))return false;
  Uint32 key;Uint8 alpha;SDL_BlendMode mode;
  if(SDL_GetColorKey(s,&key)==0||SDL_GetSurfaceAlphaMod(s,&alpha)||SDL_GetSurfaceBlendMode(s,&mode))return false;
  if(mode==SDL_BLENDMODE_NONE)return true;
  if(mode!=SDL_BLENDMODE_BLEND||alpha!=255)return false;
  if(!s->format->Amask&&s->format->BytesPerPixel>=3)return true;
  if(s->format->BytesPerPixel!=4||s->pitch<s->w*4)return false;
  auto id=surface_generations::find(s);if(!id)return false;
  if(cached.id==id.id&&cached.generation==id.generation)return value;
  cached=id;value=true;++scans;
  for(int y=0;y<s->h&&value;++y)for(int x=0;x<s->w;++x){Uint32 pixel;std::memcpy(&pixel,static_cast<const Uint8*>(s->pixels)+size_t(y)*s->pitch+size_t(x)*4,4);++pixels_scanned;if((pixel&s->format->Amask)!=s->format->Amask){value=false;break;}}
  return value;
 }
};
}
