#include "server_files.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <chrono>
extern "C" void exbox_feed_dispatch(int,const char*);
extern "C" void exbox_feed_test_root(const char*);
using namespace exbox::system_data;
static int begin(const std::string& command){int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));timeval t{2,0};assert(!setsockopt(pair[0],SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t));exbox_feed_dispatch(pair[1],command.c_str());close(pair[1]);return pair[0];}
static std::string finish(int fd){std::string result;char bytes[2048];for(;;){auto n=recv(fd,bytes,sizeof bytes,0);assert(n>=0);if(!n)break;result.append(bytes,std::size_t(n));}close(fd);return result;}
static std::string call(const std::string& verb,const std::string& path,std::uint64_t limit=4096){return finish(begin("FEED1\t"+verb+"\t"+hex(path)+"\t"+std::to_string(limit)));}
int main(){
 char temp[]="/tmp/exbox-feed-server-XXXXXX";assert(mkdtemp(temp));std::filesystem::path p(temp);std::filesystem::create_directories(p/"user/appmeta/PPSA00001");
 std::ofstream(p/"user/appmeta/PPSA00001/param.json")<<"{\"titleName\":\"Fixture\",\"category\":\"gd\"}";
 std::filesystem::create_directories(p/"user/app/PPSA00002");
 std::ofstream(p/"user/appmeta/PPSA00001/icon0.png")<<"original";exbox_feed_test_root(temp);
 assert(call("version","")=="OK\t13\nEXBOX-FEED 1\n");
 assert(call("file","/user/appmeta/PPSA00001/icon0.png",4)=="OK\t4\norig");
 auto apps=call("apps","");assert(apps.find(hex("PPSA00002"))!=std::string::npos);assert(apps.find(hex("Fixture"))!=std::string::npos&&apps.find(hex("gd"))!=std::string::npos);
 assert(call("file","/etc/passwd").rfind("ERR\t",0)==0);assert(call("file","/user/appmeta/../private").rfind("ERR\t",0)==0);
 assert(finish(begin("FEED1\tfile\t00\t100")).rfind("ERR\t",0)==0);
 const auto file=p/"user/appmeta/PPSA00001/large";int fd=open(file.c_str(),O_WRONLY|O_CREAT,0600);assert(fd>=0&&!ftruncate(fd,8*1024*1024));close(fd);
 int busy[4];for(auto& peer:busy)peer=begin("FEED1\tfile\t"+hex("/user/appmeta/PPSA00001/large")+"\t8388608");
 assert(call("version","").rfind("ERR\t",0)==0);for(auto peer:busy)close(peer);
 std::this_thread::sleep_for(std::chrono::milliseconds(200));assert(call("version","")=="OK\t13\nEXBOX-FEED 1\n");
 std::filesystem::remove_all(p);
}
