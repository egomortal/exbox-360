#pragma once
#include "gpu2d.h"
#include <SDL.h>
#include <vector>
#include <cstring>
namespace video_q8 {
struct Frame{uint64_t generation=0;uint32_t width=0,height=0;std::vector<uint32_t>pixels;};
// Decoder/publication owner. Failed validation leaves the previous frame intact.
// Caller supplies a new generation for changed decoded bytes; presenter publication
// keys include stream owner/epoch plus this generation. No SDL pointer survives.
inline bool prepare(SDL_Surface*s,uint64_t generation,Frame&out){
 if(!generation||!s||!s->pixels||s->w<1||s->h<1||s->w>3840||s->h>2160||s->format->format!=SDL_PIXELFORMAT_ARGB8888||s->pitch<s->w*4||SDL_MUSTLOCK(s))return false;
 Uint8 r,g,b,a;Uint32 key;if(SDL_GetSurfaceColorMod(s,&r,&g,&b)||r!=255||g!=255||b!=255||SDL_GetSurfaceAlphaMod(s,&a)||a!=255||SDL_GetColorKey(s,&key)==0)return false;
 try{Frame f;f.generation=generation;f.width=s->w;f.height=s->h;f.pixels.resize(size_t(f.width)*f.height);
 for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++){uint32_t v;std::memcpy(&v,(const char*)s->pixels+size_t(y)*s->pitch+x*4,4);if((v>>24)!=255)return false;f.pixels[size_t(y)*f.width+x]=v;}
 out=std::move(f);return true;}catch(const std::bad_alloc&){return false;}
}
inline ExboxGpuCommand command(const Frame&f,ExboxGpuRect dst,ExboxGpuRect clip,float opacity=1){
 ExboxGpuCommand c{};c.kind=EXBOX_GPU_VIDEO_Q8;c.dst=dst;c.clip=clip;c.top=0xffffffff;c.opacity=255;c.blend=1;c.transform[8]=opacity;c.pixels=f.pixels.data();c.bytes=f.pixels.size()*4;c.width=f.width;c.height=f.height;c.pitch=f.width*4;return c;
}
}
