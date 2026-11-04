#include "server_files.hpp"
#include "../../system_data.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
using namespace exbox::system_data;
int main(){
 char temp[]="/tmp/exbox-feed-files-XXXXXX";assert(mkdtemp(temp));std::filesystem::path p(temp);
 std::filesystem::create_directories(p/"user/appmeta/PPSA00001");std::filesystem::create_directories(p/"user/av_contents/video");
 std::ofstream(p/"user/appmeta/PPSA00001/param.json")<<"{\"titleName\":\"Fixture\"}";
 std::ofstream(p/"outside")<<"private";
 std::filesystem::create_symlink(p/"outside",p/"user/appmeta/PPSA00001/escape");
 std::filesystem::create_directory_symlink(p,p/"user/appmeta/PPSA00001/dir-escape");
 std::ofstream(p/"user/av_contents/video/movie.mp4")<<"no video bytes";
 ReadOnlyFiles files(p.string());Entry e;assert(files.stat_path("/user/appmeta/PPSA00001/param.json",e)&&e.type=='f');
 for(const auto& path:{"/user/appmetaX/secret","/user/appmeta/../outside","/user/appmeta//x","/etc/passwd","/user/appmeta/PPSA00001/escape","/user/appmeta/PPSA00001/dir-escape/outside"})assert(files.open_path(path,true)<0);
 assert(files.open_path("/user/av_contents/video/movie.mp4",true)<0);assert(files.stat_path("/user/av_contents/video/movie.mp4",e));
 std::vector<Entry> entries;assert(files.list("/user/appmeta/PPSA00001",entries)&&entries.size()==1);
 const auto large=p/"user/appmeta/PPSA00001/large";int fd=open(large.c_str(),O_CREAT|O_WRONLY,0600);assert(fd>=0);assert(!ftruncate(fd,max_file_bytes+1));close(fd);assert(files.open_path("/user/appmeta/PPSA00001/large",true)<0&&errno==EFBIG);
 std::string decoded;assert(unhex(hex("space and\nnewline"),decoded)&&decoded=="space and\nnewline");assert(!unhex("00",decoded)&&!unhex("0g",decoded));
 Entry parsed;assert(sysdata::parse_entry(entry_line({"hello",'f',12,34}).substr(0,entry_line({"hello",'f',12,34}).size()-1),parsed)&&parsed.name=="hello"&&parsed.size==12);
 assert(!sysdata::parse_entry("f\t18446744073709551616\t0\t61",parsed));
 struct statvfs volume{};const auto prefix=entry_line({"/user",'d',0,0,1});
 assert(sysdata::parse_volume(prefix+"100\t4096\t25\n",&volume)&&volume.f_bavail==25);
 for(const auto* fields:{"-1 4096 1","100 -1 1","100 4096 101","100 0 1","100 4096 1 extra","18446744073709551616 4096 1"})assert(!sysdata::parse_volume(prefix+fields,&volume));
 sysdata::configure(false);assert(sysdata::read((p/"outside").string(),3)=="pri");assert(!sysdata::remote("/data/nxe/music"));
 sysdata::configure(true);assert(sysdata::remote("/user/appmeta/PPSA00001/param.json"));assert(!sysdata::remote("/data/nxe/music"));
 std::filesystem::remove_all(p); // Exclusive host fixture only.
}
