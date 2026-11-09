#include <cstdio>
#include "hitch_profile.h"
#include "hitch_collector.h"
#include <atomic>
#include <cerrno>
#include <pthread.h>
#include <time.h>
extern "C" void av2_boot_log(const char*,const char*,...);
#ifdef AV2_HITCH_TEST
extern uint64_t av2_hitch_test_now() noexcept;
#endif
namespace {
struct PreserveErrno {int saved=errno;~PreserveErrno(){errno=saved;}};
std::atomic<bool> enabled{false},configured{false},reported_end{false},armed{false};std::atomic<uint64_t> expires{0};
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
av2::hitch::Collector collector;
struct FirstWait{uint64_t start=0,end=0;uint32_t thread=0,object=0;Av2HitchWait metadata{};};
FirstWait first_waits[16]{};unsigned first_used=0;uint64_t first_overflow_attempts=0;

uint64_t now_ns()noexcept {
#ifdef AV2_HITCH_TEST
 return av2_hitch_test_now();
#else
 int saved=errno;timespec t{};int result=clock_gettime(CLOCK_MONOTONIC,&t);errno=saved;
 return result?0:uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);
#endif
}
bool live(uint64_t now)noexcept {
 if(!enabled.load(std::memory_order_relaxed))return false;
 if(now>=expires.load(std::memory_order_relaxed)){enabled.store(false,std::memory_order_relaxed);return false;}
 return now!=0;
}
}
// Once at startup before worker creation. Disabled by default; no implicit file polling.
extern "C" void av2_hitch_configure(bool on,uint64_t now)noexcept {
 PreserveErrno preserve;
 enabled.store(false);configured.store(on);reported_end.store(false);armed.store(false);av2::render_phase::disable();pthread_mutex_lock(&mutex);collector.start(false,0);first_used=0;first_overflow_attempts=0;
 expires.store(now<=UINT64_MAX-av2::hitch::duration_ns?now+av2::hitch::duration_ns:now);
 pthread_mutex_unlock(&mutex);
}
extern "C" uint64_t av2_hitch_begin()noexcept {
 PreserveErrno preserve;
 if(!enabled.load(std::memory_order_relaxed))return 0;
 const auto now=now_ns();return live(now)?now:0;
}
extern "C" void av2_hitch_interval(const char* name,uint64_t start,uint64_t end,uint32_t thread,uint32_t object)noexcept {
 PreserveErrno preserve;
 if(!start||!live(end))return;
 pthread_mutex_lock(&mutex);collector.complete(name,start,end,thread,object);pthread_mutex_unlock(&mutex);
}
extern "C" void av2_hitch_record(const char* name,uint64_t start,uint32_t thread,uint32_t object)noexcept {
 PreserveErrno preserve;
 if(!start||!enabled.load(std::memory_order_relaxed))return;
 av2_hitch_interval(name,start,now_ns(),thread,object);
}
extern "C" void av2_hitch_finish(const char* reason)noexcept {
 PreserveErrno preserve;
 if(!configured.load(std::memory_order_relaxed)||!armed.load(std::memory_order_relaxed)||reported_end.exchange(true))return;
 enabled.store(false,std::memory_order_relaxed);av2::render_phase::disable();
 av2::hitch::Detail last;av2::hitch::Stats s;char first_line[1300];
 pthread_mutex_lock(&mutex);const bool emit=collector.finish(last);s=collector.stats();
 unsigned printed=std::min(first_used,2u);int n=std::snprintf(first_line,sizeof(first_line)," first-used=%u first-reported=%u first-omitted=%u first-overflow-attempts=%llu name=unknown",first_used,printed,first_used-printed,(unsigned long long)first_overflow_attempts);
 size_t used=n>0?std::min(size_t(n),sizeof(first_line)-1):0;
 for(unsigned i=0;i<printed;++i){const auto&x=first_waits[i];const auto&w=x.metadata;
 n=std::snprintf(first_line+used,sizeof(first_line)-used," first%u=ea:%08x/thread:%u/os:%llu/known:%u/guest:%u/main:%u/entry:%08x/startup:%08x/lr:%016llx/pc:%08x/timed:%u/timeout:%016llx/result:%08x/reason:%u/mode:%u/alertable:%u/start:%llu/end:%llu",i,x.object,x.thread,(unsigned long long)w.os_tid,unsigned(w.thread_known),unsigned(w.guest),unsigned(w.main),w.entry,w.startup,(unsigned long long)w.lr,w.pc,unsigned(w.timed),(unsigned long long)w.timeout,w.result,w.reason,w.mode,w.alertable,(unsigned long long)x.start,(unsigned long long)x.end);
 if(n>0)used+=std::min(size_t(n),sizeof(first_line)-used-1);
 }
 pthread_mutex_unlock(&mutex);
 if(emit){char line[1984];av2::hitch::format(last,s,line,sizeof(line));av2_boot_log("summary","%s",line);}
 av2_boot_log("summary","hitch-window end=%s frames=%llu hitches=%llu details=%llu suppressed=%llu evicted=%llu late=%llu accepted=%llu fast=%llu rejected=%llu pending=%llu replaced=%llu completed-only=1 active-omitted=1%s",
 reason?reason:"explicit",(unsigned long long)s.frames,(unsigned long long)s.hitches,(unsigned long long)s.details,(unsigned long long)s.suppressed,(unsigned long long)s.evicted,(unsigned long long)s.late_completed,(unsigned long long)s.accepted,(unsigned long long)s.fast_calls,(unsigned long long)s.invalid,(unsigned long long)s.pending,(unsigned long long)s.replaced,first_line);
}
extern "C" void av2_hitch_frame(uint64_t frame,uint64_t now)noexcept {
 PreserveErrno preserve;
 if(!configured.load(std::memory_order_relaxed)||reported_end.load(std::memory_order_relaxed)||!now)return;
 if(!armed.load(std::memory_order_relaxed)) {
  pthread_mutex_lock(&mutex);
  if(!armed.load(std::memory_order_relaxed)&&now<=UINT64_MAX-av2::hitch::duration_ns){
   collector.start(true,now);expires.store(now+av2::hitch::duration_ns);
   av2::render_phase::enable(now,now+av2::hitch::duration_ns);av2::render_phase::snapshot(now);
   enabled.store(true);armed.store(true);
  }
  pthread_mutex_unlock(&mutex);
 }
 if(configured.load(std::memory_order_relaxed)&&now>=expires.load(std::memory_order_relaxed))av2_hitch_finish("120s");
 if(!live(now))return;
 av2::hitch::Detail detail;av2::hitch::Stats stats;
 const auto phases=av2::render_phase::snapshot(now);
 pthread_mutex_lock(&mutex);const bool emit=collector.frame(frame,now,detail,phases);stats=collector.stats();pthread_mutex_unlock(&mutex);
 if(emit){char line[1984];av2::hitch::format(detail,stats,line,sizeof(line));av2_boot_log("summary","%s",line);}
}

extern "C" void av2_hitch_wait(uint64_t start,uint32_t thread,uint32_t object,const Av2HitchWait* metadata)noexcept {
 PreserveErrno preserve;
 if(!start||!enabled.load(std::memory_order_relaxed))return;
 const auto end=now_ns();if(!live(end))return;
 pthread_mutex_lock(&mutex);
 if(enabled.load(std::memory_order_relaxed)&&metadata&&metadata->valid&&(object==0x400050d4u||object==0x9458a4dcu)) {
  bool seen=false;for(unsigned i=0;i<first_used;++i)if(first_waits[i].thread==thread&&first_waits[i].object==object)seen=true;
  if(!seen){if(first_used<16)first_waits[first_used++]={start,end,thread,object,*metadata};else ++first_overflow_attempts;}
 }
 collector.complete("KeWaitForSingleObject/detail",start,end,thread,object,metadata);pthread_mutex_unlock(&mutex);
}

