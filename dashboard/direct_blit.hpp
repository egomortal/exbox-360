// Test-only proposal; production unchanged. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <cstdint>
#include <limits>
namespace direct_blit {
inline bool range(SDL_Surface*s,std::uintptr_t&begin,std::uintptr_t&end){
 if(!s||!s->pixels||s->pitch<=0||s->h<=0)return false;
 const auto pitch=std::uintptr_t(s->pitch),height=std::uintptr_t(s->h);
 if(height>std::numeric_limits<std::uintptr_t>::max()/pitch)return false;
 const auto bytes=pitch*height;begin=reinterpret_cast<std::uintptr_t>(s->pixels);
 if(bytes>std::numeric_limits<std::uintptr_t>::max()-begin)return false;
 end=begin+bytes;return true;
}
inline bool eligible(SDL_Surface*source,SDL_Surface*target,const SDL_Rect&from,const SDL_Rect&to){
 if(!source||!target||!source->format||!target->format||source==target||source->locked||target->locked)return false;
 if(source->format->format!=SDL_PIXELFORMAT_ARGB8888||target->format->format!=SDL_PIXELFORMAT_ARGB8888)return false;
 if(source->w<=0||source->w>std::numeric_limits<int>::max()/4||source->pitch!=source->w*4)return false;
 if(from.x||from.y||from.w!=source->w||from.h!=source->h||to.w!=source->w||to.h!=source->h)return false;
 const auto c=source->clip_rect;if(c.x||c.y||c.w!=source->w||c.h!=source->h)return false;
 if(SDL_HasColorKey(source)||SDL_HasSurfaceRLE(source)||SDL_HasSurfaceRLE(target))return false;
 SDL_BlendMode mode;if(SDL_GetSurfaceBlendMode(source,&mode)!=0)return false;
 if(mode!=SDL_BLENDMODE_NONE&&mode!=SDL_BLENDMODE_BLEND&&mode!=SDL_BLENDMODE_ADD&&mode!=SDL_BLENDMODE_MOD)return false;
 std::uintptr_t sb,se,tb,te;if(!range(source,sb,se)||!range(target,tb,te))return false;
 return se<=tb||te<=sb;
}
}
