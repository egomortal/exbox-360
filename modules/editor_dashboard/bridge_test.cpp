#include "bridge.hpp"
#include <cassert>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
using namespace editor_dashboard;
int main(){
 char temp[]="/tmp/exbox-q9-XXXXXX";assert(mkdtemp(temp));const std::filesystem::path data(temp);const std::int64_t now=1700000000;
 dash::Model before;before.era=2;before.dashboard_build=13604;before.channel=dash::Media;before.selected[dash::Media]=3;before.guide=true;before.guide_blade=dash::BladeHome;before.guide_item[dash::BladeHome]=2;before.viewer=dash::Viewer::Profile;before.viewer_item=1;before.system_history={{1,2},{2,3}};
 std::string reason;assert(prepare(data,before,now,reason));av2::native::ReturnTicket ticket;assert(av2::native::read_return_ticket(data/"avatar/editor/launch-ticket.bin",ticket));ticket.result=av2::native::ExitResult::saved;assert(av2::native::write_return_ticket(data/"return-ticket.bin",ticket)!=av2::native::TicketWrite::failed);
 dash::Model after;assert(consume(data,after,now+2,reason));assert(after.era==2&&after.dashboard_build==13604&&after.channel==dash::Media&&after.selected[dash::Media]==3&&after.guide&&after.viewer==dash::Viewer::Profile&&after.viewer_item==1&&after.system_history.size()==2);assert(!consume(data,after,now+3,reason));
 assert(av2::native::write_return_ticket(data/"return-ticket.bin",ticket)!=av2::native::TicketWrite::failed);assert(!consume(data,after,now+4,reason));
 assert(prepare(data,before,now,reason));assert(av2::native::read_return_ticket(data/"avatar/editor/launch-ticket.bin",ticket));assert(av2::native::write_return_ticket(data/"return-ticket.bin",ticket)!=av2::native::TicketWrite::failed);assert(!consume(data,after,now+601,reason));
 assert(prepare(data,before,now,reason));std::ofstream(data/"return-ticket.bin")<<"corrupt";assert(!consume(data,after,now,reason));assert(!std::filesystem::exists(data/"return-ticket.bin"));
 auto folder=data/"avatar/editor";assert(preferred_images(folder,data/"look").fullbody.empty());std::ofstream(folder/"avatar.json")<<"{\"schema\": 1, \"saved_at_unix\": 1700000000, \"fullbody.png\": {\"width\": 150, \"height\": 300}}";
 std::ofstream png(folder/"fullbody.png",std::ios::binary);unsigned char header[26]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,150,0,0,1,44,8,6};png.write(reinterpret_cast<char*>(header),26);png.close();auto images=preferred_images(folder,data/"look");assert(!images.fullbody.empty()&&images.gamerpic.empty());header[25]=2;{std::ofstream legacy(folder/"fullbody.png",std::ios::binary);legacy.write(reinterpret_cast<char*>(header),26);}assert(preferred_images(folder,data/"look").fullbody.empty());header[25]=6;{std::ofstream fixed(folder/"fullbody.png",std::ios::binary);fixed.write(reinterpret_cast<char*>(header),26);}std::ofstream(data/"look")<<"newer";timespec ts[2]={{1800000000,0},{1800000000,0}};assert(!utimensat(AT_FDCWD,(data/"look").c_str(),ts,0));assert(preferred_images(folder,data/"look").fullbody.empty());
 assert(!std::strcmp(launch_command,"LAUNCH\tPPSA99734\n"));
 std::filesystem::remove_all(data);
}
