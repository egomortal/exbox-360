#include "signal_profile.h"
#include "launch_profile.h"
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
extern "C" void av2_boot_log(const char*,const char*,...);
namespace {
constexpr unsigned capacity=2048;
struct Record {uint64_t id,start,end,lr,detail;uint32_t op,object,handle,thread,pc,a,b,c,result,auxiliary,completed;};
struct Irq {uint64_t entered=0,returned=0,result=0;uint32_t source=0,callback=0,data=0,return_source=0,return_callback=0,return_data=0;};
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER,dump_lock=PTHREAD_MUTEX_INITIALIZER;
Record rows[capacity]{},export_rows[capacity]{};unsigned used=0,dumps=0;
uint64_t attempts=0,completed=0,overflow=0,unresolved=0;Irq irq;
uint64_t now_ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ull+uint64_t(t.tv_nsec);}
const char* output_path(){
#if defined(AV2_SIGNAL_TESTING)
 if(const char*p=std::getenv("A1_SIGNAL_EVIDENCE_PATH"))return p;
#endif
 return "/data/nxe/avatar-editor/p3-signal-evidence.tsv";
}
}
extern "C" uint64_t av2_signal_begin(uint32_t op,uint32_t object,uint32_t handle,uint32_t thread,uint64_t lr,uint32_t pc,uint32_t a,uint32_t b,uint32_t c,uint64_t detail,uint64_t start) noexcept {
 if(!start||object!=av2_signal_target)return 0;
 pthread_mutex_lock(&lock);++attempts;
 if(used==capacity){++overflow;pthread_mutex_unlock(&lock);return UINT64_MAX;}
 auto& row=rows[used++];row={attempts,start,0,lr,detail,op,object,handle,thread,pc,a,b,c,0,0,0};
 const uint64_t token=used;pthread_mutex_unlock(&lock);return token;
}
extern "C" void av2_signal_end(uint64_t token,uint32_t result,uint32_t auxiliary) noexcept {
 if(!token)return;const auto now=now_ns();pthread_mutex_lock(&lock);++completed;
 if(token<=capacity){auto& row=rows[token-1];row.end=now;row.result=result;row.auxiliary=auxiliary;row.completed=1;}
 pthread_mutex_unlock(&lock);
}
extern "C" void av2_signal_unresolved_handle() noexcept {if(!av2_launch_begin())return;pthread_mutex_lock(&lock);++unresolved;pthread_mutex_unlock(&lock);}
extern "C" void av2_irq_enter(uint32_t source,uint32_t callback,uint32_t data) noexcept {pthread_mutex_lock(&lock);++irq.entered;irq.source=source;irq.callback=callback;irq.data=data;pthread_mutex_unlock(&lock);}
extern "C" void av2_irq_return(uint32_t source,uint32_t callback,uint32_t data,uint64_t result) noexcept {pthread_mutex_lock(&lock);++irq.returned;irq.return_source=source;irq.return_callback=callback;irq.return_data=data;irq.result=result;pthread_mutex_unlock(&lock);}
extern "C" void av2_signal_heartbeat(char*out,size_t size) noexcept {
 if(!out||!size)return;pthread_mutex_lock(&lock);const auto i=irq;const auto a=attempts,c=completed,d=overflow,u=unresolved;const auto n=used;pthread_mutex_unlock(&lock);
 std::snprintf(out,size,"irq-in=%llu irq-out=%llu irq-source=%u irq-cb=%08x irq-data=%08x irq-return=%016llx irq-ret-source=%u irq-ret-cb=%08x irq-ret-data=%08x signal-attempts=%llu signal-completed=%llu signal-stored=%u signal-overflow=%llu signal-unresolved-handle=%llu signal-open=%u",(unsigned long long)i.entered,(unsigned long long)i.returned,i.source,i.callback,i.data,(unsigned long long)i.result,i.return_source,i.return_callback,i.return_data,(unsigned long long)a,(unsigned long long)c,n,(unsigned long long)d,(unsigned long long)u,unsigned(av2_launch_begin()!=0));
}
extern "C" void av2_signal_dump(const char*window) noexcept {
 if(!window||(std::strcmp(window,"first-60s")&&std::strcmp(window,"exit")))return;
 pthread_mutex_lock(&dump_lock);
 if(dumps==2){pthread_mutex_unlock(&dump_lock);return;}++dumps;
 pthread_mutex_lock(&lock);const auto n=used;const auto a=attempts,c=completed,d=overflow,u=unresolved;std::memcpy(export_rows,rows,n*sizeof(Record));pthread_mutex_unlock(&lock);
 const char*path=output_path();char temporary[544];const int len=std::snprintf(temporary,sizeof(temporary),"%s.tmp",path);bool ok=len>0&&size_t(len)<sizeof(temporary);
 FILE*f=ok?std::fopen(temporary,"w"):nullptr;ok=f!=nullptr;
 if(f){
  ok=std::fprintf(f,"version=1 target=%08x window=%s capacity=%u stored=%u attempts=%llu completed=%llu active=%llu overflow=%llu unresolved_handles=%llu pc_zero=unverified\n",av2_signal_target,window,capacity,n,(unsigned long long)a,(unsigned long long)c,(unsigned long long)(a-c),(unsigned long long)d,(unsigned long long)u)>0;
  ok=std::fprintf(f,"id\top\tobject\thandle\tthread\traw_lr\tverified_pc\ta\tb\tc\tdetail\tstart_ns\tend_ns\tcompleted\tresult\tauxiliary\n")>0&&ok;
  for(unsigned j=0;j<n&&ok;++j){const auto&v=export_rows[j];ok=std::fprintf(f,"%llu\t%u\t%08x\t%08x\t%08x\t%016llx\t%08x\t%08x\t%08x\t%08x\t%016llx\t%llu\t%llu\t%u\t%08x\t%08x\n",(unsigned long long)v.id,v.op,v.object,v.handle,v.thread,(unsigned long long)v.lr,v.pc,v.a,v.b,v.c,(unsigned long long)v.detail,(unsigned long long)v.start,(unsigned long long)v.end,v.completed,v.result,v.auxiliary)>0;}
  if(std::fclose(f))ok=false;
 }
 if(ok&&std::rename(temporary,path))ok=false;
 pthread_mutex_unlock(&dump_lock);
 av2_boot_log(ok?"summary":"error","signal-evidence window=%s stored=%u attempts=%llu completed=%llu active=%llu overflow=%llu unresolved-handles=%llu export-ok=%u path=%s",window,n,(unsigned long long)a,(unsigned long long)c,(unsigned long long)(a-c),(unsigned long long)d,(unsigned long long)u,unsigned(ok),path);
}
#if defined(AV2_SIGNAL_TESTING)
extern "C" void av2_signal_test_reset(){pthread_mutex_lock(&lock);used=dumps=0;attempts=completed=overflow=unresolved=0;irq={};std::memset(rows,0,sizeof(rows));pthread_mutex_unlock(&lock);}
#endif
