#include "signal_profile.h"
#include "wait_profile.h"
#include "launch_profile.h"
#include <atomic>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <time.h>
extern "C" void av2_boot_log(const char*,const char*,...);
#ifdef AV2_LAUNCH_TEST
extern uint64_t av2_launch_test_now_ns() noexcept;
#endif
namespace {
constexpr uint64_t limit_ns=60000000000ULL,slow_ns=100000ULL;
std::atomic<uint64_t> origin{0};std::atomic<bool> done{false},final_dumped{false};
pthread_mutex_t profile_lock=PTHREAD_MUTEX_INITIALIZER;
struct Row {char name[96]{};uint64_t count=0,total=0,max=0;};
Row rows[256]{};unsigned used=0;uint64_t dropped=0,below_floor=0;
char phases[32][48]{};unsigned phase_count=0;
uint64_t now_ns() noexcept {
#ifdef AV2_LAUNCH_TEST
 return av2_launch_test_now_ns();
#else
 const int saved_errno=errno;timespec t{};const int result=clock_gettime(CLOCK_MONOTONIC,&t);errno=saved_errno;if(result)return 0;
 return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);
#endif
}
void init() noexcept {uint64_t zero=0;origin.compare_exchange_strong(zero,now_ns());}
void add(const char* name,uint64_t elapsed) noexcept {
 if(!name||done.load(std::memory_order_relaxed))return;
 pthread_mutex_lock(&profile_lock);
 if(elapsed<slow_ns){++below_floor;pthread_mutex_unlock(&profile_lock);return;}
 unsigned at=0;while(at<used&&std::strcmp(rows[at].name,name))++at;
 if(at==used){if(used==256){++dropped;pthread_mutex_unlock(&profile_lock);return;}std::strncpy(rows[at].name,name,sizeof(rows[at].name)-1);++used;}
 auto& row=rows[at];++row.count;row.total+=elapsed;if(elapsed>row.max)row.max=elapsed;
 pthread_mutex_unlock(&profile_lock);
}
}
#ifndef AV2_LAUNCH_TEST
// Earliest application constructor priority. OS image loading precedes this.
__attribute__((constructor(101))) static void launch_origin(){init();}
#endif
extern "C" uint64_t av2_launch_begin() noexcept {
 if(done.load(std::memory_order_relaxed))return 0;const auto now=now_ns();const auto start=origin.load();
 if(start&&now>=start&&now-start>limit_ns){done.store(true,std::memory_order_relaxed);return 0;}
 return start&&now>=start?now:0;
}
extern "C" void av2_launch_record(const char* name,uint64_t start) noexcept {
 if(!start||done.load(std::memory_order_relaxed))return;const auto now=now_ns();
 if(now>=origin.load()&&now-origin.load()>limit_ns){done.store(true,std::memory_order_relaxed);return;}
 if(now>=start)add(name,now-start);
}
extern "C" void av2_launch_elapsed(const char* name,uint64_t elapsed) noexcept {
 if(av2_launch_begin())add(name,elapsed);
}
extern "C" void av2_launch_phase(const char* name) noexcept {
 if(!name)return;init();const auto now=now_ns(),start=origin.load();
 pthread_mutex_lock(&profile_lock);for(unsigned i=0;i<phase_count;++i)if(std::strcmp(phases[i],name)==0){pthread_mutex_unlock(&profile_lock);return;}
 if(phase_count==32){pthread_mutex_unlock(&profile_lock);return;}
 std::strncpy(phases[phase_count++],name,47);pthread_mutex_unlock(&profile_lock);
 av2_boot_log("summary","launch phase=%s constructor-ms=%.3f monotonic-ns=%llu",name,double(now>=start?now-start:0)/1e6,(unsigned long long)now);
}
extern "C" void av2_launch_dump(const char* window) noexcept {
 av2_wait_dump(window);
  av2_signal_dump(window);
 Row snapshot[256];unsigned n;uint64_t overflow,fast;
 pthread_mutex_lock(&profile_lock);n=used;std::memcpy(snapshot,rows,n*sizeof(Row));overflow=dropped;fast=below_floor;pthread_mutex_unlock(&profile_lock);
 for(unsigned i=0;i<n;++i)for(unsigned j=i+1;j<n;++j)if(snapshot[j].total>snapshot[i].total){const Row tmp=snapshot[i];snapshot[i]=snapshot[j];snapshot[j]=tmp;}
 av2_boot_log("summary","launch window=%s metric=inclusive-slow-call-total floor-us=100 labels=%u overflow=%llu below-floor=%llu active-calls-omitted=1",window,n,(unsigned long long)overflow,(unsigned long long)fast);
 for(unsigned i=0;i<n&&i<20;++i){const auto& row=snapshot[i];av2_boot_log("summary","launch rank=%u name=%s calls=%llu total-ms=%.3f max-ms=%.3f",i+1,row.name,(unsigned long long)row.count,double(row.total)/1e6,double(row.max)/1e6);}
}
extern "C" void av2_launch_poll() noexcept {
 if(final_dumped.load(std::memory_order_relaxed))return;
 const auto now=now_ns(),start=origin.load();if(!start||now<start||now-start<limit_ns)return;
 done.store(true,std::memory_order_relaxed);if(!final_dumped.exchange(true))av2_launch_dump("first-60s");
}
