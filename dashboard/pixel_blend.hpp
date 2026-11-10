// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <cstring>
#include <algorithm>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
// Physical-coordinate unscaled SDL_BLENDMODE_BLEND only. Serial renderer use.
// Returns false without changing caller rectangles/pixels for unsupported cases.
namespace pixel_blend {
inline unsigned div255(unsigned x){return (x+1+(x>>8))>>8;}
inline Uint32 pixel(Uint32 src,Uint32 dst,Uint8 mod,bool dst_alpha){
 unsigned a=src>>24;const bool direct=mod==255;
 if(direct){if(!a)return dst;if(a==255)return src;}
 else a=div255(a*mod);
 Uint32 result=0;
 for(int shift:{0,8,16}){unsigned s=(src>>shift)&255,d=(dst>>shift)&255;unsigned v=direct?((s*a)>>8)+((d*(255-a))>>8):div255(s*a)+div255(d*(255-a));result|=v<<shift;}
 if(direct||dst_alpha)result|=(a+(direct?(((dst>>24)*(255-a))>>8):div255((dst>>24)*(255-a))))<<24;
 return result;
}
inline bool formats(SDL_Surface* s,SDL_Surface* d){
 if(!s||!d||s->format->BytesPerPixel!=4||d->format->BytesPerPixel!=4)return false;
 const auto a=s->format->format,b=d->format->format;
 return (a==SDL_PIXELFORMAT_ARGB8888&&(b==SDL_PIXELFORMAT_ARGB8888||b==SDL_PIXELFORMAT_RGB888)) ||
        (a==SDL_PIXELFORMAT_ABGR8888&&(b==SDL_PIXELFORMAT_ABGR8888||b==SDL_PIXELFORMAT_BGR888));
}
inline bool compatible(Uint32 sf,Uint32 df){
 struct Entry{Uint32 s,d;bool ok;};static Entry entries[4]{};static unsigned count=0;
 for(unsigned i=0;i<count;++i)if(entries[i].s==sf&&entries[i].d==df)return entries[i].ok;
 auto* s=SDL_CreateRGBSurfaceWithFormat(0,17,1,32,sf);auto* d=SDL_CreateRGBSurfaceWithFormat(0,17,1,32,df);bool ok=s&&d;Uint32 expected[17]{};
 if(ok){SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_BLEND);for(unsigned mod:{0u,1u,127u,254u,255u})for(unsigned seed=0;seed<16&&ok;++seed){
   for(unsigned x=0;x<17;++x){unsigned alpha=(x*17+seed*29)&255;if(x<3)alpha=x==0?0:x==1?1:255;Uint32 src=SDL_MapRGBA(s->format,Uint8(x*43+seed),Uint8(x*19+97),Uint8(seed*71+x),Uint8(alpha));Uint32 dst=(seed*173487251u+x*1127987u)^0x41f0500bu;
    std::memcpy(static_cast<char*>(s->pixels)+x*4,&src,4);std::memcpy(static_cast<char*>(d->pixels)+x*4,&dst,4);expected[x]=pixel(src,dst,Uint8(mod),d->format->Amask!=0);
   }
   SDL_SetSurfaceAlphaMod(s,Uint8(mod));SDL_UpperBlit(s,nullptr,d,nullptr);
   for(unsigned x=0;x<17;++x){Uint32 got;std::memcpy(&got,static_cast<char*>(d->pixels)+x*4,4);if(got!=expected[x])ok=false;}
 }}
 SDL_FreeSurface(s);SDL_FreeSurface(d);if(count<4)entries[count++]={sf,df,ok};return ok;
}
inline void row(const char* src,char* dst,int width,Uint8 mod,bool dst_alpha){
 int x=0;
#if defined(__SSE2__)
 const __m128i zero=_mm_setzero_si128(),one=_mm_set1_epi16(1),full=_mm_set1_epi16(255),m=_mm_set1_epi16(mod);
 const __m128i alpha_lanes=_mm_set_epi16(-1,0,0,0,-1,0,0,0),rgb_lanes=_mm_xor_si128(alpha_lanes,_mm_set1_epi32(-1));
 auto divide=[&](__m128i z){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(z,one),_mm_srli_epi16(z,8)),8);};
 for(;x+4<=width;x+=4){const __m128i sp=_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+x*4)),dp=_mm_loadu_si128(reinterpret_cast<const __m128i*>(dst+x*4));
  auto blend=[&](__m128i s,__m128i d){__m128i a=_mm_shufflelo_epi16(s,_MM_SHUFFLE(3,3,3,3));a=_mm_shufflehi_epi16(a,_MM_SHUFFLE(3,3,3,3));if(mod!=255)a=divide(_mm_mullo_epi16(a,m));
   const __m128i w=_mm_sub_epi16(full,a);__m128i sc=_mm_mullo_epi16(_mm_and_si128(s,rgb_lanes),a),dc=_mm_mullo_epi16(d,w);
   sc=mod==255?_mm_srli_epi16(sc,8):divide(sc);dc=mod==255?_mm_srli_epi16(dc,8):divide(dc);
   __m128i out=_mm_add_epi16(_mm_add_epi16(sc,dc),_mm_and_si128(a,alpha_lanes));
   if(mod==255){const __m128i transparent=_mm_cmpeq_epi16(a,zero),opaque=_mm_cmpeq_epi16(a,full);out=_mm_or_si128(_mm_and_si128(transparent,d),_mm_andnot_si128(transparent,out));out=_mm_or_si128(_mm_and_si128(opaque,s),_mm_andnot_si128(opaque,out));}
   else if(!dst_alpha)out=_mm_and_si128(out,rgb_lanes);
   return out;};
  const __m128i lo=blend(_mm_unpacklo_epi8(sp,zero),_mm_unpacklo_epi8(dp,zero)),hi=blend(_mm_unpackhi_epi8(sp,zero),_mm_unpackhi_epi8(dp,zero));_mm_storeu_si128(reinterpret_cast<__m128i*>(dst+x*4),_mm_packus_epi16(lo,hi));
 }
#endif
 for(;x<width;++x){Uint32 s,d;std::memcpy(&s,src+x*4,4);std::memcpy(&d,dst+x*4,4);d=pixel(s,d,mod,dst_alpha);std::memcpy(dst+x*4,&d,4);}
}
inline bool blit(SDL_Surface* src,const SDL_Rect* source,SDL_Surface* dst,SDL_Rect* destination){
 if(!formats(src,dst)||src==dst||!src->pixels||!dst->pixels||SDL_MUSTLOCK(src)||SDL_MUSTLOCK(dst)||src->pitch<0||dst->pitch<0||src->pitch%4||dst->pitch%4||src->w>src->pitch/4||dst->w>dst->pitch/4)return false;
 SDL_BlendMode mode;Uint8 r,g,b,mod;Uint32 key;
 if(SDL_GetSurfaceBlendMode(src,&mode)||mode!=SDL_BLENDMODE_BLEND||SDL_GetColorKey(src,&key)==0||SDL_GetSurfaceColorMod(src,&r,&g,&b)||r!=255||g!=255||b!=255||SDL_GetSurfaceAlphaMod(src,&mod))return false;
 const auto sb=reinterpret_cast<std::uintptr_t>(src->pixels),db=reinterpret_cast<std::uintptr_t>(dst->pixels);const auto sn=std::size_t(src->pitch)*std::size_t(src->h),dn=std::size_t(dst->pitch)*std::size_t(dst->h);
 if(sb<=db?(db-sb<sn):(sb-db<dn))return false;
 if(!compatible(src->format->format,dst->format->format))return false;
 SDL_Rect from=source?*source:SDL_Rect{0,0,src->w,src->h};int dx=destination?destination->x:0,dy=destination?destination->y:0;
 // Bound arithmetic before adding/clipping signed coordinates.
 for(int v:{from.x,from.y,from.w,from.h,dx,dy})if(v < -100000000 || v>100000000)return false;
 if(from.x<0){dx-=from.x;from.w+=from.x;from.x=0;}if(from.y<0){dy-=from.y;from.h+=from.y;from.y=0;}
 from.w=std::min(from.w,src->w-from.x);from.h=std::min(from.h,src->h-from.y);
 SDL_Rect to{dx,dy,from.w,from.h},clip=dst->clip_rect;
 if(to.x<clip.x){int n=clip.x-to.x;from.x+=n;to.w-=n;to.x=clip.x;}if(to.y<clip.y){int n=clip.y-to.y;from.y+=n;to.h-=n;to.y=clip.y;}
 to.w=std::min(to.w,clip.x+clip.w-to.x);to.h=std::min(to.h,clip.y+clip.h-to.y);
 if(to.w<=0||to.h<=0){if(destination){destination->w=0;destination->h=0;}return true;}
 for(int y=0;y<to.h;++y)row(static_cast<const char*>(src->pixels)+(from.y+y)*src->pitch+from.x*4,static_cast<char*>(dst->pixels)+(to.y+y)*dst->pitch+to.x*4,to.w,mod,dst->format->Amask!=0);
 if(destination)*destination=to;
 return true;
}
}


