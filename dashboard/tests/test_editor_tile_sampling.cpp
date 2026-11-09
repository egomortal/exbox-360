// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/tile_sampling.hpp"
#include <cassert>
#include <array>
using namespace exbox::editor;
int main(){
 const std::array<uint8_t,16> p={255,0,0,255, 0,255,0,0, 0,0,255,128, 255,255,255,0};
 assert((area_tile(p,2,2,1)==std::vector<uint8_t>{169,0,85,95}));
 assert((area_tile(p,2,2,2)==std::vector<uint8_t>{255,0,0,255,0,0,0,0,0,0,255,128,0,0,0,0}));
 assert(area_tile(p,3,3,2).empty());assert(area_tile(p,0,2,1).empty());
 for(unsigned hint:{0u,64u,127u,128u,256u,0xffffffffu})assert(tile_size_ladder(hint)==(hint>=128?std::vector<uint32_t>{128,96,64}:std::vector<uint32_t>{64}));
 for(unsigned w=1;w<=20;++w)for(unsigned h=1;h<=20;++h)for(unsigned target=1;target<=20;++target){
  std::vector<uint8_t> input(w*h*4);for(unsigned i=0;i<w*h;++i){input[i*4]=19;input[i*4+1]=57;input[i*4+2]=203;input[i*4+3]=127;}
  const auto original=input;auto out=area_tile(input,w,h,target);assert(out.size()==target*target*4);assert(input==original);
  for(unsigned i=0;i<target*target;++i){assert(out[i*4]==19&&out[i*4+1]==57&&out[i*4+2]==203&&out[i*4+3]==127);}
 }
}
