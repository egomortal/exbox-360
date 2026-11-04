#include "ticket_file.h"
#include <cassert>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace av2::native;
int main() {
  char temp[]="/tmp/av2-ticket-test-XXXXXX";
  const char* created=::mkdtemp(temp);assert(created);
  const std::filesystem::path root(created),path=root/"return.ticket";
  ReturnTicket source{"0123456789abcdef0123456789abcdef",0,1888,ExitResult::saved,std::string("screen\0state",12)};
  assert(write_return_ticket(path,source)!=TicketWrite::failed);
  struct stat info{};assert(::stat(path.c_str(),&info)==0 && (info.st_mode&0777)==0600);
  ReturnTicket output;
  assert(read_return_ticket(path,output));assert(output.dashboard_state==source.dashboard_state);
  source.revision=17559;source.era=3;
  assert(write_return_ticket(path,source)!=TicketWrite::failed);
  assert(read_return_ticket(path,output) && output.revision==17559);
  source.nonce="invalid";
  assert(write_return_ticket(path,source)==TicketWrite::failed);
  assert(read_return_ticket(path,output) && output.revision==17559);
  std::filesystem::create_symlink(path,root/"link");
  assert(!read_return_ticket(root/"link",output));
  assert(::mkfifo((root/"fifo").c_str(),0600)==0);
  assert(!read_return_ticket(root/"fifo",output));
  assert(!read_return_ticket(root,output));
  assert(!read_return_ticket(root/"absent",output));
  {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<std::string(kMaxDashboardState+257,'x');}
  assert(!read_return_ticket(path,output) && output.revision==17559);
  {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<"invalid";}
  assert(!read_return_ticket(path,output) && output.revision==17559);
  std::filesystem::remove_all(root); // Only this test's exclusive mkdtemp tree.
}
