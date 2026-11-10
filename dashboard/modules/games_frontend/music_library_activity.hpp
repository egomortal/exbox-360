// Ex-Box 360: bounded local listening history and smart music views.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace exbox_music {
enum class Support { Playable, RecognizedUnsupported, ContainerUnsupported, Unknown };
inline std::string lower(std::string value){for(char&c:value)if(c>='A'&&c<='Z')c=char(c+('a'-'A'));return value;}
inline std::string extension(const std::string&path){const auto slash=path.find_last_of("/\\"),dot=path.find_last_of('.');return dot==std::string::npos||(slash!=std::string::npos&&dot<slash)?std::string{}:lower(path.substr(dot+1));}
inline Support audio_support(const std::string&path){const auto e=extension(path);if(e=="wav"||e=="mp3"||e=="flac"||e=="ogg"||e=="oga")return Support::Playable;if(e=="xma"||e=="wma"||e=="fsb")return Support::RecognizedUnsupported;if(e=="iso"||e=="god"||e=="zip"||e=="7z"||e=="rar")return Support::ContainerUnsupported;return Support::Unknown;}
inline const char* support_reason(Support value){return value==Support::Playable?"Playable with the dashboard decoder":value==Support::RecognizedUnsupported?"Recognized audio; approved decoder required":value==Support::ContainerUnsupported?"Container scan staged; no mount, decryption or extraction":"Unknown format; left unchanged";}
struct Track {std::string path,title,album,genre;};
struct GameAlbum {std::string name;std::vector<std::size_t> tracks;};
inline std::vector<GameAlbum> game_albums(const std::vector<Track>&tracks){std::map<std::string,GameAlbum> grouped;for(std::size_t i=0;i<tracks.size();++i){const auto&t=tracks[i];if(t.genre!="Game Soundtrack"||t.album.empty())continue;auto&g=grouped[lower(t.album)];if(g.tracks.empty())g.name=t.album;g.tracks.push_back(i);}std::vector<GameAlbum> out;for(auto&pair:grouped)out.push_back(std::move(pair.second));return out;}
struct Record {std::uint32_t plays=0;std::uint64_t last_play=0,added=0;};
class LibraryActivity {
 static constexpr std::size_t max_records=512,max_path=4096,max_bytes=1u<<20;
 std::map<std::string,Record> records_;std::uint64_t sequence_=0;
 static bool valid_path(const std::string&path){return !path.empty()&&path.size()<=max_path&&path.find('\0')==std::string::npos&&path.find('\n')==std::string::npos&&path.find('\r')==std::string::npos;}
 void trim(){while(records_.size()>max_records){auto drop=std::min_element(records_.begin(),records_.end(),[](const auto&a,const auto&b){const auto ax=std::max(a.second.last_play,a.second.added),bx=std::max(b.second.last_play,b.second.added);return ax==bx?a.first<b.first:ax<bx;});records_.erase(drop);}}
public:
 void observe(const std::vector<std::string>&paths){for(const auto&path:paths)if(valid_path(path)&&!records_.count(path)){records_[path].added=++sequence_;trim();}}
 void played(const std::string&path){if(!valid_path(path))return;auto&r=records_[path];if(!r.added)r.added=++sequence_;r.last_play=++sequence_;if(r.plays!=UINT32_MAX)++r.plays;trim();}
 const Record*find(const std::string&path)const{const auto it=records_.find(path);return it==records_.end()?nullptr:&it->second;}
 std::vector<int> most_played(const std::vector<std::string>&paths)const{return ranked(paths,true);}
 std::vector<int> recently_added(const std::vector<std::string>&paths)const{return ranked(paths,false);}
 std::vector<int> ranked(const std::vector<std::string>&paths,bool plays)const{std::vector<int> out;for(std::size_t i=0;i<paths.size();++i)if(records_.count(paths[i]))out.push_back(int(i));std::stable_sort(out.begin(),out.end(),[&](int a,int b){const auto&x=records_.at(paths[std::size_t(a)]);const auto&y=records_.at(paths[std::size_t(b)]);if(plays&&x.plays!=y.plays)return x.plays>y.plays;const auto ax=plays?x.last_play:x.added,ay=plays?y.last_play:y.added;return ax==ay?lower(paths[std::size_t(a)])<lower(paths[std::size_t(b)]):ax>ay;});return out;}
 std::string encode()const{std::ostringstream out;out<<"EXBOX_MUSIC_ACTIVITY_1 "<<sequence_<<' '<<records_.size()<<'\n';for(const auto&pair:records_)out<<pair.second.plays<<' '<<pair.second.last_play<<' '<<pair.second.added<<' '<<std::quoted(pair.first)<<'\n';const auto value=out.str();return value.size()<=max_bytes?value:std::string{};}
 bool decode(const std::string&text){if(text.size()>max_bytes)return false;std::istringstream in(text);std::string magic;std::uint64_t seq=0;std::size_t count=0;if(!(in>>magic>>seq>>count)||magic!="EXBOX_MUSIC_ACTIVITY_1"||count>max_records)return false;std::map<std::string,Record> next;for(std::size_t i=0;i<count;++i){std::string path;Record r;if(!(in>>r.plays>>r.last_play>>r.added>>std::quoted(path))||!valid_path(path)||!r.added||r.last_play>seq||r.added>seq||!next.emplace(path,r).second)return false;}in>>std::ws;if(!in.eof())return false;records_=std::move(next);sequence_=seq;return true;}
 std::size_t size()const{return records_.size();}
};
} // namespace exbox_music
