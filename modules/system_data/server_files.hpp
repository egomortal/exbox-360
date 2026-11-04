#pragma once
// Ex-Box addition: read-only filesystem boundary used by the companion.
// A pinned root descriptor plus openat(O_NOFOLLOW) at every component prevents
// both traversal and symlink-swap escapes. Symlinks are conservatively refused.
#include "protocol.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
namespace exbox::system_data {
struct OwnedFd {int value=-1;explicit OwnedFd(int fd=-1):value(fd){}~OwnedFd(){if(value>=0)close(value);}OwnedFd(const OwnedFd&)=delete;OwnedFd& operator=(const OwnedFd&)=delete;int release(){int fd=value;value=-1;return fd;}};
class ReadOnlyFiles {
 std::string prefix_;
 static bool boundary(const std::string& path,const char* root){const auto n=std::strlen(root);return path.compare(0,n,root)==0&&(path.size()==n||path[n]=='/');}
public:
 explicit ReadOnlyFiles(std::string test_prefix={}):prefix_(std::move(test_prefix)){}
 static bool valid_path(const std::string& path){
  if(path.empty()||path[0]!='/'||path.size()>4096||path.find('\0')!=std::string::npos||path.find('\\')!=std::string::npos)return false;
  std::size_t start=1;while(start<path.size()){auto end=path.find('/',start);if(end==std::string::npos)end=path.size();auto part=path.substr(start,end-start);if(part.empty()||part=="."||part=="..")return false;start=end+1;}return true;
 }
 static const char* root_for(const std::string& path,bool bytes=false){
  if(!valid_path(path))return nullptr;
  static const char* roots[]={"/user/appmeta","/system_data/priv/appmeta","/system_data/priv/cache/profile","/user/av_contents/photo","/user/av_contents/thumbnails/photo","/user/av_contents/video","/mnt/usb0","/mnt/usb1","/mnt/ext0","/mnt/ext1"};
  for(auto root:roots)if(boundary(path,root)){if(bytes&&!std::strcmp(root,"/user/av_contents/video"))return nullptr;return root;}
  if(path=="/system_data/priv/home/user_list.dat")return "/system_data/priv/home/user_list.dat";
  return nullptr;
 }
 int open_path(const std::string& path,bool bytes=false)const{
  const char* root=root_for(path,bytes);if(!root){errno=EACCES;return -1;}
  std::string local=prefix_+root;OwnedFd current(open(local.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK));if(current.value<0)return -1;
  std::size_t start=std::strlen(root);if(start<path.size())++start;
  while(start<path.size()){
   auto end=path.find('/',start);if(end==std::string::npos)end=path.size();const auto part=path.substr(start,end-start);
   int child=openat(current.value,part.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(child<0)return -1;close(current.value);current.value=child;start=end+1;
  }
  struct stat st{};if(fstat(current.value,&st))return -1;
  if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)){errno=EACCES;return -1;}
  if(bytes&&(!S_ISREG(st.st_mode)||st.st_size<0||std::uint64_t(st.st_size)>max_file_bytes)){errno=EFBIG;return -1;}
  return current.release();
 }
 bool stat_path(const std::string& path,Entry& out)const{OwnedFd fd(open_path(path));if(fd.value<0)return false;struct stat st{};if(fstat(fd.value,&st))return false;out={path,S_ISDIR(st.st_mode)?'d':'f',std::uint64_t(std::max<off_t>(0,st.st_size)),std::int64_t(st.st_mtime),std::uint64_t(st.st_dev)};return true;}
 bool list(const std::string& path,std::vector<Entry>& out)const{
  OwnedFd fd(open_path(path));if(fd.value<0)return false;const int raw=fd.release();DIR* dir=fdopendir(raw);if(!dir){close(raw);return false;}
  std::vector<Entry> rows;bool ok=true;errno=0;
  for(;;){
   errno=0;auto* e=readdir(dir);if(!e){if(errno)ok=false;break;}
   if(!std::strcmp(e->d_name,".")||!std::strcmp(e->d_name,".."))continue;
   struct stat st{};if(fstatat(dirfd(dir),e->d_name,&st,AT_SYMLINK_NOFOLLOW))continue;
   if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))continue;
   rows.push_back({e->d_name,S_ISDIR(st.st_mode)?'d':'f',std::uint64_t(std::max<off_t>(0,st.st_size)),std::int64_t(st.st_mtime),std::uint64_t(st.st_dev)});
   if(rows.size()>10000){errno=EFBIG;ok=false;break;}
  }
  closedir(dir);if(!ok)return false;out=std::move(rows);return true;
 }
};
}
