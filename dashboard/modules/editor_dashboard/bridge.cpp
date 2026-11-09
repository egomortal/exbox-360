#include "bridge.hpp"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <type_traits>
#include <sys/stat.h>
#include <unistd.h>
namespace editor_dashboard {
namespace {
bool integer(std::istream& in,long long& n){return bool(in>>n)&&n>=-1&&n<=1000000;}
std::int64_t modified(const std::filesystem::path& p){struct stat st{};return !::stat(p.c_str(),&st)&&S_ISREG(st.st_mode)?st.st_mtime:0;}
}
std::string screen_state(const dash::Model& m,std::int64_t now){
 std::ostringstream out;out<<"EXBOX-SCREEN/1 "<<now<<'\n';
 out<<static_cast<long long>(m.era)<<' ';
 out<<static_cast<long long>(m.dashboard_build)<<' ';
 out<<static_cast<long long>(m.channel)<<' ';
 out<<static_cast<long long>(m.metro_tab)<<' ';
 out<<static_cast<long long>(m.metro_focus)<<' ';
 out<<static_cast<long long>(m.blades_signed_out)<<' ';
 out<<static_cast<long long>(m.blades_page)<<' ';
 out<<static_cast<long long>(m.collection)<<' ';
 out<<static_cast<long long>(m.library_filter)<<' ';
 out<<static_cast<long long>(m.details)<<' ';
 out<<static_cast<long long>(m.guide)<<' ';
 out<<static_cast<long long>(m.guide_host)<<' ';
 out<<static_cast<long long>(m.guide_blade)<<' ';
 out<<static_cast<long long>(m.guide_community_item)<<' ';
 out<<static_cast<long long>(m.controller_map)<<' ';
 out<<static_cast<long long>(m.viewer)<<' ';
 out<<static_cast<long long>(m.viewer_item)<<' ';
 out<<static_cast<long long>(m.viewer_full)<<' ';
 out<<static_cast<long long>(m.avatar_category)<<' ';
 out<<static_cast<long long>(m.theme_previous_viewer)<<' ';
 out<<static_cast<long long>(m.theme_previous_item)<<' ';
 out<<static_cast<long long>(m.theme_previous_full)<<' ';
 out<<static_cast<long long>(m.theme_previous_guide)<<' ';
 out<<static_cast<long long>(m.achievement_previous_viewer)<<' ';
 out<<static_cast<long long>(m.achievement_previous_item)<<' ';
 out<<static_cast<long long>(m.achievement_previous_full)<<' ';
 out<<static_cast<long long>(m.achievement_list_item)<<' ';
 out<<static_cast<long long>(m.picker_family)<<' ';
 out<<static_cast<long long>(m.picker_return_item)<<' ';
 out<<static_cast<long long>(m.picker_previews)<<' ';
 out<<static_cast<long long>(m.picker_previous_viewer)<<' ';
 out<<static_cast<long long>(m.picker_previous_item)<<' ';
 out<<static_cast<long long>(m.picker_previous_full)<<' ';
 out<<static_cast<long long>(m.picker_return_host)<<' ';
 out<<static_cast<long long>(m.ad_return_host)<<' ';
 out<<static_cast<long long>(m.ad_return_item)<<' ';
 out<<static_cast<long long>(m.ad_previous_viewer)<<' ';
 out<<static_cast<long long>(m.ad_previous_item)<<' ';
 out<<static_cast<long long>(m.ad_previous_full)<<' ';
 out<<static_cast<long long>(m.system_page)<<' ';
 out<<static_cast<long long>(m.storage_device)<<' ';
 out<<static_cast<long long>(m.storage_category)<<' ';
 out<<static_cast<long long>(m.storage_item)<<' ';
 out<<static_cast<long long>(m.network_from_blades)<<' ';
 out<<static_cast<long long>(m.music_view)<<' ';
 out<<static_cast<long long>(m.music_group)<<' ';
 out<<static_cast<long long>(m.music_page)<<' ';
 out<<static_cast<long long>(m.music_library_item)<<' ';
 out<<static_cast<long long>(m.music_cover_from)<<' ';
 out<<static_cast<long long>(m.music_target)<<' ';
 out<<static_cast<long long>(m.video_order)<<' ';
 out<<static_cast<long long>(m.video_start_item)<<' ';
 out<<static_cast<long long>(m.picture_order)<<' ';
 out<<static_cast<long long>(m.picture_group)<<' ';
 for(auto value:m.selected)out<<value<<' ';
 for(auto value:m.metro_selected)out<<value<<' ';
 for(auto value:m.guide_item)out<<value<<' ';
 for(auto value:m.blades_page_return)out<<value<<' ';
 out<<std::quoted(m.details_title)<<' ';
 out<<std::quoted(m.details_text)<<' ';
 out<<std::quoted(m.achievement_game)<<' ';
 out<<std::quoted(m.video_pending)<<' ';
 out<<m.info<<' '<<m.info_item<<' '<<std::quoted(m.info_target.key)<<' '<<std::quoted(m.info_target.title)<<' ';
 out<<m.system_history.size()<<' ';for(const auto& entry:m.system_history)out<<entry.page<<' '<<entry.item<<' ';
 return out.str();
}
bool restore_state(const std::string& bytes,dash::Model& target,std::int64_t now){
 if(bytes.size()>av2::native::kMaxDashboardState)return false;
 std::istringstream in(bytes);std::string magic;std::int64_t created=0;long long n=0;
 if(!(in>>magic>>created)||magic!="EXBOX-SCREEN/1"||created<0||now<0||created>now||now-created>600)return false;
 dash::Model m=target;
 if(!integer(in,n))return false;
 m.era=static_cast<decltype(m.era)>(n);
 if(!integer(in,n))return false;
 m.dashboard_build=static_cast<decltype(m.dashboard_build)>(n);
 if(!integer(in,n))return false;
 m.channel=static_cast<decltype(m.channel)>(n);
 if(!integer(in,n))return false;
 m.metro_tab=static_cast<decltype(m.metro_tab)>(n);
 if(!integer(in,n))return false;
 m.metro_focus=static_cast<decltype(m.metro_focus)>(n);
 if(!integer(in,n))return false;
 m.blades_signed_out=static_cast<decltype(m.blades_signed_out)>(n);
 if(!integer(in,n))return false;
 m.blades_page=static_cast<decltype(m.blades_page)>(n);
 if(!integer(in,n))return false;
 m.collection=static_cast<decltype(m.collection)>(n);
 if(!integer(in,n))return false;
 m.library_filter=static_cast<decltype(m.library_filter)>(n);
 if(!integer(in,n))return false;
 m.details=static_cast<decltype(m.details)>(n);
 if(!integer(in,n))return false;
 m.guide=static_cast<decltype(m.guide)>(n);
 if(!integer(in,n))return false;
 m.guide_host=static_cast<decltype(m.guide_host)>(n);
 if(!integer(in,n))return false;
 m.guide_blade=static_cast<decltype(m.guide_blade)>(n);
 if(!integer(in,n))return false;
 m.guide_community_item=static_cast<decltype(m.guide_community_item)>(n);
 if(!integer(in,n))return false;
 m.controller_map=static_cast<decltype(m.controller_map)>(n);
 if(!integer(in,n))return false;
 m.viewer=static_cast<decltype(m.viewer)>(n);
 if(!integer(in,n))return false;
 m.viewer_item=static_cast<decltype(m.viewer_item)>(n);
 if(!integer(in,n))return false;
 m.viewer_full=static_cast<decltype(m.viewer_full)>(n);
 if(!integer(in,n))return false;
 m.avatar_category=static_cast<decltype(m.avatar_category)>(n);
 if(!integer(in,n))return false;
 m.theme_previous_viewer=static_cast<decltype(m.theme_previous_viewer)>(n);
 if(!integer(in,n))return false;
 m.theme_previous_item=static_cast<decltype(m.theme_previous_item)>(n);
 if(!integer(in,n))return false;
 m.theme_previous_full=static_cast<decltype(m.theme_previous_full)>(n);
 if(!integer(in,n))return false;
 m.theme_previous_guide=static_cast<decltype(m.theme_previous_guide)>(n);
 if(!integer(in,n))return false;
 m.achievement_previous_viewer=static_cast<decltype(m.achievement_previous_viewer)>(n);
 if(!integer(in,n))return false;
 m.achievement_previous_item=static_cast<decltype(m.achievement_previous_item)>(n);
 if(!integer(in,n))return false;
 m.achievement_previous_full=static_cast<decltype(m.achievement_previous_full)>(n);
 if(!integer(in,n))return false;
 m.achievement_list_item=static_cast<decltype(m.achievement_list_item)>(n);
 if(!integer(in,n))return false;
 m.picker_family=static_cast<decltype(m.picker_family)>(n);
 if(!integer(in,n))return false;
 m.picker_return_item=static_cast<decltype(m.picker_return_item)>(n);
 if(!integer(in,n))return false;
 m.picker_previews=static_cast<decltype(m.picker_previews)>(n);
 if(!integer(in,n))return false;
 m.picker_previous_viewer=static_cast<decltype(m.picker_previous_viewer)>(n);
 if(!integer(in,n))return false;
 m.picker_previous_item=static_cast<decltype(m.picker_previous_item)>(n);
 if(!integer(in,n))return false;
 m.picker_previous_full=static_cast<decltype(m.picker_previous_full)>(n);
 if(!integer(in,n))return false;
 m.picker_return_host=static_cast<decltype(m.picker_return_host)>(n);
 if(!integer(in,n))return false;
 m.ad_return_host=static_cast<decltype(m.ad_return_host)>(n);
 if(!integer(in,n))return false;
 m.ad_return_item=static_cast<decltype(m.ad_return_item)>(n);
 if(!integer(in,n))return false;
 m.ad_previous_viewer=static_cast<decltype(m.ad_previous_viewer)>(n);
 if(!integer(in,n))return false;
 m.ad_previous_item=static_cast<decltype(m.ad_previous_item)>(n);
 if(!integer(in,n))return false;
 m.ad_previous_full=static_cast<decltype(m.ad_previous_full)>(n);
 if(!integer(in,n))return false;
 m.system_page=static_cast<decltype(m.system_page)>(n);
 if(!integer(in,n))return false;
 m.storage_device=static_cast<decltype(m.storage_device)>(n);
 if(!integer(in,n))return false;
 m.storage_category=static_cast<decltype(m.storage_category)>(n);
 if(!integer(in,n))return false;
 m.storage_item=static_cast<decltype(m.storage_item)>(n);
 if(!integer(in,n))return false;
 m.network_from_blades=static_cast<decltype(m.network_from_blades)>(n);
 if(!integer(in,n))return false;
 m.music_view=static_cast<decltype(m.music_view)>(n);
 if(!integer(in,n))return false;
 m.music_group=static_cast<decltype(m.music_group)>(n);
 if(!integer(in,n))return false;
 m.music_page=static_cast<decltype(m.music_page)>(n);
 if(!integer(in,n))return false;
 m.music_library_item=static_cast<decltype(m.music_library_item)>(n);
 if(!integer(in,n))return false;
 m.music_cover_from=static_cast<decltype(m.music_cover_from)>(n);
 if(!integer(in,n))return false;
 m.music_target=static_cast<decltype(m.music_target)>(n);
 if(!integer(in,n))return false;
 m.video_order=static_cast<decltype(m.video_order)>(n);
 if(!integer(in,n))return false;
 m.video_start_item=static_cast<decltype(m.video_start_item)>(n);
 if(!integer(in,n))return false;
 m.picture_order=static_cast<decltype(m.picture_order)>(n);
 if(!integer(in,n))return false;
 m.picture_group=static_cast<decltype(m.picture_group)>(n);
 for(auto& value:m.selected){if(!integer(in,n)||n<0)return false;value=int(n);}
 for(auto& value:m.metro_selected){if(!integer(in,n)||n<0)return false;value=int(n);}
 for(auto& value:m.guide_item){if(!integer(in,n)||n<0)return false;value=int(n);}
 for(auto& value:m.blades_page_return){if(!integer(in,n)||n<0)return false;value=int(n);}
 if(!(in>>std::quoted(m.details_title)))return false;
 if(!(in>>std::quoted(m.details_text)))return false;
 if(!(in>>std::quoted(m.achievement_game)))return false;
 if(!(in>>std::quoted(m.video_pending)))return false;
 if(!(in>>m.info>>m.info_item>>std::quoted(m.info_target.key)>>std::quoted(m.info_target.title))||m.info_item<0)return false;
 if(!integer(in,n)||n<0||n>32)return false;
 m.system_history.clear();
 for(int i=0,count=int(n);i<count;++i){long long page,item;if(!integer(in,page)||!integer(in,item)||page<0||page>dash::Model::SystemWelcome||item<0)return false;m.system_history.push_back({int(page),int(item)});}
 in>>std::ws;if(!in.eof()||m.era<0||m.era>3||m.dashboard_build<1888||m.dashboard_build>99999||m.channel<0||m.channel>=dash::ChannelCount||m.metro_tab<0||m.metro_tab>=8||m.guide_blade<0||m.guide_blade>=dash::BladeCount||int(m.viewer)<0||int(m.viewer)>int(dash::Viewer::SystemSettings)||m.viewer_item<0)return false;
 target=std::move(m);return true;
}
bool prepare(const std::filesystem::path& data,const dash::Model& model,std::int64_t now,std::string& reason){
 std::error_code error;std::filesystem::create_directories(data/"avatar/editor",error);if(error){reason="Cannot create editor folder";return false;}
 // Correlation token, not an authentication secret. Clock + process + counter
 // distinguishes launches; acceptance also requires identical pending state.
 static std::uint64_t serial=0;std::ostringstream nonce;nonce<<std::hex<<std::setfill('0')<<std::setw(16)<<std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())<<std::setw(16)<<(std::uint64_t(getpid())<<32|++serial);
 av2::native::ReturnTicket ticket;ticket.nonce=nonce.str();ticket.era=model.era;ticket.revision=model.dashboard_build;ticket.dashboard_state=screen_state(model,now);
 if(av2::native::write_return_ticket(data/"editor-pending.bin",ticket)==av2::native::TicketWrite::failed||av2::native::write_return_ticket(data/"avatar/editor/launch-ticket.bin",ticket)==av2::native::TicketWrite::failed){reason="Cannot save editor return state";return false;}
 reason.clear();return true;
}
void abandon(const std::filesystem::path& data){
 (void)::rename((data/"editor-pending.bin").c_str(),(data/"editor-pending.failed").c_str());
 (void)::rename((data/"avatar/editor/launch-ticket.bin").c_str(),(data/"avatar/editor/launch-ticket.failed").c_str());
}
bool consume(const std::filesystem::path& data,dash::Model& model,std::int64_t now,std::string& reason){
 const auto input=data/"return-ticket.bin",claim=data/"return-ticket.claimed";
 if(::rename(input.c_str(),claim.c_str())){reason.clear();return false;}
 av2::native::ReturnTicket returned,pending;
 if(!av2::native::read_return_ticket(claim,returned)||!av2::native::read_return_ticket(data/"editor-pending.bin",pending)||returned.nonce!=pending.nonce||returned.era!=pending.era||returned.revision!=pending.revision||returned.dashboard_state!=pending.dashboard_state){reason="Ignored corrupt or unmatched editor return ticket";return false;}
 dash::Model restored=model;
 if(!restore_state(returned.dashboard_state,restored,now)||restored.era!=int(returned.era)||restored.dashboard_build!=int(returned.revision)){reason="Ignored stale or invalid editor return state";return false;}
 // Retire pending state before applying it, so even a copied response cannot
 // replay a successful return. The claimed file remains as diagnostic evidence.
 if(::rename((data/"editor-pending.bin").c_str(),(data/"editor-pending.used").c_str())){reason="Could not retire editor return state";return false;}
 model=std::move(restored);reason="Editor return state restored";return true;
}
Images preferred_images(const std::filesystem::path& folder,const std::filesystem::path& dashboard_look){
 Images out;const auto meta=folder/"avatar.json";const auto stamp=modified(meta);if(!stamp)return out;
 std::ifstream in(meta);char content[8193]{};in.read(content,sizeof content);std::string json(content,std::size_t(in.gcount()));if(json.size()>8192)return out;
 auto number=[](const std::string& text,const std::string& key)->std::int64_t{
  auto at=text.find("\""+key+"\"");if(at==std::string::npos)return -1;at=text.find(':',at+key.size()+2);if(at==std::string::npos)return -1;
  std::istringstream value(text.substr(at+1));std::int64_t n=-1;return value>>n?n:-1;
 };
 if(number(json,"schema")!=1||number(json,"saved_at_unix")<=modified(dashboard_look))return out;
 auto png=[&](const char* name){
  auto at=json.find(std::string("\"")+name+"\"");if(at==std::string::npos)return std::string{};
  auto end=json.find('}',at);if(end==std::string::npos)return std::string{};const auto info=json.substr(at,end-at);
  const auto width=number(info,"width"),height=number(info,"height");if(width<=0||height<=0||width>4096||height>4096)return std::string{};
  auto path=folder/name;std::ifstream image(path,std::ios::binary);unsigned char header[26]{};image.read(reinterpret_cast<char*>(header),sizeof header);
  if(image.gcount()!=sizeof header||std::string(reinterpret_cast<char*>(header),8)!=std::string("\x89PNG\r\n\x1a\n",8)||std::string(reinterpret_cast<char*>(header+12),4)!="IHDR")return std::string{};
  // Older exports discarded alpha and contain an opaque black rectangle.
  // Keep those files intact, but use the existing avatar renderer until a new
  // RGBA save exists. Never infer transparency from the colour black.
  if(std::string(name)=="fullbody.png" && header[25]!=6)return std::string{};
  auto be=[](const unsigned char* p){return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|std::uint32_t(p[2])<<8|p[3];};
  return be(header+16)==width&&be(header+20)==height?path.string():std::string{};
 };
 out.fullbody=png("fullbody.png");out.gamerpic=png("gamerpic.png");
 out.signature=std::to_string(stamp)+":"+std::to_string(modified(folder/"fullbody.png"))+":"+std::to_string(modified(folder/"gamerpic.png"));return out;
}
}
