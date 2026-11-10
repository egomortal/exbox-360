// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fullapp_gpu_hook.hpp"
// UI-side command redirection only. Existing SDL layer is an audit/fallback
// surface; GPU mode never uploads it. Exactly one shared group layer is active.
namespace x21layers {
class Scope {
 TabSink*sink=nullptr;SDL_Surface*saved_target=nullptr;SDL_Rect source{},saved_clip{};uint32_t saved_destination=0;bool saved_sdl_layer=false;
public:
 Scope()=default;Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
 bool active()const{return sink!=nullptr;}
 bool begin(SDL_Surface*from,SDL_Surface*layer,SDL_Rect logical_source){
  if(sink||!x21hook::recording(from)||!layer||x21hook::active->target!=0||layer->w!=from->w||layer->h!=from->h||render_density::density(layer)!=render_density::density(from))return false;
  SDL_Rect physical;if(!render_density::checked_rect(logical_source,render_density::density(layer),physical)||physical.x<0||physical.y<0||physical.w<1||physical.h<1||int64_t(physical.x)+physical.w>layer->w||int64_t(physical.y)+physical.h>layer->h)return false;
  sink=x21hook::active;saved_target=x21hook::target;saved_clip=sink->clip;saved_destination=sink->target;saved_sdl_layer=sink->sdl_layer;sink->sdl_layer=true;source=physical;
  sink->target=1;sink->clip={0,0,sink->width,sink->height};auto c=sink->base(sink->clip);c.kind=EXBOX_GPU_CLEAR_LAYER;c.top=0;sink->push(c);sink->clip=layer->clip_rect;x21hook::target=layer;return true;
 }
 bool end(SDL_Rect logical_destination,float alpha){
  if(!sink)return false;auto*s=sink;SDL_Rect destination{};bool valid=std::isfinite(alpha)&&render_density::checked_rect(logical_destination,render_density::density(saved_target),destination)&&destination.w>0&&destination.h>0;
  x21hook::target=saved_target;s->target=saved_destination;s->sdl_layer=saved_sdl_layer;s->clip=saved_target->clip_rect;sink=nullptr;
  if(!valid){s->packet.unsupported.push_back("group_destination_invalid");s->clip=saved_clip;return false;}
  auto c=s->base(destination);c.kind=EXBOX_GPU_SDL_GROUP;c.blend=0;c.opacity=Uint8(255*std::clamp(alpha,0.f,1.f));c.transform[0]=float(source.x);c.transform[1]=float(source.y);c.transform[2]=float(source.w);c.transform[3]=float(source.h);c.transform[8]=1;s->push(c);s->clip=saved_clip;return true;
 }
 ~Scope(){if(sink){sink->packet.unsupported.push_back("unclosed_effect_layer");sink->target=saved_destination;sink->sdl_layer=saved_sdl_layer;sink->clip=saved_clip;x21hook::target=saved_target;}}
};
}
