#pragma once
#include "gpu2d.h"
#include "resize_adapter.hpp"
namespace direct_lanczos {
inline bool packet(const resize_adapter::Prepared&p,std::vector<uint32_t>&out){
 if(!p.sw||p.sw>16||!p.sh||p.sh>2160||!p.dw||p.dw>3840||!p.dh||p.dh>2160||p.pixels.size()!=size_t(p.sw)*p.sh)return false;
 auto taps=p.taps.empty()?resize_adapter::table(p.sw,p.sh,p.dw,p.dh):p.taps;
 if((8+taps.size()+p.pixels.size())*4>2u*1024*1024)return false;
 std::vector<uint32_t> words{p.sw,p.sh,p.dw,p.dh,p.alpha?1u:0u,8u,uint32_t(8+taps.size()),uint32_t(8+taps.size()+p.pixels.size())};
 words.insert(words.end(),taps.begin(),taps.end());words.insert(words.end(),p.pixels.begin(),p.pixels.end());out=std::move(words);return true;
}
inline ExboxGpuCommand command(const std::vector<uint32_t>&p,ExboxGpuRect dst,ExboxGpuRect clip,uint32_t mod=0xffffffff,float alpha=1){
 ExboxGpuCommand c{};c.kind=EXBOX_GPU_DIRECT_LANCZOS;c.dst=dst;c.clip=clip;c.top=mod;c.opacity=255;c.blend=1;c.transform[8]=alpha;c.pixels=p.data();c.bytes=p.size()*4;if(p.size()>=8){c.width=p[0];c.height=p[1];c.pitch=p[0]*4;}return c;
}
}
