#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#ifdef __SSE2__
#include <emmintrin.h>
#endif

namespace image_quality {
struct Tap { int first=0;std::vector<float> weights; };
struct Axis { int source=0,target=0;std::vector<Tap> taps; };
inline Axis make_axis(int source,int target) {
    Axis axis{source,target,{}};
    const double scale=double(source)/target,filter=std::max(1.0,scale);
    constexpr double pi=3.14159265358979323846;
    for (int x=0;x<target;x++) {
        const double center=(x+0.5)*scale-0.5;
        const int first=std::max(0,int(std::ceil(center-3*filter)));
        const int last=std::min(source-1,int(std::floor(center+3*filter)));
        Tap tap;tap.first=first;double total=0;
        for (int i=first;i<=last;i++) {
            const double d=std::abs((i-center)/filter);
            const double weight=d<1e-10?1:d>=3?0:std::sin(pi*d)*std::sin(pi*d/3)/(pi*pi*d*d/3);
            tap.weights.push_back(float(weight));total+=weight;
        }
        for (float &weight:tap.weights) weight=float(weight/total);
        axis.taps.push_back(std::move(tap));
    }
    return axis;
}
inline const Axis &axis(int source,int target) {
    static std::vector<Axis> cache;
    for (const Axis &item:cache) if (item.source==source && item.target==target) return item;
    if (cache.size()==32) cache.erase(cache.begin());
    cache.push_back(make_axis(source,target));return cache.back();
}
inline uint32_t sample(const uint32_t *source,int stride,const Tap &tap) {
#ifdef __SSE2__
    __m128 sum=_mm_setzero_ps();
    const __m128i zero=_mm_setzero_si128();
    for (size_t i=0;i<tap.weights.size();i++) {
        const __m128i packed=_mm_cvtsi32_si128(int(source[(tap.first+int(i))*stride]));
        const __m128 value=_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(packed,zero),zero));
        sum=_mm_add_ps(sum,_mm_mul_ps(value,_mm_set1_ps(tap.weights[i])));
    }
    __m128i value=_mm_cvtps_epi32(sum);
    value=_mm_packs_epi32(value,value);value=_mm_packus_epi16(value,value);
    return uint32_t(_mm_cvtsi128_si32(value));
#else
    float sum[4]{};
    for (size_t i=0;i<tap.weights.size();i++) {
        const uint32_t p=source[(tap.first+int(i))*stride];
        for (int c=0;c<4;c++) sum[c]+=float((p>>(c*8))&255)*tap.weights[i];
    }
    uint32_t result=0;
    for (int c=0;c<4;c++) result|=uint32_t(std::clamp(int(std::lround(sum[c])),0,255))<<(c*8);
    return result;
#endif
}
inline void resize_argb(const void *source,int sw,int sh,int source_pitch,
                        void *destination,int dw,int dh,int destination_pitch) {
    // Copies keep the axis tables valid if cache insertion moves its storage.
    const Axis horizontal=axis(sw,dw),vertical=axis(sh,dh);
#ifdef __SSE2__
    std::vector<float> intermediate(size_t(dw)*sh*4);
    const __m128i zero=_mm_setzero_si128();
    for (int y=0;y<sh;y++) {
        const auto *row=reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(source)+y*source_pitch);
        for (int x=0;x<dw;x++) {
            const Tap &tap=horizontal.taps[size_t(x)];
            __m128 sum=_mm_setzero_ps();
            for (size_t i=0;i<tap.weights.size();i++) {
                const __m128i packed=_mm_cvtsi32_si128(int(row[tap.first+int(i)]));
                const __m128 value=_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(packed,zero),zero));
                sum=_mm_add_ps(sum,_mm_mul_ps(value,_mm_set1_ps(tap.weights[i])));
            }
            _mm_storeu_ps(intermediate.data()+(size_t(y)*dw+x)*4,sum);
        }
    }
    for (int y=0;y<dh;y++) {
        auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(destination)+y*destination_pitch);
        const Tap &tap=vertical.taps[size_t(y)];
        for (int x=0;x<dw;x++) {
            __m128 sum=_mm_setzero_ps();
            for (size_t i=0;i<tap.weights.size();i++) {
                const __m128 value=_mm_loadu_ps(intermediate.data()+(size_t(tap.first+int(i))*dw+x)*4);
                sum=_mm_add_ps(sum,_mm_mul_ps(value,_mm_set1_ps(tap.weights[i])));
            }
            __m128i value=_mm_cvtps_epi32(sum);
            value=_mm_packs_epi32(value,value);value=_mm_packus_epi16(value,value);
            row[x]=uint32_t(_mm_cvtsi128_si32(value));
        }
    }
#else
    std::vector<uint32_t> intermediate(size_t(dw)*sh);
    for (int y=0;y<sh;y++) {
        const auto *row=reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(source)+y*source_pitch);
        for (int x=0;x<dw;x++) intermediate[size_t(y)*dw+x]=sample(row,1,horizontal.taps[size_t(x)]);
    }
    for (int y=0;y<dh;y++) {
        auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(destination)+y*destination_pitch);
        for (int x=0;x<dw;x++) row[x]=sample(intermediate.data()+x,dw,vertical.taps[size_t(y)]);
    }
#endif
}
}
