#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "wait_metadata.h"
#include "render_phases.h"
namespace av2::hitch {
// Explicit diagnostic window, timestamps from one monotonic clock. Caller serializes.
constexpr uint64_t duration_ns=120000000000ULL, slow_frame_ns=20000000ULL;
constexpr uint64_t detail_period_ns=10000000000ULL, call_floor_ns=100000ULL;
constexpr unsigned capacity=512, top_count=3;
struct Call { uint64_t start=0,end=0; uint32_t thread=0,object=0; char name[80]{}; Av2HitchWait wait{}; };
struct Match { Call call{}; uint64_t overlap=0; };
struct Detail { uint64_t frame=0,start=0,end=0,bucket=0; Match top[top_count]{}; unsigned count=0; av2::render_phase::Snapshot phases{}; };
struct Stats { uint64_t frames=0,hitches=0,details=0,suppressed=0,evicted=0,expired=0,
  fast_calls=0,invalid=0,late_completed=0,accepted=0,max_frame_ns=0,pending=0,replaced=0; };
class Collector {
 Call calls_[capacity]{}; unsigned used_=0; uint64_t origin_=0,last_frame_=0,last_id_=0;
 uint64_t pending_slot_=0; Detail pending_{}; bool enabled_=false,has_frame_=false,has_pending_=false;
 Stats stats_{};
 public:
 void start(bool enabled,uint64_t now) noexcept { used_=0;last_frame_=0;last_id_=0;pending_slot_=0;pending_=Detail{};has_pending_=false;has_frame_=false;stats_=Stats{};enabled_=enabled;origin_=now; }
 bool active(uint64_t now)const noexcept {return enabled_&&now>=origin_&&now-origin_<duration_ns;}
 const Stats& stats()const noexcept{return stats_;}
 void complete(const char* name,uint64_t start,uint64_t end,uint32_t thread=0,uint32_t object=0,const Av2HitchWait* wait=nullptr)noexcept {
  if(!enabled_)return;
  if(!name||end<start||start<origin_){++stats_.invalid;return;}
  if(!active(end))return;
  if(end-start<call_floor_ns){++stats_.fast_calls;return;}
  // A completion after a boundary cannot retroactively explain an already reported frame.
  if(has_frame_&&start<last_frame_)++stats_.late_completed;
  // Retire records that cannot overlap any future frame. Preserve unfinished-frame intervals.
  unsigned keep=0;for(unsigned i=0;i<used_;++i){if(has_frame_&&calls_[i].end<=last_frame_){++stats_.expired;}else calls_[keep++]=calls_[i];}used_=keep;
  if(used_==capacity){ // Evict earliest completion, not insertion-order assumption across threads.
   unsigned oldest=0;for(unsigned i=1;i<used_;++i)if(calls_[i].end<calls_[oldest].end)oldest=i;
   calls_[oldest]=calls_[--used_];++stats_.evicted;
  }
  auto& c=calls_[used_++];c=Call{};c.start=start;c.end=end;c.thread=thread;c.object=object;
  if(wait)c.wait=*wait;std::strncpy(c.name,name,sizeof(c.name)-1);++stats_.accepted;
 }
 bool finish(Detail& detail)noexcept {
  enabled_=false;detail=Detail{};if(!has_pending_)return false;
  detail=pending_;has_pending_=false;stats_.pending=0;++stats_.details;return true;
 }
 bool frame(uint64_t id,uint64_t now,Detail& detail,const av2::render_phase::Snapshot& phases={})noexcept {
  detail=Detail{};if(!active(now))return false;
  if(!has_frame_){has_frame_=true;last_frame_=now;last_id_=id;return false;}
  if(now<last_frame_||id<=last_id_){++stats_.invalid;return false;}
  const auto previous=last_frame_;last_frame_=now;last_id_=id;++stats_.frames;
  const auto elapsed=now-previous;stats_.max_frame_ns=std::max(stats_.max_frame_ns,elapsed);
  const auto slot=(now-origin_)/detail_period_ns;bool emit=false;
  // A quiet frame can close a prior bucket. Skipped buckets produce no invented records.
  if(has_pending_&&slot!=pending_slot_){detail=pending_;has_pending_=false;stats_.pending=0;++stats_.details;emit=true;}
  if(elapsed<=slow_frame_ns)return emit;
  ++stats_.hitches;
  if(has_pending_){++stats_.suppressed;if(elapsed<=pending_.end-pending_.start)return emit;++stats_.replaced;}
  Detail candidate;candidate.phases=phases;candidate.frame=id;candidate.start=previous;candidate.end=now;candidate.bucket=slot;
  // Snapshot matches NOW, before future completions retire these intervals.
  for(unsigned i=0;i<used_;++i){const auto& c=calls_[i];
   if(c.end>now||c.end<=previous||c.start>=now)continue;
   const auto overlap=std::min(c.end,now)-std::max(c.start,previous);
   unsigned at=0;while(at<candidate.count&&candidate.top[at].overlap>=overlap)++at;
   if(at>=top_count)continue;
   for(unsigned j=std::min(candidate.count,top_count-1);j>at;--j)candidate.top[j]=candidate.top[j-1];
   candidate.top[at]={c,overlap};candidate.count=std::min(candidate.count+1,top_count);
  }
  pending_=candidate;pending_slot_=slot;has_pending_=true;stats_.pending=1;return emit;
 }
};
// One compact summary line, never per-call I/O; format only after releasing collector lock.
inline void format(const Detail& d,const Stats& s,char* out,size_t size)noexcept {
 if(!size)return;
 int n=std::snprintf(out,size,"hitch-worst bucket=%llu frame=%llu ms=%.3f start-ns=%llu end-ns=%llu completed-only=1 inclusive-overlap=1 active-omitted=1 hitches=%llu suppressed=%llu evicted=%llu late=%llu",
 (unsigned long long)d.bucket,(unsigned long long)d.frame,double(d.end-d.start)/1e6,(unsigned long long)d.start,(unsigned long long)d.end,(unsigned long long)s.hitches,(unsigned long long)s.suppressed,(unsigned long long)s.evicted,(unsigned long long)s.late_completed);
 size_t used=n>0?std::min(size_t(n),size-1):0;
 for(unsigned i=0;i<std::min(d.count,1u)&&used<size-1;++i){const auto&m=d.top[i];n=std::snprintf(out+used,size-used," top%u=%s/thread:%08x/object:%08x/ov-us:%llu/dur-us:%llu",i+1,m.call.name,m.call.thread,m.call.object,(unsigned long long)(m.overlap/1000),(unsigned long long)((m.call.end-m.call.start)/1000));if(n>0)used+=std::min(size_t(n),size-used-1);
  const auto&w=m.call.wait;if(w.valid&&used<size-1){n=std::snprintf(out+used,size-used,"/lr:%016llx/pc:%08x/os:%llu/known:%u/guest:%u/main:%u/entry:%08x/startup:%08x/name:unknown/timed:%u/timeout:%016llx/result:%08x/reason:%u/mode:%u/alertable:%u",(unsigned long long)w.lr,w.pc,(unsigned long long)w.os_tid,unsigned(w.thread_known),unsigned(w.guest),unsigned(w.main),w.entry,w.startup,unsigned(w.timed),(unsigned long long)w.timeout,w.result,w.reason,w.mode,w.alertable);if(n>0)used+=std::min(size_t(n),size-used-1);}
 }
 n=std::snprintf(out+used,size-used," reported-ranks=1 phases-ns/calls-hex=");if(n>0)used+=std::min(size_t(n),size-used-1);
 for(unsigned i=0;i<av2::render_phase::Count&&used<size-1;++i){n=std::snprintf(out+used,size-used,"%s%llu/%llx",i?",":"",(unsigned long long)d.phases.ns[i],(unsigned long long)d.phases.calls[i]);if(n>0)used+=std::min(size_t(n),size-used-1);}
 if(used<size-1)std::snprintf(out+used,size-used," phase-bad=%llu",(unsigned long long)d.phases.bad_clock);
}
}
