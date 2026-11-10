// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pixel_blend.hpp"
#include <array>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
namespace scaled_blend {
// SDL nearest scaled BLEND uses /255 arithmetic at every alpha, unlike its
// unscaled mod255 shortcut. Physical coordinates; serial renderer ownership.
inline Uint32 pixel(Uint32 s,Uint32 d,Uint8 mod,bool alpha){
 const unsigned a=pixel_blend::div255((s>>24)*mod);Uint32 out=0;
 for(int sh:{0,8,16})out|=(pixel_blend::div255(((s>>sh)&255)*a)+pixel_blend::div255(((d>>sh)&255)*(255-a)))<<sh;
 if(alpha)out|=(a+pixel_blend::div255((d>>24)*(255-a)))<<24;
 return out;
}
inline void row(const Uint32* source,char* target,int width,Uint8 mod,bool alpha){
 int x=0;
#if defined(__AVX2__)
 const __m256i z=_mm256_setzero_si256(),one=_mm256_set1_epi16(1),full=_mm256_set1_epi16(255),m=_mm256_set1_epi16(mod);
 const __m256i al=_mm256_set_epi16(-1,0,0,0,-1,0,0,0,-1,0,0,0,-1,0,0,0),rgb=_mm256_xor_si256(al,_mm256_set1_epi32(-1));
 auto divide=[&](__m256i v){return _mm256_srli_epi16(_mm256_add_epi16(_mm256_add_epi16(v,one),_mm256_srli_epi16(v,8)),8);};
 for(;x+8<=width;x+=8){const __m256i s=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source+x)),d=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(target+x*4));
  auto blend=[&](__m256i a,__m256i b){__m256i w=_mm256_shufflelo_epi16(a,_MM_SHUFFLE(3,3,3,3));w=_mm256_shufflehi_epi16(w,_MM_SHUFFLE(3,3,3,3));w=divide(_mm256_mullo_epi16(w,m));
   __m256i out=_mm256_add_epi16(divide(_mm256_mullo_epi16(_mm256_and_si256(a,rgb),w)),divide(_mm256_mullo_epi16(b,_mm256_sub_epi16(full,w))));
   if(alpha)out=_mm256_add_epi16(out,_mm256_and_si256(w,al));else out=_mm256_and_si256(out,rgb);return out;};
  const __m256i lo=blend(_mm256_unpacklo_epi8(s,z),_mm256_unpacklo_epi8(d,z)),hi=blend(_mm256_unpackhi_epi8(s,z),_mm256_unpackhi_epi8(d,z));_mm256_storeu_si256(reinterpret_cast<__m256i*>(target+x*4),_mm256_packus_epi16(lo,hi));
 }
#elif defined(__SSE2__)
 const __m128i z=_mm_setzero_si128(),one=_mm_set1_epi16(1),full=_mm_set1_epi16(255),m=_mm_set1_epi16(mod);
 const __m128i al=_mm_set_epi16(-1,0,0,0,-1,0,0,0),rgb=_mm_xor_si128(al,_mm_set1_epi32(-1));
 auto divide=[&](__m128i v){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(v,one),_mm_srli_epi16(v,8)),8);};
 for(;x+4<=width;x+=4){const __m128i s=_mm_loadu_si128(reinterpret_cast<const __m128i*>(source+x)),d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(target+x*4));
  auto blend=[&](__m128i a,__m128i b){__m128i w=_mm_shufflelo_epi16(a,_MM_SHUFFLE(3,3,3,3));w=_mm_shufflehi_epi16(w,_MM_SHUFFLE(3,3,3,3));w=divide(_mm_mullo_epi16(w,m));
   __m128i out=_mm_add_epi16(divide(_mm_mullo_epi16(_mm_and_si128(a,rgb),w)),divide(_mm_mullo_epi16(b,_mm_sub_epi16(full,w))));
   if(alpha)out=_mm_add_epi16(out,_mm_and_si128(w,al));else out=_mm_and_si128(out,rgb);return out;};
  const __m128i lo=blend(_mm_unpacklo_epi8(s,z),_mm_unpacklo_epi8(d,z)),hi=blend(_mm_unpackhi_epi8(s,z),_mm_unpackhi_epi8(d,z));_mm_storeu_si128(reinterpret_cast<__m128i*>(target+x*4),_mm_packus_epi16(lo,hi));
 }
#endif
 for(;x<width;++x){Uint32 d;std::memcpy(&d,target+x*4,4);d=pixel(source[x],d,mod,alpha);std::memcpy(target+x*4,&d,4);}
}
// The caller proves opacity by AND-reducing the actual gathered row pixels.
inline void opaque_row(const Uint32* source,char* target,int width,Uint8 mod,bool alpha){
 if(mod==0&&alpha)return;
 if(mod==255&&alpha){std::memcpy(target,source,size_t(width)*4);return;}
 int x=0;
#if defined(__AVX2__)
 const __m256i z=_mm256_setzero_si256(),one=_mm256_set1_epi16(1),m=_mm256_set1_epi16(mod),inverse=_mm256_set1_epi16(255-mod),mask=_mm256_set1_epi32(alpha?-1:0x00ffffff);
 auto divide=[&](__m256i v){return _mm256_srli_epi16(_mm256_add_epi16(_mm256_add_epi16(v,one),_mm256_srli_epi16(v,8)),8);};
 for(;x+8<=width;x+=8){const __m256i s=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source+x)),d=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(target+x*4));
  auto blend=[&](__m256i a,__m256i b){return _mm256_add_epi16(divide(_mm256_mullo_epi16(a,m)),divide(_mm256_mullo_epi16(b,inverse)));};
  const __m256i lo=blend(_mm256_unpacklo_epi8(s,z),_mm256_unpacklo_epi8(d,z)),hi=blend(_mm256_unpackhi_epi8(s,z),_mm256_unpackhi_epi8(d,z));_mm256_storeu_si256(reinterpret_cast<__m256i*>(target+x*4),_mm256_and_si256(_mm256_packus_epi16(lo,hi),mask));
 }
#elif defined(__SSE2__)
 const __m128i z=_mm_setzero_si128(),one=_mm_set1_epi16(1),m=_mm_set1_epi16(mod),inverse=_mm_set1_epi16(255-mod),mask=_mm_set1_epi32(alpha?-1:0x00ffffff);
 auto divide=[&](__m128i v){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(v,one),_mm_srli_epi16(v,8)),8);};
 for(;x+4<=width;x+=4){const __m128i s=_mm_loadu_si128(reinterpret_cast<const __m128i*>(source+x)),d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(target+x*4));
  auto blend=[&](__m128i a,__m128i b){return _mm_add_epi16(divide(_mm_mullo_epi16(a,m)),divide(_mm_mullo_epi16(b,inverse)));};
  const __m128i lo=blend(_mm_unpacklo_epi8(s,z),_mm_unpacklo_epi8(d,z)),hi=blend(_mm_unpackhi_epi8(s,z),_mm_unpackhi_epi8(d,z));_mm_storeu_si128(reinterpret_cast<__m128i*>(target+x*4),_mm_and_si128(_mm_packus_epi16(lo,hi),mask));
 }
#endif
 for(;x<width;++x){Uint32 d;std::memcpy(&d,target+x*4,4);d=pixel(source[x],d,mod,alpha);std::memcpy(target+x*4,&d,4);}
}
inline bool compatible(Uint32 sf,Uint32 df){
 struct Entry{Uint32 s,d;bool ok;};static Entry entries[4]{};static unsigned used=0;
 for(unsigned i=0;i<used;++i)if(entries[i].s==sf&&entries[i].d==df)return entries[i].ok;
 auto*s=SDL_CreateRGBSurfaceWithFormat(0,19,13,32,sf);auto*d=SDL_CreateRGBSurfaceWithFormat(0,29,23,32,df);bool ok=s&&d;
 if(ok){SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_BLEND);for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){unsigned a=(x*17+y*23)&255;if(x<3)a=x==0?0:x==1?1:255;Uint32 p=SDL_MapRGBA(s->format,Uint8(x*39+y),Uint8(x+y*71),Uint8(y-x*13),Uint8(a));std::memcpy(static_cast<char*>(s->pixels)+y*s->pitch+x*4,&p,4);}
  for(int variant:{0,1})for(unsigned mod:{0u,1u,127u,254u,255u}){SDL_Rect from{1,1,17,11},to{2,2,variant?7:23,variant?5:19};const SDL_Rect expected=to;SDL_FillRect(d,nullptr,0x416789abu);SDL_SetSurfaceAlphaMod(s,Uint8(mod));if(SDL_UpperBlitScaled(s,&from,d,&to)!=0){ok=false;continue;}
   if(to.x!=expected.x||to.y!=expected.y||to.w!=expected.w||to.h!=expected.h)ok=false;
   const int ix=(from.w<<16)/to.w,iy=(from.h<<16)/to.h;
   for(int y=0;y<to.h&&ok;++y)for(int x=0;x<to.w;++x){Uint32 source,got;const int sx=from.x+((ix/2+x*ix)>>16),sy=from.y+((iy/2+y*iy)>>16);std::memcpy(&source,static_cast<char*>(s->pixels)+sy*s->pitch+sx*4,4);std::memcpy(&got,static_cast<char*>(d->pixels)+(y+to.y)*d->pitch+(x+to.x)*4,4);if(got!=pixel(source,0x416789abu,Uint8(mod),d->format->Amask!=0)){ok=false;break;}}
  }
 }
 SDL_FreeSurface(s);SDL_FreeSurface(d);if(used<4)entries[used++]={sf,df,ok};return ok;
}
class Workspace {
 std::array<int,3840> columns_{};alignas(16) std::array<Uint32,3840> row_{};int planned_source_=0,planned_target_=0;
public:
 // ~30KiB persistent storage; no allocation per blit. Compatibility probing
 // allocates tiny private surfaces once per format pair. false never mutates
 // caller pixels or rectangles. Partial clips retain the original SDL path.
 bool blit(SDL_Surface* src,const SDL_Rect* source,SDL_Surface* dst,SDL_Rect* destination){
  if(!pixel_blend::formats(src,dst)||src==dst||!src->pixels||!dst->pixels||SDL_MUSTLOCK(src)||SDL_MUSTLOCK(dst)||src->locked||dst->locked||src->pitch<=0||dst->pitch<=0||src->pitch%4||dst->pitch%4||src->w>src->pitch/4||dst->w>dst->pitch/4)return false;
  if(src->w<1||src->h<1||src->w>8192||src->h>8192||dst->w<1||dst->h<1||dst->w>3840||dst->h>2160)return false;
  SDL_Rect from=source?*source:SDL_Rect{0,0,src->w,src->h},to=destination?*destination:SDL_Rect{0,0,dst->w,dst->h};
  if(from.x<0||from.y<0||from.w<1||from.h<1||int64_t(from.x)+from.w>src->w||int64_t(from.y)+from.h>src->h||to.x<0||to.y<0||to.w<1||to.h<1||int64_t(to.x)+to.w>dst->w||int64_t(to.y)+to.h>dst->h)return false;
  if(from.w==to.w&&from.h==to.h)return false; // Unscaled SDL has different rounding.
  const SDL_Rect clip=dst->clip_rect;if(to.x<clip.x||to.y<clip.y||int64_t(to.x)+to.w>int64_t(clip.x)+clip.w||int64_t(to.y)+to.h>int64_t(clip.y)+clip.h)return false;
  const auto sb=reinterpret_cast<std::uintptr_t>(src->pixels),db=reinterpret_cast<std::uintptr_t>(dst->pixels);const auto sn=size_t(src->pitch)*size_t(src->h),dn=size_t(dst->pitch)*size_t(dst->h);if(sb<=db?db-sb<sn:sb-db<dn)return false;
  SDL_BlendMode mode;Uint8 r,g,b,mod;Uint32 key;if(SDL_GetSurfaceBlendMode(src,&mode)||mode!=SDL_BLENDMODE_BLEND||SDL_GetColorKey(src,&key)==0||SDL_GetSurfaceColorMod(src,&r,&g,&b)||r!=255||g!=255||b!=255||SDL_GetSurfaceAlphaMod(src,&mod))return false;
  if(!compatible(src->format->format,dst->format->format))return false;
  if(planned_source_!=from.w||planned_target_!=to.w){const int64_t step=(int64_t(from.w)<<16)/to.w;for(int x=0;x<to.w;++x)columns_[x]=int((step/2+x*step)>>16);planned_source_=from.w;planned_target_=to.w;}
  const int64_t step=(int64_t(from.h)<<16)/to.h;int prior=-1;bool opaque=false;
  for(int y=0;y<to.h;++y){const int sy=from.y+int((step/2+y*step)>>16);if(sy!=prior){const char*p=static_cast<const char*>(src->pixels)+size_t(sy)*src->pitch+from.x*4;Uint32 all=0xffffffffu;for(int x=0;x<to.w;++x){std::memcpy(&row_[x],p+columns_[x]*4,4);all&=row_[x];}opaque=(all&0xff000000u)==0xff000000u;prior=sy;}
   (opaque?opaque_row:row)(row_.data(),static_cast<char*>(dst->pixels)+size_t(to.y+y)*dst->pitch+to.x*4,to.w,mod,dst->format->Amask!=0);
  }
  if(destination){*destination=to;}
  return true;
 }
};
}

