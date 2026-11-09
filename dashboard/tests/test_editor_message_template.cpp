// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/message_template.hpp"
#include <cassert>
using namespace exbox::editor;
std::u16string literals(std::u16string_view input) {
 auto parts=parse_message_template(input);std::u16string result;
 for(auto& p:parts){assert(!p.insertion);result+=p.text;}return result;
}
int main(){
 assert(literals(u"").empty());assert(literals(u"hello")==u"hello");
 assert(literals(u"%% %! %n%r%t")==u"% ! \r\n\r\t");
 assert(literals(u"before%0after")==u"before");assert(literals(u"tail%")==u"tail");
 assert(literals(u"%x% %N")==u"%x% %N");
 assert(literals(u"\U0001f600 \u00e9") == u"\U0001f600 \u00e9");
 auto p=parse_message_template(u"a%1b%99!08X!c%100");
 assert(p.size()==7 && p[0].text==u"a" && p[1].insertion && p[1].argument==0 && p[1].text==u"s");
 assert(p[3].argument==98 && p[3].text==u"08X" && p[5].argument==9 && p[6].text==u"0");
 p=parse_message_template(u"%2!!%3!unfinished");
 assert(p.size()==3 && p[0].text.empty() && p[1].text==u"s" && p[2].text==u"!unfinished");
 p=parse_message_template(u"%1!hs!%2!ls!%3!c!");
 assert(p.size()==3 && p[0].text==u"hs" && p[1].text==u"ls" && p[2].text==u"c");
 // Deterministic malformed/UTF16 stress: bounded token count and argument range.
 uint32_t state=0x12345678;
 for(unsigned run=0;run<10000;++run){
  std::u16string input;const size_t count=run%257;
  for(size_t i=0;i<count;++i){state=state*1664525+1013904223;input.push_back((i%3)==0?u'%':char16_t(state>>16));}
  auto tokens=parse_message_template(input);assert(tokens.size()<=input.size()+1);
  for(auto& part:tokens)if(part.insertion)assert(part.argument<99);
 }
}
