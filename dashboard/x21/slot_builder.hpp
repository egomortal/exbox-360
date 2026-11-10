// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "extractor.hpp"
// UI scene evaluation/resource publication only; no CPU framebuffer read/raster.
// Original Painted::draw uses draw_text=false; callers can explicitly request text.
inline bool append_custom_slot(Extractor&out,xui::Scene&scene,xui::GpuResources&resources,int density,SDL_Rect box,SDL_Rect clip,bool draw_text=false){
 if((density!=1&&density!=2)||box.x<0||box.y<0||box.w<=0||box.h<=0||box.x+box.w>1920*density||box.y+box.h>1080*density)return false;
 Extractor local;local.extract(scene,resources,xui::Matrix::scale(float(density),float(density)),420*density,320*density,draw_text,false);if(!local.unsupported.empty())return false;
 ExboxGpuCommand begin{};begin.kind=EXBOX_GPU_BEGIN_SLOT;begin.dst=begin.clip={0,0,420*density,320*density};begin.opacity=255;begin.transform[0]=float(box.x);begin.transform[1]=float(box.y);begin.transform[2]=float(box.w);begin.transform[3]=float(box.h);begin.transform[8]=1;begin.transform[11]=2;out.commands.push_back(begin);
 for(auto&c:local.commands){c.transform[11]=2;out.commands.push_back(c);}for(auto&v:local.owned)out.owned.push_back(std::move(v));for(auto&r:local.publications)out.publications.push_back(std::move(r));
 ExboxGpuCommand end{};end.kind=EXBOX_GPU_COMPOSITE_SLOT;end.dst={box.x,box.y,box.w,box.h};end.clip={clip.x,clip.y,clip.w,clip.h};end.width=420*density;end.height=320*density;end.opacity=255;end.transform[8]=1;out.commands.push_back(end);return true;
}
