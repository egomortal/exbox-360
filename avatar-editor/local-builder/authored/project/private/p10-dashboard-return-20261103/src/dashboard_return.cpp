#include "dashboard_return.h"
#include "ticket_file.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
namespace av2::native {
namespace {
DashboardContext context;
ExitResult completed=ExitResult::cancelled;
struct Socket {int value=-1;~Socket(){if(value>=0)close(value);}};
bool ready(int fd,short events,std::chrono::steady_clock::time_point deadline) {
  for(;;) {
    const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
    if(left<=0){errno=ETIMEDOUT;return false;}
    pollfd item{fd,events,0};const int result=poll(&item,1,static_cast<int>(left));
    if(result<0 && errno==EINTR) continue;
    if(result<=0){if(result==0)errno=ETIMEDOUT;return false;}
    return (item.revents&(events|POLLHUP|POLLERR))!=0;
  }
}
}
bool consume_dashboard_context(const std::filesystem::path& path,DashboardContext& output) {
  ReturnTicket ticket;
  if(!read_return_ticket(path,ticket)) return false;
  auto consumed=path;consumed+=".consumed";
  // Launcher must atomically replace a fresh envelope before each editor launch.
  // A consumed envelope is never read again as a new launch context.
  if(std::rename(path.c_str(),consumed.c_str())!=0) return false;
  output.ticket=std::move(ticket);output.available=true;return true;
}
bool publish_dashboard_return(const DashboardContext& current,ExitResult result,const std::filesystem::path& path) {
  if(!current.available) return false;
  auto ticket=current.ticket;ticket.result=result;
  return write_return_ticket(path,ticket)!=TicketWrite::failed;
}
bool request_companion_launch(std::uint16_t port,std::chrono::milliseconds timeout) {
  if(timeout.count()<=0 || timeout>std::chrono::seconds(30)){errno=EINVAL;return false;}
  Socket socket{::socket(AF_INET,SOCK_STREAM,0)};if(socket.value<0) return false;

  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  const auto deadline=std::chrono::steady_clock::now()+timeout;
  // Q8d console evidence: F_SETFL is denied, blocking loopback succeeds.
  // Bound every blocking operation by the remainder of one total deadline.
  auto bound=[&](){
    const auto left=std::chrono::duration_cast<std::chrono::microseconds>(deadline-std::chrono::steady_clock::now()).count();
    if(left<=0){errno=ETIMEDOUT;return false;}
    timeval time{static_cast<time_t>(left/1000000),static_cast<suseconds_t>(left%1000000)};
    return !setsockopt(socket.value,SOL_SOCKET,SO_SNDTIMEO,&time,sizeof(time))&&!setsockopt(socket.value,SOL_SOCKET,SO_RCVTIMEO,&time,sizeof(time));
  };
  if(!bound())return false;
  const int connected=connect(socket.value,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  if(connected<0 && errno!=EINPROGRESS) return false;
  if(connected<0) {
    if(!ready(socket.value,POLLOUT,deadline)) return false;
    int error=0;socklen_t size=sizeof(error);
    if(getsockopt(socket.value,SOL_SOCKET,SO_ERROR,&error,&size)<0) return false;
    if(error){errno=error;return false;}
  }
  // P10: existing companion installed-title contract; GET /launch is the legacy payload.
  const std::string request="LAUNCH\tPPSA99735\n";
  std::size_t sent=0;
  while(sent<request.size()) {
    if(!ready(socket.value,POLLOUT,deadline)||!bound()) return false;
    const auto count=send(socket.value,request.data()+sent,request.size()-sent,MSG_NOSIGNAL);
    if(count<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
    if(count<=0) return false;
    sent+=static_cast<std::size_t>(count);
  }
  std::string response;response.reserve(256);
  while(response.size()<4096) {
    if(!ready(socket.value,POLLIN,deadline)||!bound()) return false;
    char bytes[256];const auto count=recv(socket.value,bytes,sizeof(bytes),0);
    if(count<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
    if(count<=0) return false;
    response.append(bytes,static_cast<std::size_t>(count));
    const auto end=response.find('\n');
    if(end==std::string::npos) continue;
    auto line=response.substr(0,end);
    if(!line.empty() && line.back()=='\r')line.pop_back();
    return line=="OK";
  }
  errno=EMSGSIZE;return false;
}
void prepare_dashboard_return() {
  if(!consume_dashboard_context("/data/nxe/avatar/editor/launch-ticket.bin",context))
    std::fputs("AV2 no fresh dashboard launch context; exact-screen ticket unavailable\n",stderr);
}
void set_dashboard_exit_result(ExitResult result){completed=result;}
void return_to_dashboard(int exit_status) noexcept {
  // One replaceable endpoint for the current companion route and future native
  // LoadExec route. Called only at native exit after normal teardown when possible.
  try {
    const auto result=exit_status==0?completed:ExitResult::failed;
    if(context.available && !publish_dashboard_return(context,result,"/data/nxe/return-ticket.bin"))
      std::fprintf(stderr,"AV2 return-ticket write failed errno=%d\n",errno);
    const bool launched=request_companion_launch();
    std::fprintf(stderr,"AV2 companion LAUNCH PPSA99735: %s errno=%d; proceeding to Q4 shell exit\n",launched?"OK accepted":"failed",errno);
  } catch(const std::exception& error) {
    std::fprintf(stderr,"AV2 dashboard return failed: %s; proceeding to Q4 shell exit\n",error.what());
  } catch(...) {std::fputs("AV2 dashboard return failed; proceeding to Q4 shell exit\n",stderr);}
}
}
