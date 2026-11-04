// Ex-Box addition: bounded, read-only companion transport diagnostics.
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using Log=void(*)(const char*,...);
static void exchange(int fd,const sockaddr* address,socklen_t size,const char* label,Log log){
  const timeval timeout{2,0};
  if(setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))||setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))){log("%s timeout setup failed errno=%d; skipping unbounded connect\n",label,errno);close(fd);return;}
  errno=0;const int rc=connect(fd,address,size);log("%s blocking connect rc=%d errno=%d\n",label,rc,errno);
  if(!rc){
    constexpr char request[]="GET / HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    const auto sent=send(fd,request,sizeof(request)-1,MSG_NOSIGNAL);log("%s send=%lld errno=%d\n",label,(long long)sent,errno);
    if(sent==sizeof(request)-1){char response[256]{};const auto got=recv(fd,response,sizeof(response)-1,0);if(got>0){if(char* e=strchr(response,'\r'))*e=0;if(char* e=strchr(response,'\n'))*e=0;}log("%s receive=%lld errno=%d status=%s\n",label,(long long)got,errno,response);}
  }
  close(fd);
}
extern "C" void exbox_probe_alternate_transports(Log log){
  int fd=socket(AF_INET,SOCK_STREAM,0);log("companion plain TCP socket=%d errno=%d\n",fd,errno);
  if(fd>=0){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(7090);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);exchange(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a),"companion plain TCP",log);}
  // No speculative port scans or other running services are contacted.
  fd=socket(AF_UNIX,SOCK_STREAM,0);log("companion AF_UNIX socket=%d errno=%d\n",fd,errno);
  if(fd>=0){sockaddr_un a{};a.sun_family=AF_UNIX;std::strcpy(a.sun_path,"/data/nxe/companion.sock");exchange(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a),"companion AF_UNIX",log);}
  constexpr char root[]="/data/nxe/companion-spool";
  if(mkdir(root,0777)&&errno!=EEXIST){log("spool mkdir failed errno=%d\n",errno);return;}
  struct stat st{};if(lstat(root,&st)||!S_ISDIR(st.st_mode)){log("spool rejected non-directory\n");return;}
  char temporary[]="/data/nxe/companion-spool/.probe-XXXXXX";
  fd=mkstemp(temporary);if(fd<0){log("spool create failed errno=%d\n",errno);return;}
  constexpr char ping[]="EXBOX-PROBE-1\n";const bool wrote=write(fd,ping,sizeof(ping)-1)==sizeof(ping)-1&&fsync(fd)==0;close(fd);
  char request[256],response[256];std::snprintf(request,sizeof(request),"%s/probe-%s.req",root,temporary+std::strlen(temporary)-6);std::snprintf(response,sizeof(response),"%s/probe-%s.rsp",root,temporary+std::strlen(temporary)-6);
  if(!wrote||rename(temporary,request)){log("spool publish failed errno=%d\n",errno);unlink(temporary);return;}
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);bool success=false;
  do{
    fd=open(response,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);if(fd>=0){char bytes[64]{};struct stat info{};if(!fstat(fd,&info)&&S_ISREG(info.st_mode)&&info.st_size==17){const auto n=read(fd,bytes,sizeof(bytes));success=n==17&&!memcmp(bytes,"EXBOX-PROBE-1 OK\n",17);}close(fd);break;}
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }while(std::chrono::steady_clock::now()<deadline);
  log("companion file spool: %s errno=%d\n",success?"ok":"failed or timed out",errno);unlink(request);unlink(response);
}
