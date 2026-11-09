// SPDX-License-Identifier: BSD-3-Clause
#include "runtime_loading.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace av2::loading;
static void color(const ExternalPixels& p,unsigned w,unsigned h,unsigned r,unsigned g,unsigned b){
 assert(p.ready()&&p.width==w&&p.height==h&&p.rgba.size()==size_t(w)*h*4);
 for(size_t n=0;n<p.rgba.size();n+=4)assert(p.rgba[n]==r&&p.rgba[n+1]==g&&p.rgba[n+2]==b&&p.rgba[n+3]==255);
}
int main(int argc,char** argv){
 assert(argc==2||argc==4);std::string root=argv[1];
 const auto path=[&](const char* n){return root+"/"+n;};
 const auto test=[&](unsigned w,unsigned h,const char* override_name,const char* packaged_name){
  auto override_path=path(override_name),package_path=path(packaged_name);
  return load_startup_png(w,h,override_path.c_str(),package_path.c_str(),package_path.c_str());
 };
 color(test(1920,1080,"override-1080.png","package-1080.png"),1920,1080,51,61,71);
 color(test(3840,2160,"override-2160.png","package-2160.png"),3840,2160,51,61,71);
 assert(!load_external_png(1920,1080,path("missing.png").c_str()).ready());
 for(const char* bad:{"missing.png","corrupt.png","wrong-size.png","truncated.png","unsupported.png","link.png","directory"})
  color(test(1920,1080,bad,"package-1080.png"),1920,1080,17,31,59);
 color(test(3840,2160,"missing.png","package-2160.png"),3840,2160,17,31,59);
 for(const char* bad:{"missing.png","corrupt.png","wrong-size.png","truncated.png","unsupported.png","link.png","directory","package-2160.png"}){
  auto p=test(1920,1080,"missing.png",bad);assert(!p.ready()&&p.rgba.empty());
 }
 auto missing=path("missing.png"),p1080=path("package-1080.png"),p2160=path("package-2160.png");
 color(load_startup_png(1920,1080,missing.c_str(),p1080.c_str(),p2160.c_str()),1920,1080,17,31,59);
 color(load_startup_png(3840,2160,missing.c_str(),p1080.c_str(),p2160.c_str()),3840,2160,17,31,59);
 for(const auto w:{0u,1280u,4097u}){auto p=load_startup_png(w,720,missing.c_str(),p1080.c_str(),p2160.c_str());assert(!p.ready()&&p.rgba.empty());}
 assert(!load_startup_png(1920,1080,nullptr,nullptr,nullptr).ready());
 if(argc==4){auto a=load_startup_png(1920,1080,missing.c_str(),argv[2],argv[3]);auto b=load_startup_png(3840,2160,missing.c_str(),argv[2],argv[3]);assert(a.ready()&&b.ready());}
 std::puts("PASS startup override precedence, exact packaged resolution, invalid/missing/corrupt/unsafe fallback, raw-loader semantics");
}
