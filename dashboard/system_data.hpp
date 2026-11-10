#pragma once
// Ex-Box addition: system-data access. /data remains direct. Native callers
// select the companion only when /user/appmeta cannot be opened at startup.
#include "modules/system_data/client.hpp"
#include "modules/system_data/server_files.hpp"
#include <cstdio>
#include <sstream>
#include <limits>
#include <sys/statvfs.h>
#include <algorithm>
namespace sysdata {
using exbox::system_data::Entry;
inline bool companion=false;
inline std::uint16_t companion_port=7090;
inline std::string cache_root="/data/nxe/system-cache";
inline bool volume_root(const std::string& path){return path=="/user"||path=="/mnt"||path=="/mnt/usb0"||path=="/mnt/usb1"||path=="/mnt/ext0"||path=="/mnt/ext1";}
inline bool remote(const std::string& path){return companion&&(volume_root(path)||exbox::system_data::ReadOnlyFiles::root_for(path)!=nullptr);}
inline void configure(bool enabled,std::uint16_t port=7090,const std::string& cache="/data/nxe/system-cache"){companion=enabled;companion_port=port;cache_root=cache;}
inline void initialize(){
 std::string version;
#ifdef EXBOX_NATIVE_TITLE
 // ShadowMount can make /user/appmeta open successfully while exposing an
 // empty/incomplete view to the title.  Prefer the bounded loopback feed when
 // it actually answers; only use direct discovery when no feed is available.
 companion=exbox::system_data::Client(companion_port).text("version","",64,version);
 if(!companion){DIR* directory=opendir("/user/appmeta");companion=!directory;if(directory)closedir(directory);}
#else
 companion=false;
#endif
 std::fprintf(stderr,"Ex-Box system data: %s\n",companion?"companion blocking TCP":"direct filesystem");
 if(companion){if(!version.empty())std::fprintf(stderr,"companion version: %s",version.c_str());else std::fprintf(stderr,"companion selected as direct filesystem fallback is unavailable errno=%d\n",errno);}
}
inline bool parse_entry(const std::string& row,Entry& output){
 std::istringstream in(row);std::string type,size,time,device,name,extra;
 if(!std::getline(in,type,'\t')||!std::getline(in,size,'\t')||!std::getline(in,time,'\t')||!std::getline(in,device,'\t')||!std::getline(in,name,'\t')||std::getline(in,extra,'\t'))return false;
 if((type!="d"&&type!="f")||size.empty()||time.empty()||size.find_first_not_of("0123456789")!=std::string::npos)return false;
 errno=0;char* end=nullptr;auto bytes=std::strtoull(size.c_str(),&end,10);if(errno||!end||*end)return false;
 errno=0;auto stamp=std::strtoll(time.c_str(),&end,10);if(errno||!end||*end)return false;
 errno=0;auto dev=std::strtoull(device.c_str(),&end,10);if(device.empty()||device.find_first_not_of("0123456789")!=std::string::npos||errno||!end||*end)return false;
 std::string decoded;if(!exbox::system_data::unhex(name,decoded))return false;output={decoded,type[0],bytes,stamp,dev};return true;
}
// Bulk names avoid two metadata round trips per title. Unknown metadata stays
// visibly unresolved (the title ID), never guessed from another application.
struct AppRecord{std::string id,name,category;std::uint64_t icon_size=0;std::int64_t icon_mtime=0;};
inline bool apps(std::vector<AppRecord>& result){
 result.clear();std::string data;
 if(!companion||!exbox::system_data::Client(companion_port).text("apps","",exbox::system_data::max_listing_bytes,data))return false;
 std::istringstream rows(data);std::string row;
 while(std::getline(rows,row)){
  std::istringstream fields(row);std::string id,name,category,size,mtime,extra;AppRecord app;
  if(!std::getline(fields,id,'\t')||!std::getline(fields,name,'\t')||!std::getline(fields,category,'\t')||!std::getline(fields,size,'\t')||!std::getline(fields,mtime,'\t')||std::getline(fields,extra,'\t'))return false;
  Entry check;if(!parse_entry("f\t"+size+"\t"+mtime+"\t0\t"+id,check)||!exbox::system_data::unhex(name,app.name)||!exbox::system_data::unhex(category,app.category))return false;
  app.id=check.name;app.icon_size=check.size;app.icon_mtime=check.mtime;result.push_back(std::move(app));if(result.size()>512)return false;
 }
 return true;
}
inline bool info(const std::string& path,Entry& out){
 if(remote(path)){std::string text;if(!exbox::system_data::Client(companion_port).text(volume_root(path)?"volume":"stat",path,16384,text))return false;if(text.empty()||text.back()!='\n')return false;text=text.substr(0,text.find('\n'));return parse_entry(text,out);}
 struct stat st{};if(::stat(path.c_str(),&st))return false;out={path,S_ISDIR(st.st_mode)?'d':S_ISREG(st.st_mode)?'f':'?',std::uint64_t(std::max<off_t>(0,st.st_size)),std::int64_t(st.st_mtime),std::uint64_t(st.st_dev)};return true;
}
inline int stat(const std::string& path,struct stat* result){
 if(!remote(path))return ::stat(path.c_str(),result);
 Entry entry;if(!info(path,entry))return -1;*result={};result->st_mode=(entry.type=='d'?S_IFDIR:S_IFREG)|0444;result->st_size=static_cast<off_t>(entry.size);result->st_mtime=static_cast<time_t>(entry.mtime);result->st_dev=static_cast<dev_t>(entry.device);return 0;
}
inline int lstat(const std::string& path,struct stat* out){return remote(path)?stat(path,out): ::lstat(path.c_str(),out);}
// Parse every volume field strictly before exposing it to capacity arithmetic.
inline bool parse_volume(const std::string& data,struct statvfs* result){
 auto split=data.find('\n');Entry entry;
 if(split==std::string::npos||!parse_entry(data.substr(0,split),entry)||entry.type!='d')return false;
 std::istringstream fields(data.substr(split+1));std::string token;std::uint64_t values[3]{};
 for(auto& value:values){
  if(!(fields>>token)||token.empty()||token.find_first_not_of("0123456789")!=std::string::npos)return false;
  errno=0;char* end=nullptr;value=std::strtoull(token.c_str(),&end,10);if(errno||!end||*end)return false;
 }
 if(fields>>token||!values[1]||values[2]>values[0])return false;
 *result={};result->f_blocks=values[0];result->f_frsize=values[1];result->f_bavail=values[2];
 return std::uint64_t(result->f_blocks)==values[0]&&std::uint64_t(result->f_frsize)==values[1]&&std::uint64_t(result->f_bavail)==values[2];
}
inline int statvfs(const std::string& path,struct statvfs* result){
 if(!companion||!volume_root(path))return ::statvfs(path.c_str(),result);
 std::string data;if(!exbox::system_data::Client(companion_port).text("volume",path,16384,data))return -1;
 if(!parse_volume(data,result)){errno=EPROTO;return -1;}return 0;
}
inline std::vector<Entry> list(const std::string& path){
 std::vector<Entry> entries;
 if(remote(path)){std::string data;if(!exbox::system_data::Client(companion_port).text("list",path,exbox::system_data::max_listing_bytes,data))return entries;std::istringstream lines(data);std::string row;while(std::getline(lines,row)){Entry e;if(!parse_entry(row,e)||e.name=="."||e.name==".."||e.name.find('/')!=std::string::npos)return {};entries.push_back(std::move(e));}return entries;}
 DIR* directory=opendir(path.c_str());if(!directory)return entries;while(auto* e=readdir(directory)){if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;Entry entry;entry.name=e->d_name;struct stat st{};if(!::stat((path+"/"+entry.name).c_str(),&st)){entry.type=S_ISDIR(st.st_mode)?'d':S_ISREG(st.st_mode)?'f':'?';entry.size=std::uint64_t(std::max<off_t>(0,st.st_size));entry.mtime=std::int64_t(st.st_mtime);entry.device=std::uint64_t(st.st_dev);}entries.push_back(std::move(entry));}closedir(directory);return entries;
}
inline std::string read(const std::string& path,std::size_t limit){
 std::string out;if(remote(path)){(void)exbox::system_data::Client(companion_port).text("file",path,limit,out);return out;}
 FILE* file=std::fopen(path.c_str(),"rb");if(!file)return out;char bytes[4096];while(out.size()<limit){auto n=std::fread(bytes,1,std::min(sizeof bytes,limit-out.size()),file);if(!n)break;out.append(bytes,n);}std::fclose(file);return out;
}
// Native-only source-byte cache for libraries whose decoders need a filename.
// Existing SDL surface caches/limits remain unchanged; downloads stream in 32K.
inline std::string local_path(const std::string& path){
 if(!remote(path))return path;
 Entry e;if(!info(path,e)||e.type!='f'||e.size>exbox::system_data::max_file_bytes)return {};
 std::uint64_t hash=1469598103934665603ull;for(unsigned char c:path){hash^=c;hash*=1099511628211ull;}
 char key[32];std::snprintf(key,sizeof key,"%016llx",static_cast<unsigned long long>(hash));
 if(mkdir(cache_root.c_str(),0755)&&errno!=EEXIST)return {};
 const auto target=cache_root+"/"+key;const auto expected=path+"\n"+std::to_string(e.size)+"\n"+std::to_string(e.mtime)+"\n";
 struct stat st{};if(read(target+".meta",8192)==expected&&!::stat(target.c_str(),&st)&&std::uint64_t(st.st_size)==e.size)return target;
 std::string pattern=target+".XXXXXX";std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);int fd=mkstemp(temporary.data());if(fd<0)return {};
 std::uint64_t written=0;const bool received=exbox::system_data::Client(companion_port).request("file",path,e.size,[&](const char* bytes,std::size_t size){while(size){auto n=::write(fd,bytes,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;bytes+=n;size-=std::size_t(n);written+=std::uint64_t(n);}return true;});
 const bool ok=received&&written==e.size&&!fsync(fd);close(fd);if(!ok||rename(temporary.data(),target.c_str())){unlink(temporary.data());return {};}
 // Metadata is only a freshness hint: a partial write causes a fresh download.
 if(FILE* meta=std::fopen((target+".meta").c_str(),"wb")){std::fwrite(expected.data(),1,expected.size(),meta);std::fclose(meta);}return target;
}
}
