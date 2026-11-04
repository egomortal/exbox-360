// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/keyboard_text.hpp"
#include <algorithm>
#include <array>
#include <cassert>
using namespace exbox::editor;
int main(){
 const std::array<uint8_t,12> original{0,65,0x03,0xa9,0xd8,0x3d,0xde,0x00,0,0,0,90};
 auto snapshot=keyboard_default(original);assert(snapshot.size()==8);
 for(uint32_t cap=0;cap<=7;++cap){
  std::array<uint8_t,16> out;out.fill(0xcc);
  assert(keyboard_copy(snapshot,out,cap)==(cap>0));
  if(!cap){assert(out[0]==0xcc);continue;}
  const auto bytes=std::min(cap-1,4U)*2;
  assert(std::equal(snapshot.begin(),snapshot.begin()+bytes,out.begin()));
  assert(out[bytes]==0 && out[bytes+1]==0 && out[bytes+2]==0xcc);
 }
 std::array<uint8_t,2> short_out{9,9};assert(!keyboard_copy(snapshot,short_out,2));assert(short_out[0]==9);
 assert(!keyboard_copy(snapshot,short_out,UINT32_MAX));
 assert(keyboard_default({}).empty());assert(keyboard_copy({},short_out,1));assert(short_out[0]==0 && short_out[1]==0);
 auto aliased=original;const auto saved=keyboard_default(aliased);aliased.fill(0xff);
 assert(keyboard_copy(saved,aliased,6));assert(aliased[0]==0 && aliased[1]==65);
}
