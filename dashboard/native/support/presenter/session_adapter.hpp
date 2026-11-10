// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "route.hpp"
#include "surface_upload.hpp"
#include <atomic>
namespace exbox::presenter {
struct Backend {int(*open)(int*,int*);void*(*upload)(size_t*,int*);int(*present)();int(*close)();};
// Startup and close belong to the control thread. Only submit runs on the
// existing presenter worker; control joins that worker before closing.
class Adapter {
 Backend backend_;bool owns_=false;int width_=0,height_=0;
 std::atomic<bool> failed_{false};
public:
 explicit Adapter(Backend backend):backend_(backend){}
 Route start(bool enabled){
  if(!enabled)return Route::sdl;
  const int result=backend_.open(&width_,&height_);
  owns_=result!=0;
  if(result>0 && width_>0 && height_>0)return Route::native;
  if(result>0)return close()?Route::sdl:Route::stop;
  return result==0?Route::sdl:Route::stop;
 }
 bool submit(SDL_Surface* frame){
  if(!owns_||failed_ ||!frame||frame->w!=width_||frame->h!=height_){failed_=true;return false;}
  size_t bytes=0;int pitch=0;void* destination=backend_.upload(&bytes,&pitch);
  if(!copy_bgra(frame,destination,bytes,pitch)||!backend_.present()){failed_=true;return false;}
  return true;
 }
 bool close(){if(!owns_)return true;if(!backend_.close())return false;owns_=false;return true;}
 bool failed()const{return failed_.load();}
 int width()const{return width_;}int height()const{return height_;}
};
}
