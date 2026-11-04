// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/xui_heading.hpp"
#include <cassert>
using exbox::editor::HeadingTracker;
int main(){
 HeadingTracker unique;
 assert(!unique.observe(0,1,"ignored"));
 assert(!unique.observe(1,1,"Tops"));
 assert(!unique.observe(1,1,"Tops")); // Duplicates do not advance streak.
 assert(!unique.observe(1,2,"Tops"));
 assert(unique.observe(1,3,"Tops"));
 assert(unique.observe(9,3,"Tops")); // Label moves to another handle.
 assert(!unique.observe(9,4,"Other")); // Equal live streak cannot steal heading.
 assert(unique.observe(10,4,"Tops"));
 assert(!unique.observe(1,8,"Other")); // Gap resets streak.
 HeadingTracker stale;
 assert(!stale.observe(1,1,"old"));assert(!stale.observe(1,2,"old"));
 assert(stale.observe(1,3,"old"));
 assert(!stale.observe(2,10,"new"));assert(!stale.observe(2,11,"new"));
 assert(stale.observe(2,12,"new"));assert(!stale.observe(3,12,"old"));
 HeadingTracker empty;
 assert(!empty.observe(1,1,""));assert(!empty.observe(1,2,""));
 assert(empty.observe(1,3,""));assert(!empty.observe(2,3,""));
 HeadingTracker bounded;
 for(uint32_t i=1;i<30;++i)assert(!bounded.observe(i,i,"x"));
 assert(!bounded.observe(29,30,"x"));assert(bounded.observe(29,31,"x"));
 HeadingTracker wrap;
 assert(!wrap.observe(1,UINT32_MAX-1,"x"));
 assert(!wrap.observe(1,UINT32_MAX,"x"));assert(wrap.observe(1,0,"x"));
}
