// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
namespace solid_blend {
inline unsigned div255(unsigned x){return (x+1+(x>>8))>>8;}
inline Uint32 pixel(Uint32 dst,Uint32 src,unsigned a,bool has_alpha){
 Uint32 out=0;for(int shift=0;shift<24;shift+=8)out|=(div255(((src>>shift)&255)*a)+div255(((dst>>shift)&255)*(255-a)))<<shift;
 if(has_alpha)out|=(a+div255((dst>>24)*(255-a)))<<24;
 return out;
}
inline bool format_ok(SDL_Surface* s){return s&&s->format->BytesPerPixel==4&&s->format->Gmask==0xff00&&((s->format->Rmask==0xff0000&&s->format->Bmask==0xff)||(s->format->Bmask==0xff0000&&s->format->Rmask==0xff))&&(s->format->Amask==0xff000000||s->format->Amask==0);}
// Checked once per native SDL format; preserve reference path if its arithmetic differs.
inline bool compatible(Uint32 format){
 static Uint32 known[4]{};static bool good[4]{};static unsigned count=0;
 for(unsigned i=0;i<count;++i)if(known[i]==format)return good[i];
 auto* s=SDL_CreateRGBSurfaceWithFormat(0,1,1,32,SDL_PIXELFORMAT_ARGB8888);
 auto* d=SDL_CreateRGBSurfaceWithFormat(0,1,1,32,format);bool ok=s&&d&&format_ok(d);
 if(ok){SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_BLEND);for(unsigned a=1;a<255&&ok;++a){
   const Uint32 original=SDL_MapRGBA(d->format,240,80,11,65);const Uint32 color=SDL_MapRGBA(d->format,43,127,221,255);
   SDL_FillRect(s,nullptr,SDL_MapRGBA(s->format,43,127,221,255));SDL_SetSurfaceAlphaMod(s,Uint8(a));SDL_FillRect(d,nullptr,original);SDL_UpperBlitScaled(s,nullptr,d,nullptr);
   ok=*static_cast<Uint32*>(d->pixels)==pixel(original,color,a,d->format->Amask!=0);
 }}
 SDL_FreeSurface(s);SDL_FreeSurface(d);if(count<4){known[count]=format;good[count++]=ok;}return ok;
}
inline bool fill(SDL_Surface* dst,SDL_Rect area,unsigned rgb,Uint8 alpha){
 if(!format_ok(dst)||!compatible(dst->format->format))return false;
 if(!alpha && dst->format->Amask)return true;
 SDL_Rect clip;SDL_GetClipRect(dst,&clip);SDL_Rect r;if(!SDL_IntersectRect(&area,&clip,&r))return true;
 // SDL's1x1 scaler can drop narrow clipped coverage; retain that exact path.
 if(r.x!=area.x||r.y!=area.y||r.w!=area.w||r.h!=area.h)return false;
 const Uint32 src=SDL_MapRGBA(dst->format,(rgb>>16)&255,(rgb>>8)&255,rgb&255,255);
 if(alpha==255){SDL_FillRect(dst,&r,src);return true;}
 if(SDL_MUSTLOCK(dst)&&SDL_LockSurface(dst)!=0)return false;
 const bool has_alpha=dst->format->Amask!=0;
#if defined(__SSE2__)
 const __m128i zero=_mm_setzero_si128(),one=_mm_set1_epi16(1),weight=_mm_set1_epi16(255-alpha);
 const Uint32 constant=pixel(0,src,alpha,has_alpha);const __m128i add=_mm_set1_epi32(int(constant));
 const __m128i mask=_mm_set1_epi32(has_alpha?-1:0x00ffffff);
#endif
 for(int y=r.y;y<r.y+r.h;++y){auto* p=reinterpret_cast<Uint32*>(static_cast<Uint8*>(dst->pixels)+y*dst->pitch)+r.x;int x=0;
#if defined(__SSE2__)
 for(;x+4<=r.w;x+=4){const __m128i v=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+x));
   auto scale=[&](__m128i z){z=_mm_mullo_epi16(z,weight);return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(z,one),_mm_srli_epi16(z,8)),8);};
   const __m128i lo=scale(_mm_unpacklo_epi8(v,zero)),hi=scale(_mm_unpackhi_epi8(v,zero));
   const __m128i out=_mm_add_epi8(_mm_and_si128(_mm_packus_epi16(lo,hi),mask),add);_mm_storeu_si128(reinterpret_cast<__m128i*>(p+x),out);
 }
#endif
 for(;x<r.w;++x)p[x]=pixel(p[x],src,alpha,has_alpha);
 }
 if(SDL_MUSTLOCK(dst))SDL_UnlockSurface(dst);
 return true;
}
}


