// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpu2d.h"
#include <vector>
// Original CPU Scene evaluation/resource preparation supplies local pixel commands.
// Caller retains resource payloads until its normal owned-packet snapshot completes.
// Serial group scratch is consumed; do not call inside another group/custom slot.
inline bool exbox_guide_commands(std::vector<ExboxGpuCommand>&out,const std::vector<ExboxGpuCommand>&scene,int W,int H,ExboxGpuRect area){
 if(W<1||H<1||area.w<1||area.h<1||area.w>W||area.h>H||uint64_t(area.w)*area.h>3888000u)return false;
 for(const auto&c:scene)if(c.kind>EXBOX_GPU_NINE||c.transform[11]!=0)return false;
 const size_t old=out.size();try{
 for(int pass=0;pass<2;pass++){
  ExboxGpuCommand clear{};clear.kind=EXBOX_GPU_CLEAR_LAYER;clear.dst=clear.clip={0,0,W,H};clear.top=pass?0xffffffffu:0xff000000u;clear.opacity=255;clear.transform[8]=clear.transform[11]=1;out.push_back(clear);
  // Existing CPU composite defaults observed XUI mode9 to source-over.
  for(auto c:scene){if(c.blend==9)c.blend=1;c.transform[11]=1;out.push_back(c);}
  if(!pass){ExboxGpuCommand c{};c.kind=EXBOX_GPU_GUIDE_SAVE_BLACK;c.dst=c.clip={0,0,area.w,area.h};c.width=area.w;c.height=area.h;c.pitch=area.w*4;c.opacity=255;c.transform[8]=1;out.push_back(c);}
 }
 ExboxGpuCommand c{};c.kind=EXBOX_GPU_GUIDE_RECOVER;c.dst=area;c.clip={0,0,W,H};c.width=area.w;c.height=area.h;c.pitch=area.w*4;c.opacity=255;c.transform[8]=1;out.push_back(c);return true;
 }catch(...){out.resize(old);return false;}
}
