// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
// Opaque full-frame video only. Separable pixel-center bilinear, Q8 weights,
// rounded to8-bit after EACH axis. Not a replacement for static-art Lanczos.
namespace realtime_scale {
struct Axis {int lo=0,hi=0;unsigned weight=0;};
inline Axis coordinate(int at,int source,int target){
 const int q=int((std::int64_t(2*at+1)*source*128)/target)-128;
 if(q<=0)return {0,0,0};
 if(q>=(source-1)*256)return {source-1,source-1,0};
 return {q/256,q/256+1,unsigned(q%256)};
}
inline Uint32 mix(Uint32 a,Uint32 b,unsigned w){Uint32 out=0;for(int sh:{0,8,16,24})out|=(((((a>>sh)&255)*(256-w)+((b>>sh)&255)*w+128)>>8)<<sh);return out;}
class Workspace {
 static constexpr int max_w=3840,max_h=2160;
 std::array<Axis,max_w> xp_{};std::array<Axis,max_h> yp_{};
 std::array<std::array<Uint32,max_w>,2> rows_{};
 int sw_=0,sh_=0,dw_=0,dh_=0;
 static Uint32 read(const char* p){Uint32 v;std::memcpy(&v,p,4);return v;}
 void horizontal(const char* source,Uint32* target){int x=0;
#if defined(__SSE2__)
  const __m128i zero=_mm_setzero_si128(),full=_mm_set1_epi16(256),round=_mm_set1_epi16(128);
  for(;x+4<=dw_;x+=4){const Axis* q=xp_.data()+x;
   const __m128i a=_mm_set_epi32(int(read(source+q[3].lo*4)),int(read(source+q[2].lo*4)),int(read(source+q[1].lo*4)),int(read(source+q[0].lo*4)));
   const __m128i b=_mm_set_epi32(int(read(source+q[3].hi*4)),int(read(source+q[2].hi*4)),int(read(source+q[1].hi*4)),int(read(source+q[0].hi*4)));
   const __m128i wl=_mm_set_epi16(short(q[1].weight),short(q[1].weight),short(q[1].weight),short(q[1].weight),short(q[0].weight),short(q[0].weight),short(q[0].weight),short(q[0].weight));
   const __m128i wh=_mm_set_epi16(short(q[3].weight),short(q[3].weight),short(q[3].weight),short(q[3].weight),short(q[2].weight),short(q[2].weight),short(q[2].weight),short(q[2].weight));
   auto blend=[&](__m128i aa,__m128i bb,__m128i w){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(aa,_mm_sub_epi16(full,w)),_mm_mullo_epi16(bb,w)),round),8);};
   _mm_storeu_si128(reinterpret_cast<__m128i*>(target+x),_mm_packus_epi16(blend(_mm_unpacklo_epi8(a,zero),_mm_unpacklo_epi8(b,zero),wl),blend(_mm_unpackhi_epi8(a,zero),_mm_unpackhi_epi8(b,zero),wh)));
  }
#endif
  for(;x<dw_;++x){const auto q=xp_[x];target[x]=mix(read(source+q.lo*4),read(source+q.hi*4),q.weight);}
 }
 static void vertical(const Uint32* a,const Uint32* b,char* dst,int width,unsigned weight){int x=0;
#if defined(__SSE2__)
  const __m128i zero=_mm_setzero_si128(),w=_mm_set1_epi16(short(weight)),iw=_mm_set1_epi16(short(256-weight)),round=_mm_set1_epi16(128);
  for(;x+4<=width;x+=4){const __m128i aa=_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+x)),bb=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+x));
   auto blend=[&](__m128i av,__m128i bv){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(av,iw),_mm_mullo_epi16(bv,w)),round),8);};
   _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+x*4),_mm_packus_epi16(blend(_mm_unpacklo_epi8(aa,zero),_mm_unpacklo_epi8(bb,zero)),blend(_mm_unpackhi_epi8(aa,zero),_mm_unpackhi_epi8(bb,zero))));
  }
#endif
  for(;x<width;++x){Uint32 v=mix(a[x],b[x],weight);std::memcpy(dst+x*4,&v,4);}
 }
public:
 // Persistent workspace, single caller at a time. No allocation in scale().
 // false: destination unchanged; caller retains its previous scaler fallback.
 bool scale(SDL_Surface* source,SDL_Surface* target){
  if(!source||!target||!source->pixels||!target->pixels||SDL_MUSTLOCK(source)||SDL_MUSTLOCK(target))return false;
  int sw=source->w,sh=source->h,dw=target->w,dh=target->h;
  if(sw<1||sh<1||dw<1||dh<1||sw>max_w||dw>max_w||sh>max_h||dh>max_h||source->pitch<sw*4||target->pitch<dw*4)return false;
  const Uint32 sf=source->format->format,df=target->format->format;
  if(!((sf==SDL_PIXELFORMAT_ARGB8888&&(df==sf||df==SDL_PIXELFORMAT_RGB888))||(sf==SDL_PIXELFORMAT_ABGR8888&&(df==sf||df==SDL_PIXELFORMAT_BGR888))))return false;
  if(target->clip_rect.x||target->clip_rect.y||target->clip_rect.w!=dw||target->clip_rect.h!=dh)return false;
  Uint8 r,g,b,a;Uint32 key;if(SDL_GetSurfaceColorMod(source,&r,&g,&b)||r!=255||g!=255||b!=255||SDL_GetSurfaceAlphaMod(source,&a)||a!=255||SDL_GetColorKey(source,&key)==0)return false;
  const auto sb=reinterpret_cast<std::uintptr_t>(source->pixels),db=reinterpret_cast<std::uintptr_t>(target->pixels);const auto sn=std::size_t(source->pitch)*sh,dn=std::size_t(target->pitch)*dh;if(sb<=db?(db-sb<sn):(sb-db<dn))return false;
  // Reject transparent input before any destination write; alpha must be opaque.
  for(int y=0;y<sh;++y){const char* row=static_cast<const char*>(source->pixels)+std::size_t(y)*source->pitch;int x=0;
#if defined(__SSE2__)
   const __m128i alpha=_mm_set1_epi32(int(0xff000000u));for(;x+4<=sw;x+=4){__m128i v=_mm_loadu_si128(reinterpret_cast<const __m128i*>(row+x*4));if(_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_and_si128(v,alpha),alpha))!=65535)return false;}
#endif
   for(;x<sw;++x)if((read(row+x*4)>>24)!=255)return false;
  }
  // Identity still validates opacity/state before any write.
  if(sw==dw&&sh==dh){for(int y=0;y<sh;++y)std::memcpy(static_cast<char*>(target->pixels)+std::size_t(y)*target->pitch,static_cast<const char*>(source->pixels)+std::size_t(y)*source->pitch,std::size_t(sw)*4);return true;}
  if(sw!=sw_||sh!=sh_||dw!=dw_||dh!=dh_){sw_=sw;sh_=sh;dw_=dw;dh_=dh;for(int x=0;x<dw;++x)xp_[x]=coordinate(x,sw,dw);for(int y=0;y<dh;++y)yp_[y]=coordinate(y,sh,dh);}
  int ids[2]={-1,-1};auto ensure=[&](int y,int protect){for(int i=0;i<2;++i)if(ids[i]==y)return i;int slot=protect==0?1:0;horizontal(static_cast<const char*>(source->pixels)+std::size_t(y)*source->pitch,rows_[slot].data());ids[slot]=y;return slot;};
  for(int y=0;y<dh;++y){const auto q=yp_[y];int first=ensure(q.lo,-1),second=ensure(q.hi,first);vertical(rows_[first].data(),rows_[second].data(),static_cast<char*>(target->pixels)+std::size_t(y)*target->pitch,dw,q.weight);}
  return true;
 }
};
}


