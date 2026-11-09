#include "../media_transport_layout.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
 using namespace media_transport;
 assert(!cell_for(1888,0,true));assert(!cell_for(17526,0,false));
 assert(!cell_for(9199,-1,false));assert(!cell_for(9199,8,false));
 const auto* nxe=cell_for(9199,0,true);assert(nxe);
 assert(std::abs(nxe->width*nxe->scale_x-43.55)<0.00001);
 auto video=project(*nxe,1920.0/720,1080.0/480,34,29);
 assert(video.x==461 && video.y==873 && video.w==116 && video.h==64);
 const auto* later=cell_for(17559,0,true);assert(later);
 assert(std::abs(later->width*later->scale_x-45.5)<0.00001);
 const auto* blades=cell_for(1888,0,false);assert(blades);
 auto picture=project(*blades,1.5,1.5,60,44.0094);
 assert(picture.x==470 && picture.y==889 && picture.w==87 && picture.h==70);
 assert(!blades->transform_verified); // Tiny original rotation is retained as unresolved.
 const auto* photo=cell_for(9199,0,false);assert(photo);
 assert(std::abs(photo->scale_x-0.9)<0.00001);
 picture=project(*photo,1.5,1.5,67.0094,44.0094);
 assert(picture.w==90 && picture.h==60);
 for(const auto& row:kLayouts) {
  for(int i=0;i<8;++i){auto p=project(row.picture[i],1.5,1.5,60,44);assert(p.w>0 && p.h>0);}
 }
 std::puts("PASS: original transport sizes/scales, absolute-edge conversion, revision and unresolved-transform boundaries");
}
