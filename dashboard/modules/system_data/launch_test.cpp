// Link-time wrappers prove dispatch/fallback behavior without launching a title.
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdlib>
extern "C" int exbox_feed_title_installed(const char*);
extern "C" int exbox_feed_launch_title(const char*);
extern "C" void exbox_launch_test_root(const char*);
static int calls=0,mode=0;
struct Parameters{std::uint32_t size,user,options;std::uint64_t crash;std::uint32_t check;};
static int lnc(const char* id,const char**,Parameters* p){assert(!std::strcmp(id,"PPSA00001"));++calls;if(p)assert(p->size==sizeof *p&&p->user==7);return mode==1?0:-1;}
static int system_launch(const char*,const char**,void*){++calls;return 0;}
extern "C" int sceUserServiceGetInitialUser(int* user){*user=7;return 0;}
extern "C" void* __wrap_dlopen(const char*,int){return mode==3?nullptr:reinterpret_cast<void*>(1);}
extern "C" void* __wrap_dlsym(void*,const char* name){if(mode==2)return nullptr;return !std::strcmp(name,"sceLncUtilLaunchApp")?reinterpret_cast<void*>(&lnc):reinterpret_cast<void*>(&system_launch);}
int main(){char temp[]="/tmp/exbox-launch-XXXXXX";assert(mkdtemp(temp));std::filesystem::path root(temp);exbox_launch_test_root(temp);
 auto app=root/"user/appmeta/PPSA00001";std::filesystem::create_directories(app);
 assert(!exbox_feed_title_installed("PPSA00001"));std::filesystem::create_directory(app/"param.json");assert(!exbox_feed_title_installed("PPSA00001"));std::ofstream(app/"param.sfo")<<"fixture";
 assert(exbox_feed_title_installed("PPSA00001"));assert(!exbox_feed_title_installed("../PPSA00001"));assert(exbox_feed_launch_title("missing")<0&&calls==0);
 assert(exbox_feed_launch_title("PPSA00001")==0&&calls==3);mode=1;calls=0;assert(exbox_feed_launch_title("PPSA00001")==0&&calls==1);
 mode=2;calls=0;assert(exbox_feed_launch_title("PPSA00001")<0&&calls==0);mode=3;assert(exbox_feed_launch_title("PPSA00001")<0&&calls==0);
 std::filesystem::remove_all(root);
}
