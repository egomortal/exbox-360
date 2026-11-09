// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/xui_range.hpp"
#include <cassert>
#include <vector>
using exbox::editor::xui_range_allowed;
int main(){
 std::vector<uint32_t> visits;
 auto allow=[&](uint32_t at){visits.push_back(at);return true;};
 assert(!xui_range_allowed(0,4,allow));assert(!xui_range_allowed(65535,4,allow));
 assert(!xui_range_allowed(65536,0,allow));assert(!xui_range_allowed(65536,65537,allow));
 assert(!xui_range_allowed(UINT32_MAX,2,allow));assert(visits.empty());
 assert(xui_range_allowed(65536,1,allow));assert(visits==std::vector<uint32_t>{65536});visits.clear();
 assert(xui_range_allowed(0x10fff,3,allow));assert((visits==std::vector<uint32_t>{0x10fff,0x11000,0x11001}));visits.clear();
 assert(xui_range_allowed(UINT32_MAX-3,4,allow));assert((visits==std::vector<uint32_t>{UINT32_MAX-3,UINT32_MAX}));
 assert(!xui_range_allowed(0x10000,8193,[](uint32_t at){return at!=0x11000;}));
 visits.clear();assert(xui_range_allowed(0x10000,65536,allow));assert(visits.size()==17 && visits.back()==0x1ffff);
}
