// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_record.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
namespace exbox::diagnostics {
namespace {
constexpr std::uint32_t magic=0x45584352;
struct Marker {std::uint32_t magic,version;std::int32_t signal;};
static_assert(sizeof(Marker)==12);
bool write_all(int fd,const void* bytes,std::size_t count) noexcept {
 const char* p=static_cast<const char*>(bytes);
 while(count){const auto n=::write(fd,p,count);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;count-=std::size_t(n);}return true;
}
struct File {int fd;~File(){if(fd>=0)::close(fd);}};
}
bool record_signal(int fd,int signal_number) noexcept {
 // No heap, mutex, stdio, logger, destructor or LoadExec inside this primitive.
 // Caller serializes the terminal signal and owns descriptor lifetime.
 const int saved_errno=errno;
 const Marker marker{magic,1,signal_number};
 const bool ok=fd>=0&&write_all(fd,&marker,sizeof(marker));errno=saved_errno;return ok;
}
bool previous_report(const char* log,const char* marker,const char* report){
 if(!log||!marker||!report||std::strcmp(log,report)==0||std::strcmp(marker,report)==0)return false;
 File input{::open(log,O_RDONLY)};if(input.fd<0)return false;
 struct stat source{},target{},signal_stat{};
 if(::fstat(input.fd,&source)!=0)return false;
 if(::stat(report,&target)==0){
  if(source.st_dev==target.st_dev&&source.st_ino==target.st_ino)return false;
  if(::stat(marker,&signal_stat)==0&&signal_stat.st_dev==target.st_dev&&signal_stat.st_ino==target.st_ino)return false;
 }
 std::deque<std::string> tail;std::string pending,last_phase="unknown";std::size_t total=0;bool line_truncated=false;
 auto accept=[&]{
  const auto at=pending.find("[phase] ");
  if(at!=std::string::npos)last_phase=pending.substr(at+8);
  tail.push_back(pending+(line_truncated?" [line truncated]":""));if(tail.size()>50)tail.pop_front();pending.clear();line_truncated=false;
 };
 char buffer[4096];
 for(;;){const auto n=::read(input.fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0)return false;if(n==0)break;
  total+=std::size_t(n);if(total>8*1024*1024)return false;
  for(ssize_t i=0;i<n;++i){if(buffer[i]=='\n')accept();else if(pending.size()<2048)pending+=buffer[i];else line_truncated=true;}
 }
 const bool partial=!pending.empty()||line_truncated;if(partial)accept();
 std::string signal="unknown";File signal_file{::open(marker,O_RDONLY)};
 if(signal_file.fd>=0){Marker m{};char bytes[sizeof(Marker)+1];std::size_t count=0;
  while(count<sizeof(bytes)){auto n=::read(signal_file.fd,bytes+count,sizeof(bytes)-count);if(n<0&&errno==EINTR)continue;if(n<=0)break;count+=std::size_t(n);}
  if(count==sizeof(Marker)){std::memcpy(&m,bytes,sizeof(m));if(m.magic==magic&&m.version==1&&m.signal>0&&m.signal<128)signal=std::to_string(m.signal);}
 }
 std::string body="Ex-Box previous-run evidence v1\nSignal: "+signal+"\nLast phase: "+last_phase+"\nPartial final log line: "+(partial?"yes":"no")+"\nCause: not inferred\nLast "+std::to_string(tail.size())+" log lines:\n";
 for(const auto& line:tail)body+=line+'\n';
 // Exclusive temporary creation fails closed on a leftover or concurrent writer.
 // Never truncate an existing report until a complete replacement is durable.
 const std::string temporary=std::string(report)+".pending";
 File output{::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600)};if(output.fd<0)return false;
 if(!write_all(output.fd,body.data(),body.size())||::fsync(output.fd)!=0){::unlink(temporary.c_str());return false;}
 if(::rename(temporary.c_str(),report)!=0){::unlink(temporary.c_str());return false;}
 return true;
}
}
