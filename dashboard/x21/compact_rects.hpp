#pragma once
#include "gpu2d.h"
#include <vector>
#include <cstring>
namespace x21compact {
inline bool simple(const ExboxGpuCommand&c){
 if(c.kind!=EXBOX_GPU_RECT||c.blend>1||c.fill_type||c.opacity!=255||c.pixels||c.bytes||c.width||c.height||c.pitch)return false;
 if(c.dst.w<=0||c.dst.h<=0||c.dst.w>16384||c.dst.h>16384||c.dst.x< -16384||c.dst.y< -16384||c.dst.x>16384||c.dst.y>16384)return false;
 for(unsigned i=0;i<12;i++)if(c.transform[i]!=(i==8?1.f:0.f))return false;
 return true;
}
inline bool merge(ExboxGpuCommand&a,const ExboxGpuCommand&b){
 if(!simple(a)||!simple(b)||a.blend!=b.blend||a.top!=b.top||a.bottom!=b.bottom||std::memcmp(&a.clip,&b.clip,sizeof a.clip))return false;
 if(a.dst.x==b.dst.x&&a.dst.w==b.dst.w&&int64_t(a.dst.y)+a.dst.h==b.dst.y&&int64_t(a.dst.h)+b.dst.h<=16384){a.dst.h+=b.dst.h;return true;}
 if(a.dst.y==b.dst.y&&a.dst.h==b.dst.h&&int64_t(a.dst.x)+a.dst.w==b.dst.x&&int64_t(a.dst.w)+b.dst.w<=16384){a.dst.w+=b.dst.w;return true;}
 return false;
}
inline size_t rectangles(std::vector<ExboxGpuCommand>&commands){
 size_t written=0;
 for(size_t i=0;i<commands.size();++i){
  if(written&&merge(commands[written-1],commands[i]))continue;
  if(written>=2&&simple(commands[written-1])){
   auto combined=commands[written-2];
   if(merge(combined,commands[i])){
    const auto&a=combined.dst;const auto&b=commands[written-1].dst;
    const bool disjoint=int64_t(a.x)+a.w<=b.x||int64_t(b.x)+b.w<=a.x||int64_t(a.y)+a.h<=b.y||int64_t(b.y)+b.h<=a.y;
    if(disjoint){commands[written-2]=combined;continue;}
   }
  }
  if(written!=i)commands[written]=commands[i];
  ++written;
 }
 size_t removed=commands.size()-written;commands.resize(written);return removed;
}
}
