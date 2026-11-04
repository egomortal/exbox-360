#include "../../system_data.hpp"
#include "../../ps5_library.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
int main(int argc,char** argv){
 assert(argc==3);const auto port=std::uint16_t(std::stoi(argv[1]));sysdata::configure(true,port,argv[2]);
 exbox::system_data::Client client(port);std::string text;assert(client.text("version","",64,text)&&text=="EXBOX-FEED 1\n");
 auto games=ps5lib::installed_games();assert(games.size()==1&&games[0].title_id=="PPSA00001"&&games[0].title=="Test Game");
 assert(games[0].icon=="/user/appmeta/PPSA00001/icon0.png");
 const auto path=sysdata::local_path(games[0].icon);assert(!path.empty());std::ifstream f(path,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(f),{}};assert(bytes=="original-icon-bytes");assert(sysdata::local_path(games[0].icon)==path);
 assert(sysdata::read("/system_data/priv/home/user_list.dat",4)=="USER");
 auto photos=ps5lib::find_captures();assert(photos.size()==1&&photos[0]=="/user/av_contents/photo/20261029_100000.jpg");
 assert(!client.text("file","/user/appmeta/badlen",64,text));assert(!client.text("file","/user/appmeta/short",64,text));
 assert(!sysdata::remote("/data/nxe/music"));sysdata::configure(false);assert(!sysdata::remote("/user/appmeta/PPSA00001/icon0.png"));
}
