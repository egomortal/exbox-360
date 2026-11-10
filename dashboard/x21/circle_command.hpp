// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpu2d.h"
inline bool circle_command(ExboxGpuCommand&out,bool disc,int cx,int cy,int radius,int thickness,unsigned rgb,ExboxGpuRect clip,int density){
 if((density!=1&&density!=2)||radius<0||radius>4096/density||thickness<0||thickness>8192/density||cx<-8192/density||cx>8192/density||cy<-8192/density||cy>8192/density)return false;
 cx*=density;cy*=density;radius*=density;thickness*=density;int extra=disc?1:0;
 ExboxGpuCommand c{};c.kind=disc?EXBOX_GPU_DISC:EXBOX_GPU_RING;c.dst={cx-radius-extra,cy-radius,2*radius+1+2*extra,2*radius+1};c.clip=clip;c.top=0xff000000u|(rgb&0xffffffu);c.opacity=255;c.transform[0]=float(cx);c.transform[1]=float(cy);c.transform[2]=float(radius);c.transform[3]=float(thickness);c.transform[8]=1;out=c;return true;
}
