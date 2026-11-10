// Ex-Box addition: worker-owned metadata index. Enumerate every refresh; validate each file identity.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace music_index {
struct Stamp {std::uint64_t size=0,device=0;std::int64_t mtime=0;bool valid=false;
 bool operator==(const Stamp& b)const{return valid&&b.valid&&size==b.size&&device==b.device&&mtime==b.mtime;}};
struct Item {music::Song song;Stamp stamp;};
using Index=std::map<std::string,Item>;
inline Stamp stamp(const std::string& path){sysdata::Entry e;if(!sysdata::info(path,e)||e.type!='f')return {};return {e.size,e.device,e.mtime,true};}
inline Index read(const std::string& path){
 Index result;struct stat st{};if(::stat(path.c_str(),&st)||st.st_size<0||st.st_size>8*1024*1024)return result;
 std::ifstream in(path);std::string magic;size_t count=0;if(!(in>>magic>>count)||magic!="EXBOX_MUSIC_INDEX_1"||count>music::kMaxSongs)return {};
 for(size_t n=0;n<count;++n){Item i;auto& s=i.song;if(!(in>>i.stamp.size>>i.stamp.device>>i.stamp.mtime>>std::quoted(s.path)>>std::quoted(s.title)>>std::quoted(s.artist)>>std::quoted(s.album)>>std::quoted(s.genre)))return {};
  if(s.path.empty()||s.path.size()>4096||s.title.size()>65536||s.artist.size()>65536||s.album.size()>65536||s.genre.size()>65536)return {};
  i.stamp.valid=true;if(!result.emplace(s.path,std::move(i)).second)return {};
 }in>>std::ws;if(!in.eof())return {};return result;
}
inline bool write(const std::string& path,const Index& index){
 if(index.size()>music::kMaxSongs)return false;
 const auto temp=path+".tmp";std::ofstream out(temp,std::ios::trunc);if(!out)return false;
 const std::string header="EXBOX_MUSIC_INDEX_1 "+std::to_string(index.size())+"\n";
 out<<header;size_t bytes=header.size();
 for(const auto& row:index){const auto& i=row.second;const auto& s=i.song;
  if(s.path.empty()||s.path.size()>4096||s.title.size()>65536||s.artist.size()>65536||s.album.size()>65536||s.genre.size()>65536){out.close();std::remove(temp.c_str());return false;}
  std::ostringstream line;line<<i.stamp.size<<' '<<i.stamp.device<<' '<<i.stamp.mtime<<' '<<std::quoted(s.path)<<' '<<std::quoted(s.title)<<' '<<std::quoted(s.artist)<<' '<<std::quoted(s.album)<<' '<<std::quoted(s.genre)<<'\n';
  const auto text=line.str();if(text.size()>8*1024*1024-bytes){out.close();std::remove(temp.c_str());return false;}
  out<<text;bytes+=text.size();
 }
 out.close();if(!out){std::remove(temp.c_str());return false;}return std::rename(temp.c_str(),path.c_str())==0;
}

struct Result {std::vector<music::Song> songs;size_t hits=0,misses=0;std::uint64_t scan_ms=0;bool cache_written=false,ok=false;};
inline std::string game_album(const std::string& path,const std::string& root){
 std::string relative=path.size()>root.size()?path.substr(root.size()):std::string{};
 while(!relative.empty()&&relative.front()=='/')relative.erase(relative.begin());
 const auto slash=relative.find('/');if(slash!=std::string::npos&&slash>0)return relative.substr(0,slash);
 const auto end=root.find_last_not_of('/');const auto begin=root.find_last_of('/',end);const auto start=begin==std::string::npos?0:begin+1;return root.substr(start,end-start+1);
}
inline Result scan(const std::vector<std::string>& roots,const std::string& cache,const std::vector<std::string>& game_roots={}){
 const auto started=std::chrono::steady_clock::now();
 const Index old=read(cache);Index next;std::vector<std::string> paths;
 for(const auto& root:roots)if(music::is_directory(root))music::walk(root,0,paths);
 for(const auto& root:game_roots)if(music::is_directory(root))music::walk(root,0,paths);
 std::sort(paths.begin(),paths.end(),[](const auto& a,const auto& b){return music::folded(a)<music::folded(b);});paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
 if(paths.size()>music::kMaxSongs)paths.resize(music::kMaxSongs);
 Result result;
 for(const auto& path:paths){Item item;item.stamp=stamp(path);const auto it=old.find(path);
  if(it!=old.end()&&item.stamp==it->second.stamp){item.song=it->second.song;++result.hits;}
  else{item.song.path=path;music::read_tags(item.song);++result.misses;}
  for(const auto& root:game_roots)if(path.size()>root.size()&&path.compare(0,root.size(),root)==0&&path[root.size()]=='/'){
   item.song.album=game_album(path,root);item.song.genre="Game Soundtrack";break;
  }
  result.songs.push_back(item.song);if(item.stamp.valid)next.emplace(path,std::move(item));
 }
 result.cache_written=write(cache,next);result.scan_ms=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());result.ok=true;return result;
}
}
