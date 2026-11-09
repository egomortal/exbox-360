// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/message_values.hpp"
#include <cassert>
using namespace exbox::editor;
int main(){
 assert(message_integer("d",0xffffffff)==u"-1");assert(message_integer("u",0xffffffff)==u"4294967295");
 assert(message_integer("08X",42)==u"0000002A");assert(message_integer("#x",42)==u"0x2a");
 assert(message_integer("+.4d",42)==u"+0042");assert(message_integer("hhd",255)==u"-1");
 assert(message_integer(".0u",0)==u"");assert(message_integer("100u",1)->size()==63);
 for(auto spec:{"n","p","s","f","*u",".*d","lld","lu","4097u",".4097d","%d","", "99999999999999d"})assert(!message_integer(spec,42));
 auto arg=[](uint32_t i){return i==0?42u:i==1?100u:i==2?0u:65u;};
 auto read=[](uint32_t v,bool narrow){assert(v==100);return narrow?u"narrow":u"wide";};
 assert(expand_message(u"%1!04u! %2 %2!hs! %3 %4!c!%n",arg,read)==u"0042 wide narrow (null) A\r\n");
 assert(!expand_message(u"%1!n!",arg,read));
 assert(expand_message(u"%2!100.1s!",arg,read)==u"wide");
 const std::u16string text=u"AU0001f600Z";
 for(uint32_t capacity=0;capacity<16;++capacity){
  std::array<uint8_t,48> bytes;bytes.fill(0xcc);auto result=write_message(text,capacity,std::span(bytes).subspan(8,32));
  if(!capacity){assert(!result);for(auto b:bytes)assert(b==0xcc);continue;}
  const size_t count=std::min(text.size(),size_t(capacity-1));assert(result==count);
  for(size_t at=0;at<count;++at){assert(bytes[8+2*at]==uint8_t(text[at]>>8));assert(bytes[9+2*at]==uint8_t(text[at]));}
  assert(bytes[8+2*count]==0 && bytes[9+2*count]==0);
  for(size_t at=0;at<bytes.size();++at)if(at<8||at>=10+2*count)assert(bytes[at]==0xcc);
 }
 std::array<uint8_t,3> small{1,2,3};assert(!write_message(u"x",2,small));assert((small==std::array<uint8_t,3>{1,2,3}));
}
