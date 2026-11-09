#include "return_ticket.h"
#include <charconv>
#include <utility>
namespace av2::native {
namespace {
constexpr std::string_view magic="EXBOX-AV2-RETURN/1\n";
bool valid_nonce(std::string_view value) {
  if (value.size()!=32) return false;
  for (char c:value) if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
  return true;
}
const char* result_name(ExitResult result) {
  switch(result) {
    case ExitResult::saved:return "saved";
    case ExitResult::cancelled:return "cancelled";
    case ExitResult::failed:return "failed";
  }
  return nullptr;
}
bool field(std::string_view& bytes,std::string_view key,std::string_view& value) {
  const auto end=bytes.find('\n');
  if (end==std::string_view::npos || bytes.substr(0,key.size())!=key) return false;
  value=bytes.substr(key.size(),end-key.size());
  bytes.remove_prefix(end+1);
  return true;
}
bool number(std::string_view value,std::uint32_t& output) {
  if(value.empty()) return false;
  const auto parsed=std::from_chars(value.data(),value.data()+value.size(),output);
  return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size();
}
bool valid(const ReturnTicket& ticket) {
  return valid_nonce(ticket.nonce) && ticket.era<=3 && ticket.revision>=1888 &&
         ticket.revision<=99999 && result_name(ticket.result) &&
         !ticket.dashboard_state.empty() && ticket.dashboard_state.size()<=kMaxDashboardState;
}
}
bool encode_return_ticket(const ReturnTicket& ticket,std::string& output) {
  if(!valid(ticket)) return false;
  std::string encoded(magic);
  encoded+="nonce="+ticket.nonce+"\nera="+std::to_string(ticket.era)+
    "\nrevision="+std::to_string(ticket.revision)+"\nresult="+result_name(ticket.result)+
    "\nbytes="+std::to_string(ticket.dashboard_state.size())+"\n\n";
  encoded+=ticket.dashboard_state;
  output=std::move(encoded);
  return true;
}
bool decode_return_ticket(std::string_view bytes,ReturnTicket& output) {
  // Limit work before allocation, including the small fixed-format header.
  if(bytes.size()>kMaxDashboardState+256 || bytes.substr(0,magic.size())!=magic) return false;
  bytes.remove_prefix(magic.size());
  ReturnTicket parsed;
  std::string_view value;
  if(!field(bytes,"nonce=",value) || !valid_nonce(value)) return false;
  parsed.nonce=std::string(value);
  if(!field(bytes,"era=",value) || !number(value,parsed.era)) return false;
  if(!field(bytes,"revision=",value) || !number(value,parsed.revision)) return false;
  if(!field(bytes,"result=",value)) return false;
  if(value=="saved") parsed.result=ExitResult::saved;
  else if(value=="cancelled") parsed.result=ExitResult::cancelled;
  else if(value=="failed") parsed.result=ExitResult::failed;
  else return false;
  std::uint32_t count=0;
  if(!field(bytes,"bytes=",value) || !number(value,count) || bytes.empty() || bytes.front()!='\n') return false;
  bytes.remove_prefix(1);
  if(count!=bytes.size() || count>kMaxDashboardState) return false;
  parsed.dashboard_state=std::string(bytes);
  if(!valid(parsed)) return false;
  output=std::move(parsed);
  return true;
}
}
