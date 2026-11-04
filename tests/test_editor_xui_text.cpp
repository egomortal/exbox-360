// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/xui_text.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main(){
 std::array<uint8_t,512> bytes;bytes.fill(0xcc);
 assert(encode_heading("Tops",bytes)==4);assert(heading_text(std::span(bytes).first(130))=="Tops");
 assert(bytes[8]==0 && bytes[9]==0 && bytes[10]==0xcc);
 bytes[2]=1;assert(heading_text(std::span(bytes).first(130))=="T ps");
 bytes[0]=1;assert(heading_text(bytes).empty());bytes[0]=0;
 assert(heading_text(std::span(bytes).first(129)).empty());
 const std::string long_text(150,'x');assert(encode_heading(long_text,bytes)==126);
 assert(heading_text(bytes)==std::string(60,'x'));assert(bytes[252]==0 && bytes[253]==0);
 for(size_t size=0;size<16;++size){
  bytes.fill(0xcc);const auto n=encode_heading("abcdef",std::span(bytes).first(size));
  if(size<2){assert(n==0 && bytes[0]==0xcc);continue;}
  assert(n==std::min(size_t(6),size/2-1));assert(bytes[n*2]==0 && bytes[n*2+1]==0);
  assert(bytes[size]==0xcc);
 }
 bytes.fill(0xcc);assert(!encode_heading("",bytes));assert(bytes[0]==0 && bytes[1]==0);
}
