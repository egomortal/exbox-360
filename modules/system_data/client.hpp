#pragma once
// Ex-Box addition: one bounded request per blocking loopback connection.
// Native titles deny F_SETFL. Do not add O_NONBLOCK/fcntl to this transport.
#include "protocol.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <functional>
namespace exbox::system_data {
class Client {
 std::uint16_t port_;
public:
 explicit Client(std::uint16_t port=7090):port_(port){}
 // sink receives bounded chunks; file downloads can go directly to a disk
 // cache instead of allocating a second full image/audio buffer.
 bool request(const std::string& verb,const std::string& path,std::uint64_t limit,const std::function<bool(const char*,std::size_t)>& sink)const{
  struct Socket{int fd;~Socket(){if(fd>=0)close(fd);}}sock{socket(AF_INET,SOCK_STREAM,0)};if(sock.fd<0)return false;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  auto bound=[&](){auto us=std::chrono::duration_cast<std::chrono::microseconds>(deadline-std::chrono::steady_clock::now()).count();if(us<=0){errno=ETIMEDOUT;return false;}timeval t{static_cast<time_t>(us/1000000),static_cast<suseconds_t>(us%1000000)};return !setsockopt(sock.fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t)&&!setsockopt(sock.fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);};
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port_);
  if(!bound()||connect(sock.fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr))return false;
  const auto command="FEED1\t"+verb+"\t"+hex(path)+"\t"+std::to_string(limit)+"\n";std::size_t sent=0;
  while(sent<command.size()){if(!bound())return false;auto n=send(sock.fd,command.data()+sent,command.size()-sent,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)return false;sent+=std::size_t(n);}
  std::string header;char c=0;
  while(header.size()<80){if(!bound())return false;const auto n=recv(sock.fd,&c,1,0);if(n<0&&errno==EINTR)continue;if(n!=1)return false;if(c=='\n')break;header+=c;}
  if(c!='\n'||header.rfind("OK\t",0)!=0){errno=EPROTO;return false;}
  const auto digits=header.substr(3);if(digits.empty()||digits.find_first_not_of("0123456789")!=std::string::npos){errno=EPROTO;return false;}
  errno=0;char* end=nullptr;const auto size=std::strtoull(digits.c_str(),&end,10);if(errno||!end||*end||size>limit){errno=EFBIG;return false;}
  std::uint64_t remaining=size;char bytes[32768];
  while(remaining){if(!bound())return false;const auto want=std::size_t(remaining<sizeof bytes?remaining:sizeof bytes);const auto n=recv(sock.fd,bytes,want,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;if(!sink(bytes,std::size_t(n)))return false;remaining-=std::uint64_t(n);}
  return true;
 }
 bool text(const std::string& verb,const std::string& path,std::size_t limit,std::string& output)const{std::string result;bool ok=request(verb,path,limit,[&](const char* data,std::size_t size){result.append(data,size);return true;});if(ok)output=std::move(result);return ok;}
};
}
