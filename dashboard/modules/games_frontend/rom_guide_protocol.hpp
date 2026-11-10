// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
namespace exbox_rom_guide {
inline constexpr std::uint32_t protocol_version=1;
enum class Command{Pause,Resume,ExitToDashboard,QueryStatus};
enum class Outcome{New,Duplicate,StaleGeneration,Invalid,AckApplied,AckRejected,AckUncertain};
enum class Ack{Applied,Rejected,Uncertain};
struct Request{std::uint32_t version=protocol_version;std::string session_id;std::uint64_t generation=0,request_id=0;Command command=Command::QueryStatus;};
struct Reply{std::string session_id;std::uint64_t generation=0,request_id=0;Ack ack=Ack::Uncertain;};

// Logical ledger only. The approved companion owner supplies transport. It
// deliberately contains no endpoint, injection, emulator access or launcher.
class Ledger{
public:
 Outcome submit(const Request&r){if(r.version!=protocol_version||r.session_id.empty()||!r.generation||!r.request_id)return Outcome::Invalid;
  if(session_.empty()||r.session_id!=session_)reset(r.session_id,r.generation);if(r.generation<generation_)return Outcome::StaleGeneration;
  if(r.generation>generation_)reset(r.session_id,r.generation);Key k{r.generation,r.request_id};if(requests_.count(k))return Outcome::Duplicate;
  requests_.emplace(k,Entry{r.command,Ack::Uncertain,false});return Outcome::New;}
 Outcome acknowledge(const Reply&r){if(r.session_id!=session_||r.generation!=generation_)return Outcome::StaleGeneration;
  auto it=requests_.find({r.generation,r.request_id});if(it==requests_.end())return Outcome::Invalid;it->second.ack=r.ack;it->second.has_ack=true;
  return r.ack==Ack::Applied?Outcome::AckApplied:r.ack==Ack::Rejected?Outcome::AckRejected:Outcome::AckUncertain;}
 bool may_retry(const Request&r)const{auto it=requests_.find({r.generation,r.request_id});if(it==requests_.end())return true;
  if(it->second.command==Command::ExitToDashboard&&(!it->second.has_ack||it->second.ack!=Ack::Rejected))return false;
  return it->second.has_ack&&it->second.ack==Ack::Rejected;}
 std::uint64_t generation()const{return generation_;}
private:
 using Key=std::pair<std::uint64_t,std::uint64_t>;struct Entry{Command command;Ack ack;bool has_ack;};
 void reset(std::string s,std::uint64_t g){session_=std::move(s);generation_=g;requests_.clear();}
 std::string session_;std::uint64_t generation_=0;std::map<Key,Entry>requests_;
};
} // namespace exbox_rom_guide
