// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "x21/surface_generations.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "pixel_blend.hpp"
#include "scaled_blend.hpp"
#include <map>
#include <algorithm>
#include <cstdint>
#include <climits>
#include <vector>
namespace render_density {
inline int factor=1;
inline std::map<SDL_Surface*,int> surfaces;
inline std::map<TTF_Font*,int> fonts;
inline std::map<TTF_Font*,std::uint64_t> font_serials;
inline std::uint64_t next_font_serial=0;
inline std::uint64_t font_serial(TTF_Font* f){auto i=font_serials.find(f);return i==font_serials.end()?0:i->second;}
struct ClipSnapshot{SDL_Rect logical,physical;};
inline std::map<SDL_Surface*,std::vector<ClipSnapshot>> clips;
inline bool valid_density(int d){return d==1||d==2;}
inline int density(SDL_Surface* s){auto i=surfaces.find(s);return i!=surfaces.end()&&i->second==2?2:1;}
inline int font_density(TTF_Font* f){auto i=fonts.find(f);return i!=fonts.end()&&i->second==2?2:1;}
inline int ceil_extent(int n,int d){return n>0?int((int64_t(n)+d-1)/d):0;}
inline int logical_w(SDL_Surface* s){return s?ceil_extent(s->w,density(s)):0;}
inline int logical_h(SDL_Surface* s){return s?ceil_extent(s->h,density(s)):0;}
inline bool checked_rect(SDL_Rect r,int d,SDL_Rect& out){
 if(!valid_density(d)){SDL_SetError("Invalid render density");return false;}
 const int values[]={r.x,r.y,r.w,r.h};int scaled[4];
 for(int i=0;i<4;++i){const int64_t n=int64_t(values[i])*d;if(n<INT_MIN||n>INT_MAX){SDL_SetError("Render rectangle overflow");return false;}scaled[i]=int(n);}
 out={scaled[0],scaled[1],scaled[2],scaled[3]};return true;
}
inline SDL_Rect pixel_rect(SDL_Rect r,int d){SDL_Rect out{};checked_rect(r,d,out);return out;}
inline SDL_Surface* tag(SDL_Surface* s,int d){if(!valid_density(d)){SDL_SetError("Invalid render density");return nullptr;}if(s){surfaces[s]=d;clips.erase(s);}return s;}
inline SDL_Surface* create(int w,int h,Uint32 format=SDL_PIXELFORMAT_ARGB8888){
 if(!valid_density(factor)||w<=0||h<=0||w>8192/factor||h>8192/factor)return nullptr;
 return tag(SDL_CreateRGBSurfaceWithFormat(0,w*factor,h*factor,32,format),factor);
}
inline void forget_surface(SDL_Surface* s){surfaces.erase(s);clips.erase(s);}
inline void free_surface(SDL_Surface* s){surface_generations::invalidate(s);forget_surface(s);SDL_FreeSurface(s);}
inline int physical_fill(SDL_Surface* s,const SDL_Rect* r,Uint32 c){surface_generations::invalidate(s);return SDL_FillRect(s,r,c);}
inline int fill(SDL_Surface* s,const SDL_Rect* r,Uint32 c){if(!s)return SDL_SetError("Missing target");surface_generations::invalidate(s);if(density(s)==1)return SDL_FillRect(s,r,c);SDL_Rect q{};if(r&&!checked_rect(*r,density(s),q))return -1;return SDL_FillRect(s,r?&q:nullptr,c);}
inline bool same_rect(SDL_Rect a,SDL_Rect b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
inline SDL_bool set_clip(SDL_Surface* s,const SDL_Rect* r){
 if(!s||density(s)==1)return SDL_SetClipRect(s,r);
 SDL_Rect q{};auto saved=clips.find(s);
 // Restore an unmodified logical snapshot exactly, including odd physical edges.
 if(r&&saved!=clips.end()){
  auto& stack=saved->second;
  for(size_t n=stack.size();n>0;--n)if(same_rect(*r,stack[n-1].logical)){q=stack[n-1].physical;stack.erase(stack.begin()+n-1);return SDL_SetClipRect(s,&q);}
 }
 if(r&&!checked_rect(*r,density(s),q)){SDL_Rect empty{};SDL_SetClipRect(s,&empty);return SDL_FALSE;}
 return SDL_SetClipRect(s,r?&q:nullptr);
}
inline void get_clip(SDL_Surface* s,SDL_Rect* r){
 if(!r)return;
 SDL_GetClipRect(s,r);if(!s||density(s)==1)return;
 const SDL_Rect physical=*r;const int d=density(s);
 // SDL clips are nonnegative/intersected. Expose covering logical bounds;
 // the saved physical snapshot prevents get/set from expanding subpixel clips.
 const int left=r->x/d,top=r->y/d;
 const int right=int((int64_t(r->x)+r->w+d-1)/d),bottom=int((int64_t(r->y)+r->h+d-1)/d);
 *r={left,top,r->w>0?std::max(0,right-left):0,r->h>0?std::max(0,bottom-top):0};
 auto& stack=clips[s];if(stack.size()==32)stack.erase(stack.begin());stack.push_back({*r,physical});
}
inline bool try_scaled(SDL_Surface* s,const SDL_Rect* a,SDL_Surface* d,SDL_Rect* b){static scaled_blend::Workspace w;return w.blit(s,a,d,b);}
inline int blit_scaled(SDL_Surface* src,const SDL_Rect* sr,SDL_Surface* dst,SDL_Rect* dr){
 if(!src||!dst)return SDL_SetError("Missing blit surface"); surface_generations::invalidate(dst);
 if(density(src)==1&&density(dst)==1)return try_scaled(src,sr,dst,dr)?0:SDL_UpperBlitScaled(src,sr,dst,dr);
 SDL_Rect from{0,0,src->w,src->h},to{0,0,dst->w,dst->h};
 if((sr&&!checked_rect(*sr,density(src),from))||(dr&&!checked_rect(*dr,density(dst),to)))return -1;
 const int rc=(from.w==to.w&&from.h==to.h&&pixel_blend::blit(src,&from,dst,&to))?0:(try_scaled(src,&from,dst,&to)?0:SDL_UpperBlitScaled(src,&from,dst,&to));
 if(dr){int d=density(dst);*dr={to.x/d,to.y/d,ceil_extent(to.w,d),ceil_extent(to.h,d)};}return rc;
}
inline int blit(SDL_Surface* src,const SDL_Rect* sr,SDL_Surface* dst,SDL_Rect* dr){
 if(!src||!dst)return SDL_SetError("Missing blit surface"); surface_generations::invalidate(dst);
 const int sd=density(src),dd=density(dst);
 if(sd==1&&dd==1)return pixel_blend::blit(src,sr,dst,dr)?0:SDL_UpperBlit(src,sr,dst,dr);
 SDL_Rect from{0,0,src->w,src->h},position{dr?dr->x:0,dr?dr->y:0,0,0},to{};
 if((sr&&!checked_rect(*sr,sd,from))||!checked_rect(position,dd,to))return -1;
 const int64_t w=(int64_t(std::max(0,from.w))*dd+sd-1)/sd,h=(int64_t(std::max(0,from.h))*dd+sd-1)/sd;
 if(w>INT_MAX||h>INT_MAX)return SDL_SetError("Render blit extent overflow");
 to.w=int(w);to.h=int(h);
 const int rc=(from.w==to.w&&from.h==to.h)?(pixel_blend::blit(src,&from,dst,&to)?0:SDL_UpperBlit(src,&from,dst,&to)):(try_scaled(src,&from,dst,&to)?0:SDL_UpperBlitScaled(src,&from,dst,&to));
 if(dr)*dr={to.x/dd,to.y/dd,ceil_extent(to.w,dd),ceil_extent(to.h,dd)};
 return rc;
}
inline TTF_Font* open_font(const char* p,int size){if(!valid_density(factor)||size<=0||size>4096/factor){SDL_SetError("Invalid font size/density");return nullptr;}auto* f=TTF_OpenFont(p,size*factor);if(f){fonts[f]=factor;if(next_font_serial!=UINT64_MAX)font_serials[f]=++next_font_serial;}return f;}
inline void close_font(TTF_Font* f){font_serials.erase(f);fonts.erase(f);TTF_CloseFont(f);}
inline int font_height(TTF_Font* f){return ceil_extent(TTF_FontHeight(f),font_density(f));}
inline int size_utf8(TTF_Font* f,const char* t,int* w,int* h){const int d=font_density(f);if(d==1)return TTF_SizeUTF8(f,t,w,h);int pw=0,ph=0;const int rc=TTF_SizeUTF8(f,t,&pw,&ph);if(rc!=0)return rc;if(w)*w=ceil_extent(pw,d);if(h)*h=ceil_extent(ph,d);return rc;}
inline SDL_Surface* text(TTF_Font* f,const char* v,SDL_Color c){auto*s=tag(TTF_RenderUTF8_Blended(f,v,c),font_density(f));surface_generations::seal(s);return s;}
}
