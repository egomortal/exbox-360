// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "render_density.hpp"
namespace video_output_storage {
// Serial render owner. At most one active output wrapper; free that wrapper
// before clear/destruction. Wrapper destruction never releases backing pixels.
class Backing {
 SDL_Surface* storage_=nullptr;
public:
 Backing()=default;Backing(const Backing&)=delete;Backing& operator=(const Backing&)=delete;
 ~Backing(){clear();}
 bool prepare(int width,int height){
  if(width<1||height<1||width>3840||height>2160)return false;
  if(storage_)return storage_->w==width&&storage_->h==height;
  storage_=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
  if(!storage_)return false;
  if(SDL_FillRect(storage_,nullptr,0)!=0){clear();return false;}
  return true;
 }
 SDL_Surface* create(int width,int height,int density){
  if(width<1||height<1||width>3840||height>2160||!render_density::valid_density(density))return nullptr;
  SDL_Surface* output=nullptr;
  if(storage_&&width<=storage_->w&&height<=storage_->h)
   output=SDL_CreateRGBSurfaceWithFormatFrom(storage_->pixels,width,height,32,storage_->pitch,SDL_PIXELFORMAT_ARGB8888);
  // Missing/failed backing or wrapper construction preserves the prior owned
  // SDL allocation path. The caller overwrites all visible output pixels.
  if(!output)output=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
  return render_density::tag(output,density);
 }
 void clear(){if(storage_){SDL_FreeSurface(storage_);storage_=nullptr;}}
 size_t bytes()const{return storage_?size_t(storage_->pitch)*size_t(storage_->h):0;}
 int pitch()const{return storage_?storage_->pitch:0;}
};
}
