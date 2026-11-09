#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace av2::mesa_cache {
enum class State { Ready, UserDisabled, PathFailure, EnvironmentFailure };
struct Result { State state=State::PathFailure; bool preserved=false,disable_requested=false; int error=0; char path[4096]{}; };
using SetOption=int(*)(const char*,const char*,int);
inline bool equal_fold(const char* a,const char* b){while(*a&&*b){char c=*a++,d=*b++;if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(c!=d)return false;}return *a==*b;}
inline bool enabled_value(const char* p){return p&&(equal_fold(p,"1")||equal_fold(p,"y")||equal_fold(p,"yes")||equal_fold(p,"t")||equal_fold(p,"true"));}
inline bool make_directories(const char* path){
 char copy[4096];size_t n=std::strlen(path);if(!n||path[0]!='/'||n>=sizeof(copy)){errno=EINVAL;return false;}std::memcpy(copy,path,n+1);
 for(size_t i=1;i<=n;++i)if(copy[i]=='/'||copy[i]==0){char saved=copy[i];copy[i]=0;if(mkdir(copy,0755)!=0&&errno!=EEXIST)return false;copy[i]=saved;}
 struct stat st{};if(stat(path,&st)!=0)return false;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return false;}return true;
}
inline bool write_probe(const char* path){
 char file[4200];int fd=-1;
 for(unsigned attempt=0;attempt<8;++attempt){int n=std::snprintf(file,sizeof(file),"%s/.av2-cache-probe-%ld-%u",path,static_cast<long>(getpid()),attempt);if(n<0||size_t(n)>=sizeof(file)){errno=ENAMETOOLONG;return false;}fd=open(file,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd>=0)break;if(errno!=EEXIST)return false;}
 if(fd<0)return false;const char byte='1';ssize_t written;do{written=write(fd,&byte,1);}while(written<0&&errno==EINTR);int saved=written==1?0:errno;bool ok=written==1;if(close(fd)!=0){ok=false;saved=errno;}if(unlink(file)!=0){ok=false;saved=errno;}if(!ok)errno=saved;return ok;
}
inline Result configure(const char* default_path,SetOption set_option=&setenv){
 Result result;const char* disable=getenv("MESA_SHADER_CACHE_DISABLE");if(!disable)disable=getenv("MESA_GLSL_CACHE_DISABLE");
 if(enabled_value(disable)||enabled_value(getenv("MESA_GLSL_DISABLE_IO_OPT"))){result.state=State::UserDisabled;result.preserved=true;return result;}
 const char* path=getenv("MESA_SHADER_CACHE_DIR");if(!path)path=getenv("MESA_GLSL_CACHE_DIR");result.preserved=path!=nullptr;if(!path)path=default_path;
 const size_t size=path?std::strlen(path):0;
 if(!size||size>=sizeof(result.path)||path[0]!='/'){result.error=EINVAL;result.disable_requested=set_option("MESA_SHADER_CACHE_DISABLE","true",1)==0;return result;}
 std::memcpy(result.path,path,size+1);
 if(!make_directories(result.path)||!write_probe(result.path)){result.error=errno;result.disable_requested=set_option("MESA_SHADER_CACHE_DISABLE","true",1)==0;return result;}
 if(!result.preserved && set_option("MESA_SHADER_CACHE_DIR",result.path,0)!=0){result.state=State::EnvironmentFailure;result.error=errno;result.disable_requested=set_option("MESA_SHADER_CACHE_DISABLE","true",1)==0;return result;}
 result.state=State::Ready;return result;
}
}
