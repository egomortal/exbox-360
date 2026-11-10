// Ex-Box 360: bounded, versioned dashboard context persistence.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sstream>
#include <string>

namespace exbox_activity {
struct Context {int channel=4,metro_tab=1,viewer=0,viewer_item=0,music_view=0,music_page=0,music_group=-1;std::string focus_key;};
inline bool safe_key(const std::string&key){if(key.size()>192)return false;for(unsigned char c:key)if(c<32||c==127||c=='"'||c=='\\')return false;return true;}
inline std::string encode(const Context&c){std::ostringstream out;out<<"dashboard_context=1,"<<c.channel<<','<<c.metro_tab<<','<<c.viewer<<','<<c.viewer_item<<','<<c.music_view<<','<<c.music_page<<','<<c.music_group<<','<<c.focus_key;return safe_key(c.focus_key)?out.str():std::string{};}
inline bool decode(const std::string&line,Context&out){constexpr const char*prefix="dashboard_context=1,";if(line.rfind(prefix,0)!=0)return false;std::string rest=line.substr(std::char_traits<char>::length(prefix));while(!rest.empty()&&(rest.back()=='\n'||rest.back()=='\r'))rest.pop_back();std::istringstream in(rest);Context c;char comma=0;if(!(in>>c.channel>>comma)||comma!=',')return false;if(!(in>>c.metro_tab>>comma)||comma!=',')return false;if(!(in>>c.viewer>>comma)||comma!=',')return false;if(!(in>>c.viewer_item>>comma)||comma!=',')return false;if(!(in>>c.music_view>>comma)||comma!=',')return false;if(!(in>>c.music_page>>comma)||comma!=',')return false;if(!(in>>c.music_group>>comma)||comma!=',')return false;std::getline(in,c.focus_key);if(c.channel<0||c.channel>5||c.metro_tab<0||c.metro_tab>7||c.viewer<0||c.viewer>64||c.viewer_item<0||c.viewer_item>100000||c.music_view<0||c.music_view>6||c.music_page<0||c.music_page>6||c.music_group < -1||c.music_group>100000||!safe_key(c.focus_key))return false;out=std::move(c);return true;}
} // namespace exbox_activity
