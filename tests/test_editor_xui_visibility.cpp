// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/xui_visibility.hpp"
#include <cassert>
#include <map>
using namespace exbox::editor;
int main(){
 std::map<uint32_t,uint32_t> m{{112,0x70005},{108,0x70005},
 {0x9457EF18,6},{0x9457EAF8,1000},{1040,7},{1044,2000},
 {2004,0},{2032,3000},{3036,0x3f800000},{3180,0xff}};
 auto read=[&](uint32_t a)->std::optional<uint32_t>{
  const auto it=m.find(a);return it==m.end()?std::nullopt:std::optional(it->second);};
 bool writable=true;unsigned writes=0;
 auto write=[&](uint32_t a,uint32_t av,uint32_t b,uint32_t bv){
  if(!writable)return false;m[a]=av;m[b]=bv;++writes;return true;};
 assert(hide_identifying(100,read,write)==2);
 assert(m[3036]==0 && m[3180]==0xfe && writes==2);
 assert(!hide_identifying(0,read,write));
 assert(!hide_identifying(UINT32_MAX,read,write));
 m[3036]=123;m[3180]=9;writable=false;
 assert(!hide_identifying(100,read,write));assert(m[3036]==123 && m[3180]==9);
 writable=true;m.erase(3180);assert(!hide_identifying(100,read,write));
 m[3180]=8;m[1040]=6;assert(!hide_identifying(100,read,write));
}
