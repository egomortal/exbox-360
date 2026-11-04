#include "ticket_file.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace av2::native {
namespace {
struct Fd {int value=-1;~Fd(){if(value>=0) ::close(value);}};
bool write_all(int fd,std::string_view bytes) {
  std::size_t offset=0;
  while(offset<bytes.size()) {
    const auto count=::write(fd,bytes.data()+offset,bytes.size()-offset);
    if(count<0 && errno==EINTR) continue;
    if(count<=0) return false;
    offset+=static_cast<std::size_t>(count);
  }
  return true;
}
bool read_regular(const std::filesystem::path& path,std::size_t limit,std::string& output) {
  Fd file{::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW)};
  if(file.value<0) return false;
  struct stat info{};
  if(::fstat(file.value,&info)!=0) return false;
  if(!S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>static_cast<off_t>(limit)){errno=EINVAL;return false;}
  std::string bytes(static_cast<std::size_t>(info.st_size),'\0');
  std::size_t offset=0;
  while(offset<bytes.size()) {
    const auto count=::read(file.value,bytes.data()+offset,bytes.size()-offset);
    if(count<0 && errno==EINTR) continue;
    if(count<=0){if(count==0)errno=EIO;return false;}
    offset+=static_cast<std::size_t>(count);
  }
  char trailing;ssize_t tail;
  do {tail=::read(file.value,&trailing,1);} while(tail<0 && errno==EINTR);
  if(tail!=0){if(tail>0)errno=EIO;return false;}
  output=std::move(bytes);return true;
}
}
TicketWrite write_atomic_bytes(const std::filesystem::path& path,std::string_view bytes,bool backup) {
  if(path.filename().empty()) return TicketWrite::failed;
  if(backup) {
    std::string previous;
    if(read_regular(path,128u*1024u*1024u,previous)) {
      auto previous_path=path;previous_path+=".bak";
      if(write_atomic_bytes(previous_path,previous,false)==TicketWrite::failed) return TicketWrite::failed;
    } else if(errno!=ENOENT) return TicketWrite::failed;
  }
  auto parent=path.parent_path();if(parent.empty()) parent=".";
  // Temp and target share a directory/filesystem. The old complete file stays
  // intact until checked write/fsync/close succeeds and rename publishes it.
  const auto pattern=(parent/(path.filename().string()+".tmp-XXXXXX")).string();
  std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
  Fd staged{::mkstemp(name.data())};if(staged.value<0) return TicketWrite::failed;
  const bool flushed=write_all(staged.value,bytes) && ::fsync(staged.value)==0;
  const int closed=::close(staged.value);staged.value=-1;
  if(!flushed || closed!=0 || ::rename(name.data(),path.c_str())!=0) {
    ::unlink(name.data());return TicketWrite::failed;
  }
  Fd directory{::open(parent.c_str(),O_RDONLY|O_DIRECTORY)};
  return directory.value>=0 && ::fsync(directory.value)==0?TicketWrite::durable:TicketWrite::published;
}
TicketWrite write_return_ticket(const std::filesystem::path& path,const ReturnTicket& ticket) {
  std::string bytes;
  if(!encode_return_ticket(ticket,bytes)) return TicketWrite::failed;
  return write_atomic_bytes(path,bytes,false);
}
bool read_return_ticket(const std::filesystem::path& path,ReturnTicket& output) {
  std::string bytes;
  return read_regular(path,kMaxDashboardState+256,bytes) && decode_return_ticket(bytes,output);
}
}
