// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/search_state.hpp"
#include <array>
#include <cassert>
#include <thread>
using exbox::editor::SearchState;
int main(){
 SearchState s;std::array<char,128> out;out.fill('!');
 assert(s.status(out.data(),128)==0 && out[0]=='!');
 s.overlay(true,"hat",nullptr,0);
 auto text=[&](){int n=s.status(out.data(),128);assert(n==int(std::string(out.data()).size()));return std::string(out.data());};
 assert(text()=="Search: hat_");
 const char* rows[]={"12 matches",nullptr};s.overlay(true,"hat",rows,2);
 assert(text()=="Search: hat_  (12 matches)");
 s.overlay(false,"hat",rows,1);s.applied(true);
 assert(text()=="Filter: hat  (12 matches, Ctrl+F to edit)");
 s.overlay(false,"hat",nullptr,0);assert(text()=="Filter: hat  (Ctrl+F to edit)");
 s.overlay(true,nullptr,nullptr,2);assert(text()=="Search: _");
 out.fill('!');assert(s.status(nullptr,128)==0);
 assert(s.status(out.data(),7)==0 && out[0]=='!');
 assert(s.status(out.data(),8)==7 && std::string(out.data())=="Search:" && out[8]=='!');
 s.overlay(true,"x",nullptr,-1);assert(text()=="Search: x_");
 s.rebuild();s.rebuild();assert(s.consume_rebuild());assert(!s.consume_rebuild());
 std::thread a([&]{for(int i=0;i<2000;++i)s.overlay(i%2,"query",rows,2);});
 std::thread b([&]{for(int i=0;i<2000;++i){s.applied(i%2);s.rebuild();s.possessed(i%2);}});
 for(int i=0;i<2000;++i){s.status(out.data(),128);s.consume_rebuild();}
 a.join();b.join();
}
