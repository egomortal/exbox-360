#include "raw_capture.h"
#include "guest_diagnostics.h"
#include "boot_log.h"
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
namespace {
struct Thread {uint32_t id=0;char name[80]{};int state=0;const char* call="none";const char* last="none";uint32_t arg=0;};
std::mutex lock;Thread threads[128];uint64_t overflow=0;
std::atomic<uint64_t> armed{0},dispatches{0},returns{0};std::atomic<uint32_t> registered{0};
uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Thread* find(uint32_t id){if(!id)return nullptr;for(auto& t:threads)if(t.id==id)return &t;for(auto& t:threads)if(!t.id){t.id=id;return &t;}++overflow;return nullptr;}
bool trace_name(const char* n){return std::strstr(n,"Wait")||std::strstr(n,"Event")||std::strstr(n,"Signal")||std::strstr(n,"Semaphore");}
}
extern "C" void av2_diag_arm(){armed=now();}
extern "C" void av2_diag_thread(uint32_t id,const char* name,int state){std::lock_guard g(lock);if(auto* t=find(id)){if(state)t->state=state;if(name)std::snprintf(t->name,sizeof(t->name),"%s",name);}}
Av2ImportObservation::Av2ImportObservation(uint32_t i,const char* n,uint32_t a,uint32_t b):id(i),name(n),previous("none"),previous_arg(0),arg(a),trace(false){
 {std::lock_guard g(lock);if(auto* t=find(id)){previous=t->call;previous_arg=t->arg;t->call=name;t->last=name;t->arg=a;
}}
 auto start=armed.load();trace=start&&now()-start<5000000000ULL&&trace_name(n);
 if(trace)av2_boot_log("guest-call","enter tid=%u call=%s r3=%08x r4=%08x",id,n,a,b);
 if(std::strcmp(n,"__imp__VdSetGraphicsInterruptCallback")==0){registered=a;av2_boot_log("interrupt","registration callback=%08x context=%08x",a,b);}
}
Av2ImportObservation::~Av2ImportObservation(){
 {std::lock_guard g(lock);if(auto* t=find(id)){t->call=previous;t->arg=previous_arg;}}
 if(trace)av2_boot_log("guest-call","return tid=%u call=%s r3-target=%08x",id,name,arg);
}
extern "C" void av2_diag_interrupt(uint32_t address,int entering){if(entering)++dispatches;else ++returns;av2_boot_log("interrupt","%s address=%08x registered=%08x",entering?"dispatch":"return",address,registered.load());}
extern "C" void av2_diag_threads_report(){
 Thread copy[128];uint64_t dropped;
 {std::unique_lock g(lock,std::try_to_lock);if(!g){av2_boot_log("watchdog-thread","snapshot busy");return;}for(unsigned i=0;i<128;++i)copy[i]=threads[i];dropped=overflow;}
 av2_boot_log("watchdog-thread","overflow=%llu registered-interrupt=%08x dispatched=%llu returned=%llu (no synthetic vblank)",(unsigned long long)dropped,registered.load(),(unsigned long long)dispatches.load(),(unsigned long long)returns.load());
 for(auto& t:copy)if(t.id)av2_boot_log("watchdog-thread","id=%u name=%s lifecycle=%d active=%s target-r3=%08x last=%s (active call is not OS scheduler state)",t.id,t.name,t.state,t.call,t.arg,t.last);
}

extern "C" bool av2_diag_trace_window(){auto start=armed.load();return start && now()-start<5000000000ULL;}

// Release variant intentionally contains no per-thread import history.
extern "C" void av2_diag_import_history_report(){}
