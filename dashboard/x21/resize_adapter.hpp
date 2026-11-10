// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_resample.hpp"
#include <SDL.h>
#include <cstring>
#include <memory>
#include <tuple>
#include <stdexcept>
namespace resize_adapter {
// Immutable publication identity. Crop/output are physical, never logical.
// Source generation must change for pixel/format/colour-key changes. Mods remain
// downstream draw state and are deliberately absent from this cache key.
struct Key {
 uint64_t owner=0,id=0,generation=0;int x=0,y=0,w=0,h=0,dw=0,dh=0;uint32_t filter=1;
 bool operator==(const Key&o)const{return std::tie(owner,id,generation,x,y,w,h,dw,dh,filter)==std::tie(o.owner,o.id,o.generation,o.x,o.y,o.w,o.h,o.dw,o.dh,o.filter);}
};
struct Prepared {std::vector<uint32_t>pixels,taps;uint32_t sw=0,sh=0,dw=0,dh=0;bool alpha=false;};
inline std::vector<uint32_t> table(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh){
 if(!sw||!sh||!dw||!dh||sw>8192||sh>8192||dw>8192||dh>8192)throw std::invalid_argument("resize dimensions");
 auto x=image_quality::make_axis(int(sw),int(dw)),y=image_quality::make_axis(int(sh),int(dh));
 std::vector<uint32_t> out(size_t(dw+dh)*3);size_t j=0;
 for(auto*a:{&x,&y})for(auto&t:a->taps){if(out.size()+t.weights.size()>1048576)throw std::length_error("resize taps");out[j*3]=uint32_t(t.first);out[j*3+1]=uint32_t(t.weights.size());out[j*3+2]=uint32_t(out.size());for(float w:t.weights){uint32_t bits;std::memcpy(&bits,&w,4);out.push_back(bits);}++j;}
 return out;
}
// UI/publication-owner only: mirrors blit_quality's original SDL preparation.
// No CPU filtering. Can be retained by immutable crop key, uploaded once, then
// passed as a borrowed source buffer to resize_prepare. Does not retain SDL pointers.
inline bool prepare(SDL_Surface*source,SDL_Rect crop,int dw,int dh,Prepared&out){
 if(!source||crop.w<1||crop.h<1||dw<1||dh<1||crop.w>8192||crop.h>8192||dw>8192||dh>8192||uint64_t(crop.w)*crop.h*4>67108864||uint64_t(dw)*dh*4>67108864)return false;
 // Avoid SDL signed coordinate overflow; clipping itself remains SDL-authoritative.
 if(int64_t(crop.x)+crop.w>INT32_MAX||int64_t(crop.y)+crop.h>INT32_MAX)return false;
 try {
  std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> input(SDL_CreateRGBSurfaceWithFormat(0,crop.w,crop.h,32,SDL_PIXELFORMAT_ARGB8888),SDL_FreeSurface);if(!input)return false;
  SDL_BlendMode blend;Uint8 alpha,r,g,b;
  if(SDL_GetSurfaceBlendMode(source,&blend)||SDL_GetSurfaceAlphaMod(source,&alpha)||SDL_GetSurfaceColorMod(source,&r,&g,&b))return false;
  SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);SDL_SetSurfaceAlphaMod(source,255);SDL_SetSurfaceColorMod(source,255,255,255);
  SDL_Rect from=crop;int result=SDL_UpperBlit(source,&from,input.get(),nullptr);
  SDL_SetSurfaceBlendMode(source,blend);SDL_SetSurfaceAlphaMod(source,alpha);SDL_SetSurfaceColorMod(source,r,g,b);if(result)return false;
  Prepared p;p.sw=uint32_t(crop.w);p.sh=uint32_t(crop.h);p.dw=uint32_t(dw);p.dh=uint32_t(dh);p.alpha=source->format->Amask!=0;p.pixels.resize(size_t(p.sw)*p.sh);
  for(uint32_t y=0;y<p.sh;y++)std::memcpy(p.pixels.data()+size_t(y)*p.sw,static_cast<char*>(input->pixels)+size_t(y)*input->pitch,size_t(p.sw)*4);
  if(p.sw!=p.dw||p.sh!=p.dh)p.taps=table(p.sw,p.sh,p.dw,p.dh);out=std::move(p);return true;
 }catch(const std::bad_alloc&){return false;}catch(const std::length_error&){return false;}
}
}
