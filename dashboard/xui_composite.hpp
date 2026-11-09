#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xui {
// Numeric state recovered from the owner's 1888 XAM at 0x81934c18.
// 0 inherits the current context; groups do not imply isolated layers.
enum BlendMode {Inherit=0,Normal=1,Multiply=2,Minimum=3,Maximum=4,Add=5,Subtract=6};
struct BlendState {int operation,source,destination;};
inline constexpr BlendState blend_states[]={{0,6,7},{0,6,7},{0,0,4},{2,1,1},{3,1,1},{0,6,1},{1,6,1}};
inline uint32_t composite(uint32_t destination,uint32_t source,float opacity,float coverage,int mode) {
    coverage=std::clamp(coverage,0.f,1.f);
    if (coverage<=0) return destination;
    const int alpha=std::clamp(int(float(source>>24)*opacity+.5f),0,255);
    if ((mode<=Normal || mode==Add) && !alpha) return destination;
    const int cov=std::clamp(int(coverage*255+.5f),0,255);
    uint32_t result=0xff000000u;
    for (int shift:{0,8,16}) {
        const int d=int((destination>>shift)&255),s=int((source>>shift)&255);
        int value;
        switch (mode) {
        case Multiply:value=(s*d+127)/255;break;
        case Minimum:value=std::min(s,d);break;
        case Maximum:value=std::max(s,d);break;
        case Add:value=std::min(255,d+(s*alpha+127)/255);break;
        case Subtract:value=std::max(0,(s*alpha+127)/255-d);break;
        default:value=(s*alpha+d*(255-alpha)+127)/255;break;
        }
        result|=uint32_t((value*cov+d*(255-cov)+127)/255)<<shift;
    }
    return result;
}
}
