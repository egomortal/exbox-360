#pragma once
#include <cstdint>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
namespace xui {
inline bool supports_avx2() {
#ifdef __PROSPERO__
    return true;
#else
    return __builtin_cpu_supports("avx2");
#endif
}
__attribute__((target("avx2"))) inline __m256i divide255(__m256i x) {
    const auto t=_mm256_add_epi32(x,_mm256_set1_epi32(128));
    return _mm256_srli_epi32(_mm256_add_epi32(t,_mm256_srli_epi32(t,8)),8);
}
// x / 255 rounded, for 16-bit lanes holding at most 255 * 255.
__attribute__((target("avx2"))) inline __m256i divide255_16(__m256i x) {
    const auto t=_mm256_add_epi16(x,_mm256_set1_epi16(128));
    return _mm256_srli_epi16(_mm256_add_epi16(t,_mm256_srli_epi16(t,8)),8);
}
// One pair of colour channels (16-bit lanes) through a blend mode, then coverage.
__attribute__((target("avx2"))) inline __m256i blend16(__m256i s,__m256i d,__m256i a16,__m256i c16,int mode,bool full) {
    const auto byte16=_mm256_set1_epi16(255);
    __m256i channel;
    switch(mode) {
    case 2:channel=divide255_16(_mm256_mullo_epi16(s,d));break;
    case 3:channel=_mm256_min_epu16(s,d);break;
    case 4:channel=_mm256_max_epu16(s,d);break;
    case 5:channel=_mm256_min_epu16(byte16,_mm256_add_epi16(d,divide255_16(_mm256_mullo_epi16(s,a16))));break;
    case 6:channel=_mm256_subs_epu16(divide255_16(_mm256_mullo_epi16(s,a16)),d);break;
    default:channel=divide255_16(_mm256_add_epi16(_mm256_mullo_epi16(s,a16),_mm256_mullo_epi16(d,_mm256_sub_epi16(byte16,a16))));break;
    }
    if (!full) channel=divide255_16(_mm256_add_epi16(_mm256_mullo_epi16(channel,c16),_mm256_mullo_epi16(d,_mm256_sub_epi16(byte16,c16))));
    return _mm256_and_si256(channel,_mm256_set1_epi32(0x00ff00ff));
}
__attribute__((target("avx2"))) inline int raster_avx2(uint32_t *destination,const float *coverage,int count,int x0,float gy,float hy,
        float gx,float hx,float gc,float hc,const uint32_t *table,int fill_type,uint32_t solid,float opacity,int mode) {
    const auto zero=_mm256_setzero_si256(),byte=_mm256_set1_epi32(255),alpha_bits=_mm256_set1_epi32(int(0xff000000u));
    const auto half=_mm256_set1_ps(.5f),range=_mm256_set1_ps(4096),opacity_v=_mm256_set1_ps(opacity);
    const auto x_offsets=_mm256_setr_ps(.5f,1.5f,2.5f,3.5f,4.5f,5.5f,6.5f,7.5f);
    int at=0;
    for (;at+8<=count;at+=8) {
        const auto cov_f=_mm256_loadu_ps(coverage+at);
        if (!_mm256_movemask_ps(_mm256_cmp_ps(cov_f,_mm256_setzero_ps(),_CMP_GT_OQ))) continue;
        const auto cov=_mm256_min_epi32(byte,_mm256_max_epi32(zero,_mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(cov_f,_mm256_set1_ps(255)),half))));
        const bool full=_mm256_movemask_epi8(_mm256_cmpeq_epi32(cov,byte))==-1;
        __m256i source;
        if (table) {
            const auto x=_mm256_add_ps(_mm256_set1_ps(float(x0+at)),x_offsets);
            const auto gu=_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(gx),x),_mm256_set1_ps(gy)),_mm256_set1_ps(gc));
            __m256 phase;
            if (fill_type==3) {
                const auto gv=_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(hx),x),_mm256_set1_ps(hy)),_mm256_set1_ps(hc));
                phase=_mm256_mul_ps(_mm256_set1_ps(2),_mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(gu,gu),_mm256_mul_ps(gv,gv))));
            } else phase=_mm256_add_ps(gu,half);
            const auto index=_mm256_min_epi32(_mm256_set1_epi32(4096),_mm256_max_epi32(zero,_mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(phase,range),half))));
            source=_mm256_i32gather_epi32(reinterpret_cast<const int *>(table),index,4);
        } else source=_mm256_set1_epi32(int(solid));
        const auto dest=_mm256_loadu_si256(reinterpret_cast<const __m256i *>(destination+at));
        __m256i value=alpha_bits;
        const auto alpha=_mm256_min_epi32(byte,_mm256_max_epi32(zero,_mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srli_epi32(source,24)),opacity_v),half))));
        const bool opaque=_mm256_movemask_epi8(_mm256_cmpeq_epi32(alpha,byte))==-1;
        if (full && (mode==1 || mode==0) && opaque) value=_mm256_or_si256(source,alpha_bits);
        else if (full && mode==3) value=_mm256_or_si256(_mm256_min_epu8(source,dest),alpha_bits);
        else if (full && mode==4) value=_mm256_or_si256(_mm256_max_epu8(source,dest),alpha_bits);
        else if (full && mode==5 && opaque) value=_mm256_or_si256(_mm256_adds_epu8(source,dest),alpha_bits);
        else if (full && mode==6 && opaque) value=_mm256_or_si256(_mm256_subs_epu8(source,dest),alpha_bits);
        else {
            // All three colour channels at once in 16-bit lanes: R/B in one register,
            // G (and an ignored alpha lane) in another. Same integer math as the
            // scalar composite(), so results are bit-identical.
            const auto lo8=_mm256_set1_epi32(0x00ff00ff);
            const auto a16=_mm256_or_si256(alpha,_mm256_slli_epi32(alpha,16));
            const auto c16=_mm256_or_si256(cov,_mm256_slli_epi32(cov,16));
            const auto rb=blend16(_mm256_and_si256(source,lo8),_mm256_and_si256(dest,lo8),a16,c16,mode,full);
            const auto g=blend16(_mm256_and_si256(_mm256_srli_epi32(source,8),lo8),_mm256_and_si256(_mm256_srli_epi32(dest,8),lo8),a16,c16,mode,full);
            value=_mm256_or_si256(alpha_bits,_mm256_or_si256(rb,_mm256_slli_epi32(g,8)));
        }
        // Geometry with zero coverage must preserve all destination bytes.
        const auto mask=_mm256_cmpeq_epi32(cov,zero);value=_mm256_blendv_epi8(value,dest,mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(destination+at),value);
    }
    return at;
}
}
#else
namespace xui {inline bool supports_avx2(){return false;}inline int raster_avx2(uint32_t *,const float *,int,int,float,float,float,float,float,float,const uint32_t *,int,uint32_t,float,int){return 0;}}
#endif
