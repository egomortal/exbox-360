// Ex-Box addition: the existing LAUNCH command accepts a validated installed
// title ID for native callers. The dashboard's existing launch sequence is
// moved to its companion; file-feed commands remain strictly read-only.
#include "server_files.hpp"
#include "../../ps5_library.hpp"
#include <dlfcn.h>
#include <cstdint>
extern "C" int sceUserServiceGetInitialUser(int*);
namespace { std::string launch_test_root; }
#ifdef EXBOX_FEED_TEST
extern "C" void exbox_launch_test_root(const char* root){launch_test_root=root;}
#endif
extern "C" int exbox_feed_title_installed(const char* id){
 if(!id||!ps5lib::title_id(id)||ps5lib::hidden_title(id))return 0;
 exbox::system_data::ReadOnlyFiles files(launch_test_root);exbox::system_data::Entry entry;
 for(const char* root:{"/user/appmeta/","/system_data/priv/appmeta/"})
  for(const char* metadata:{"/param.json","/param.sfo"})
   if(files.stat_path(std::string(root)+id+metadata,entry)&&entry.type=='f')return 1;
 return 0;
}
extern "C" int exbox_feed_launch_title(const char* id){
 if(!exbox_feed_title_installed(id))return -1;
 struct Parameters{std::uint32_t size,user,options;std::uint64_t crash;std::uint32_t check;};
 using Lnc=int(*)(const char*,const char**,Parameters*);using Sys=int(*)(const char*,const char**,void*);
 void* module=dlopen("libSceSystemService.sprx",RTLD_LAZY);if(!module)return -1;
 auto lnc=reinterpret_cast<Lnc>(dlsym(module,"sceLncUtilLaunchApp"));auto sys=reinterpret_cast<Sys>(dlsym(module,"sceSystemServiceLaunchApp"));
 const char* args[]={nullptr};int user=-1;(void)sceUserServiceGetInitialUser(&user);Parameters p{};p.size=sizeof p;p.user=std::uint32_t(user);
 int result=-1;if(lnc){result=lnc(id,args,&p);if(result<0)result=lnc(id,args,nullptr);}if(result<0&&sys)result=sys(id,args,nullptr);
 // SystemService is process-lived, like the original dashboard's resolver.
 return result;
}
