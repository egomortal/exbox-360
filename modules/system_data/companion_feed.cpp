// Ex-Box addition: bounded read-only feed workers for the existing companion.
#include "server_files.hpp"
#include "../../ps5_library.hpp"
#include <atomic>
#include <thread>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <chrono>
#include <cstdio>
namespace {
using namespace exbox::system_data;
std::atomic<unsigned> clients{0};
std::string filesystem_prefix; // Empty in production; test setter is compiled out.
void refusal(const std::string& verb,const std::string& path,int error){
 std::fprintf(stderr,"companion feed refused verb=%s path=%s errno=%d\n",verb.c_str(),hex(path).c_str(),error);
 if(FILE* log=std::fopen("/data/nxe/dashboard-daemon.log","a")){std::fprintf(log,"feed refused verb=%s path_hex=%s errno=%d\n",verb.c_str(),hex(path).c_str(),error);std::fclose(log);}
}
class Reply {
 int fd_;std::chrono::steady_clock::time_point deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(5);
public:
 explicit Reply(int fd):fd_(fd){}
 bool send_bytes(const char* bytes,std::size_t size){
  while(size){auto us=std::chrono::duration_cast<std::chrono::microseconds>(deadline_-std::chrono::steady_clock::now()).count();if(us<=0)return false;timeval t{static_cast<time_t>(us/1000000),static_cast<suseconds_t>(us%1000000)};if(setsockopt(fd_,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t))return false;auto n=send(fd_,bytes,size,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)return false;bytes+=n;size-=std::size_t(n);}return true;
 }
 bool text(const std::string& value){return send_bytes(value.data(),value.size());}
 bool header(std::uint64_t count){return text("OK\t"+std::to_string(count)+"\n");}
 void fail(int error){(void)text("ERR\t"+std::to_string(error)+"\n");}
};
std::string category(const std::string& json){auto pos=json.find("\"category\"");if(pos==std::string::npos)return "unknown";pos=json.find(':',pos+10);if(pos==std::string::npos)return "unknown";pos=json.find('"',pos+1);if(pos==std::string::npos)return "unknown";auto end=json.find('"',pos+1);return end!=std::string::npos&&end-pos<64?json.substr(pos+1,end-pos-1):"unknown";}
void serve(int fd,const std::string& command){
 Reply reply(fd);std::vector<std::string> parts;std::istringstream line(command);std::string part;while(std::getline(line,part,'\t'))parts.push_back(part);
 if(parts.size()!=4||parts[0]!="FEED1"||parts[3].empty()||parts[3].find_first_not_of("0123456789")!=std::string::npos){reply.fail(EINVAL);return;}
 std::string path;if(!unhex(parts[2],path)){reply.fail(EINVAL);return;}
 errno=0;char* end=nullptr;auto limit=std::strtoull(parts[3].c_str(),&end,10);if(errno||!end||*end||limit>max_file_bytes){reply.fail(EFBIG);return;}
 const auto& verb=parts[1];ReadOnlyFiles files(filesystem_prefix);std::string output;
 if(verb=="version"&&path.empty())output="EXBOX-FEED 1\n";
 else if(verb=="volume"){
  const bool allowed=path=="/user"||path=="/mnt"||path=="/mnt/usb0"||path=="/mnt/usb1"||path=="/mnt/ext0"||path=="/mnt/ext1";
  if(!allowed){refusal(verb,path,EACCES);reply.fail(EACCES);return;}
  OwnedFd volume(open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW));struct stat st{};struct statvfs space{};
  if(volume.value<0||fstat(volume.value,&st)||fstatvfs(volume.value,&space)){reply.fail(errno);return;}
  output=entry_line({path,'d',0,std::int64_t(st.st_mtime),std::uint64_t(st.st_dev)})+std::to_string(space.f_blocks)+"\t"+std::to_string(space.f_frsize)+"\t"+std::to_string(space.f_bavail)+"\n";
 }
 else if(verb=="stat"){Entry e;if(!files.stat_path(path,e)){int error=errno;refusal(verb,path,error);reply.fail(error);return;}output=entry_line(e);}
 else if(verb=="list"){
  std::vector<Entry> entries;if(!files.list(path,entries)){int error=errno;refusal(verb,path,error);reply.fail(error);return;}
  for(const auto& e:entries){output+=entry_line(e);if(output.size()>max_listing_bytes){refusal(verb,path,EFBIG);reply.fail(EFBIG);return;}}
 }else if(verb=="file"){
  OwnedFd file(files.open_path(path,true));if(file.value<0){int error=errno;refusal(verb,path,error);reply.fail(error);return;}
  struct stat st{};if(fstat(file.value,&st)){reply.fail(errno);return;}std::uint64_t left=std::min<std::uint64_t>(std::uint64_t(st.st_size),limit);if(!reply.header(left))return;
  char bytes[32768];while(left){auto n=read(file.value,bytes,std::size_t(std::min<std::uint64_t>(left,sizeof bytes)));if(n<0&&errno==EINTR)continue;if(n<=0||!reply.send_bytes(bytes,std::size_t(n)))return;left-=std::uint64_t(n);}return;
 }else if(verb=="apps"&&path.empty()){
  auto apps=ps5lib::installed_games(filesystem_prefix);
  // Discovery-only fallback: never expose /user/app package bytes to clients.
  OwnedFd app_root(open((filesystem_prefix+"/user/app").c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW));
  if(app_root.value>=0){
   const int scan_fd=dup(app_root.value); DIR* directory=scan_fd<0?nullptr:fdopendir(scan_fd);
   if(!directory&&scan_fd>=0)close(scan_fd);
   if(directory){while(auto* item=readdir(directory)){
    const std::string id=item->d_name;
    if(!ps5lib::title_id(id)||ps5lib::hidden_title(id)||apps.size()>=512)continue;
    struct stat st{};if(fstatat(app_root.value,id.c_str(),&st,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(st.st_mode))continue;
    if(std::none_of(apps.begin(),apps.end(),[&](const dash::App& app){return app.title_id==id;})){
     dash::App app;app.title_id=id;app.title=id;apps.push_back(std::move(app));
    }
   }closedir(directory);}
  }
  for(const auto& app:apps){
   Entry icon;std::string base="/user/appmeta/";
   Entry metadata;if(!files.stat_path(base+app.title_id+"/param.json",metadata))base="/system_data/priv/appmeta/";
   (void)files.stat_path(std::string(base)+app.title_id+"/icon0.png",icon);
   const auto cat=category(ps5lib::read_file(filesystem_prefix+std::string(base)+app.title_id+"/param.json"));
   output+=hex(app.title_id)+"\t"+hex(app.title)+"\t"+hex(cat)+"\t"+std::to_string(icon.size)+"\t"+std::to_string(icon.mtime)+"\n";
   if(output.size()>max_listing_bytes){reply.fail(EFBIG);return;}
  }
 }else{reply.fail(EINVAL);return;}
 if(output.size()>limit){reply.fail(EFBIG);return;}
 if(reply.header(output.size()))(void)reply.text(output);
}
}
// Called after the existing main-thread parser has read one complete line.
// Duplicating the connection lets that parser retain its existing close logic.
extern "C" void exbox_feed_dispatch(int fd,const char* command){
 unsigned count=clients.load();while(count<4&&!clients.compare_exchange_weak(count,count+1)){}
 if(count>=4){Reply(fd).fail(EBUSY);return;}
 const int owned=dup(fd);if(owned<0){--clients;Reply(fd).fail(errno);return;}
 try{std::thread([owned,text=std::string(command)](){try{serve(owned,text);}catch(...){Reply(owned).fail(EIO);}close(owned);--clients;}).detach();}
 catch(...){close(owned);--clients;Reply(fd).fail(EAGAIN);}
}

#ifdef EXBOX_FEED_TEST
extern "C" void exbox_feed_test_root(const char* root){filesystem_prefix=root;}
#endif
