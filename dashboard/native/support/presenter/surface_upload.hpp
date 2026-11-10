// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <cstddef>
#include <climits>
namespace exbox::presenter {
// Reuse SDL's existing channel conversion. No scaling, blending, transfer
// function change or HDR reinterpretation is performed by the upload boundary.
inline bool copy_bgra(SDL_Surface* source,void* destination,size_t bytes,int pitch){
 if(!source || !source->format || !source->pixels || !destination || source->w<=0 || source->h<=0 || source->w>INT_MAX/4)return false;
 if(pitch<source->w*4 || size_t(source->h)>bytes/size_t(pitch))return false;
 if(destination==source->pixels)return false;
 const bool locked=SDL_MUSTLOCK(source);
 if(locked && SDL_LockSurface(source)!=0)return false;
 const int rc=SDL_ConvertPixels(source->w,source->h,source->format->format,source->pixels,source->pitch,SDL_PIXELFORMAT_BGRA32,destination,pitch);
 if(locked)SDL_UnlockSurface(source);
 return rc==0;
}
}
