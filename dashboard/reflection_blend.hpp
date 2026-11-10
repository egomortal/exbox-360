// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <initializer_list>
#if defined(__SSE2__)
#include <immintrin.h>
#endif
namespace reflection_blend {
inline void row(const uint32_t* src,uint32_t* dst,const float* coverage,int count,float opacity,uint32_t alpha){
 int x=0;
#if defined(__SSE2__)
 const __m128 one=_mm_set1_ps(1.f),zero=_mm_setzero_ps(),op=_mm_set1_ps(opacity);
 const __m128i byte=_mm_set1_epi32(255),a=_mm_set1_epi32(int(alpha));
 for(;x+4<=count;x+=4){const __m128 c=_mm_loadu_ps(coverage+x);const __m128i active=_mm_castps_si128(_mm_cmpgt_ps(c,zero));if(!_mm_movemask_epi8(active))continue;
  const __m128 k=_mm_mul_ps(c,op),inv=_mm_sub_ps(one,k);const __m128i s=_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+x)),d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(dst+x));
  auto channel=[&](__m128i sc,__m128i dc){
   const __m128 source=_mm_cvtepi32_ps(_mm_and_si128(sc,byte)),destination=_mm_cvtepi32_ps(_mm_and_si128(dc,byte));
#if defined(__clang__) && defined(__FMA__)
   // SDK Clang21 znver2 with -ffp-contract=on contracts the scalar reference's
   // destination product. Native builds must retain that FP contract setting.
   return _mm_cvttps_epi32(_mm_fmadd_ps(destination,inv,_mm_mul_ps(source,k)));
#else
   return _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(destination,inv),_mm_mul_ps(source,k)));
#endif
  };
  __m128i out=_mm_or_si128(a,channel(s,d));out=_mm_or_si128(out,_mm_slli_epi32(channel(_mm_srli_epi32(s,8),_mm_srli_epi32(d,8)),8));out=_mm_or_si128(out,_mm_slli_epi32(channel(_mm_srli_epi32(s,16),_mm_srli_epi32(d,16)),16));
  out=_mm_or_si128(_mm_and_si128(active,out),_mm_andnot_si128(active,d));_mm_storeu_si128(reinterpret_cast<__m128i*>(dst+x),out);
 }
#endif
 for(;x<count;++x){if(coverage[x]<=0)continue;const float k=opacity*coverage[x];uint32_t out=alpha;for(int sh:{0,8,16})out|=uint32_t(uint8_t(((dst[x]>>sh)&255)*(1-k)+((src[x]>>sh)&255)*k))<<sh;dst[x]=out;}
}
}
