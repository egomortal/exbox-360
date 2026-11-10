#pragma once
#include "extractor.hpp"
#include "source_publication.hpp"
#include "filtered_packet.hpp"
// CPU layout/resource references only. No raster or scene surface allocation.
struct TabSink {
 std::vector<std::shared_ptr<const x21source::Publication>> publications;
 std::vector<resize_adapter::Key> filtered_requested;
 std::vector<filtered_art::Request> filtered_requests;
 Extractor packet;int width,height,density;SDL_Rect clip;uint32_t target=0;bool sdl_layer=false;
 struct Counters{size_t commands=0,source_bytes=0,clear_groups=0,composite_groups=0;size_t cpu_pixel_raster_calls=0;} counters;
 TabSink(int w,int h):width(w),height(h),density(w/1920),clip{0,0,w,h}{if(w!=1920&&w!=3840)throw std::runtime_error("unsupported density");}
 ExboxGpuCommand base(SDL_Rect dst){auto c=packet.base(dst,clip,1,1);c.transform[11]=float(target);return c;}
 void push(ExboxGpuCommand c){packet.commands.push_back(c);counters.commands++;counters.source_bytes+=c.bytes;}
 void clear_output(uint32_t color){auto c=base({0,0,width,height});c.kind=EXBOX_GPU_RECT;c.top=0xff000000u|color;push(c);}
 void begin_tab(){target=1;clip={0,0,width,height};auto c=base({0,0,width,height});c.kind=EXBOX_GPU_CLEAR_LAYER;c.top=0;push(c);counters.clear_groups++;}
 void rect(int x,int y,int w,int h,uint32_t color){if(w<=0||h<=0)return;auto c=base({x*density,y*density,w*density,h*density});c.kind=EXBOX_GPU_RECT;c.top=0xff000000u|color;push(c);}
 void gradient(int x,int y,int w,int h,uint32_t top,uint32_t bottom){if(w<=0||h<=0)return;auto c=base({x*density,y*density,w*density,h*density});c.kind=EXBOX_GPU_GRADIENT;c.top=top;c.bottom=bottom;c.transform[7]=density==1?3:1;push(c);}
 void rounded(int x,int y,int w,int h,int radius,uint32_t top,uint32_t bottom){if(w<=0||h<=0)return;auto c=base({x*density,y*density,w*density,h*density});c.kind=EXBOX_GPU_ROUNDED;c.top=top;c.bottom=bottom;c.transform[6]=float(std::max(0,radius)*density);push(c);}
 void image(SDL_Surface*image,const xui::Matrix&physical,float opacity=1,uint32_t tint=0xffffffffu){if(!image)return;size_t first=packet.commands.size();packet.texture(image,physical,clip,opacity,1,tint,x21source::surface_key(image));for(size_t i=first;i<packet.commands.size();i++){packet.commands[i].transform[11]=float(target);counters.commands++;counters.source_bytes+=packet.commands[i].bytes;}}
 bool blit(SDL_Surface* source,const SDL_Rect* source_rect,const SDL_Rect& destination,bool nearest=false){
  if(!source||destination.w<=0||destination.h<=0)return true;
  SDL_BlendMode blend=SDL_BLENDMODE_NONE;SDL_GetSurfaceBlendMode(source,&blend);
  if((source->format->format!=SDL_PIXELFORMAT_ARGB8888&&source->format->format!=SDL_PIXELFORMAT_ABGR8888&&source->format->format!=SDL_PIXELFORMAT_RGB888&&source->format->format!=SDL_PIXELFORMAT_BGR888)||(blend!=SDL_BLENDMODE_BLEND&&blend!=SDL_BLENDMODE_NONE)){packet.unsupported.push_back("native_blit_format_or_mode:"+std::to_string(source->format->format)+":"+std::to_string(int(blend)));return false;}
  SDL_Rect src=source_rect?*source_rect:SDL_Rect{0,0,source->w,source->h};
  if(src.x<0||src.y<0||src.w<=0||src.h<=0||src.x+src.w>source->w||src.y+src.h>source->h){packet.unsupported.push_back("native_blit_source_rect");return false;}
  uint8_t r,g,b,a;SDL_GetSurfaceColorMod(source,&r,&g,&b);SDL_GetSurfaceAlphaMod(source,&a);
  float sx=float(destination.w)/src.w,sy=float(destination.h)/src.h;
  auto matrix=xui::Matrix::translate(float(destination.x)-src.x*sx,float(destination.y)-src.y*sy)*xui::Matrix::scale(sx,sy);
  SDL_Rect saved=clip,limited{};if(!SDL_IntersectRect(&clip,&destination,&limited))return true;clip=limited;
  size_t first=packet.commands.size();image(source,matrix,(blend==SDL_BLENDMODE_NONE&&a==0)?1.f:float(a)/255,(uint32_t(255)<<24)|(uint32_t(r)<<16)|(uint32_t(g)<<8)|b);for(size_t i=first;i<packet.commands.size();i++){if(blend==SDL_BLENDMODE_NONE){packet.commands[i].blend=7;packet.commands[i].transform[8]=float(a)/255;}packet.commands[i].transform[9]=float(source->format->format==SDL_PIXELFORMAT_ABGR8888?1:source->format->format==SDL_PIXELFORMAT_RGB888?2:source->format->format==SDL_PIXELFORMAT_BGR888?3:0);}if(nearest)for(size_t i=first;i<packet.commands.size();i++){auto&c=packet.commands[i];c.transform[10]=1;c.transform[0]=float((int64_t(src.w)<<16)/destination.w);c.transform[4]=float((int64_t(src.h)<<16)/destination.h);c.transform[2]=float(destination.x);c.transform[5]=float(destination.y);c.transform[6]=float(src.x);c.transform[7]=float(src.y);}if((sdl_layer||target==0)&&blend==SDL_BLENDMODE_BLEND&&r==255&&g==255&&b==255)for(size_t i=first;i<packet.commands.size();i++){auto&c=packet.commands[i];bool scaled=src.w!=destination.w||src.h!=destination.h;c.blend=scaled?10:9;c.fill_type=0x53444cu;if(scaled)c.bottom=(uint32_t(src.w)<<16)|uint32_t(src.h);}clip=saved;return true;
 }
 void end_tab(const xui::Matrix&physical,float opacity){target=0;xui::GpuResources::Inverse inv(physical);if(!inv.valid)return;auto c=base(xui::GpuResources::bounds(physical,float(width),float(height)));c.kind=EXBOX_GPU_GROUP;c.top=0xffffffffu;c.transform[0]=inv.a;c.transform[1]=inv.b;c.transform[2]=inv.x;c.transform[3]=inv.c;c.transform[4]=inv.d;c.transform[5]=inv.y;c.transform[8]=opacity;push(c);counters.composite_groups++;}
};
