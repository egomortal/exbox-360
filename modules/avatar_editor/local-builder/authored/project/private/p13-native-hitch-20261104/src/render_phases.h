#pragma once
#include <cstdint>
namespace av2::render_phase {
enum Phase:unsigned {Other,QueueLock,EmptyQueue,Backend,Fence,Submit,Acquire,Present,DecodeReady,PipelineReady,DriverShader,DriverPipeline,Prepare,RecordClear,RecordResolve,RecordSwap,RecordReadback,RecordDraw,RecordRelease,RecordTextureUpload,RecordWriteback,Count};
struct Snapshot{uint64_t ns[Count]{};uint64_t calls[Count]{};uint64_t bad_clock=0;};
class Collector {
 uint64_t last_=0;Phase phase_=Other;Snapshot sum_{};
 public:
 void charge(uint64_t now)noexcept {if(last_&&now>=last_)sum_.ns[phase_]+=now-last_;else if(last_)++sum_.bad_clock;last_=now;}
 Phase enter(Phase next,uint64_t now,bool count=true)noexcept {charge(now);if(count)++sum_.calls[next];auto old=phase_;phase_=next;return old;}
 Snapshot snapshot(uint64_t now)noexcept {charge(now);auto result=sum_;sum_=Snapshot{};return result;}
};
void enable(uint64_t start,uint64_t end)noexcept;
void disable()noexcept;
bool active()noexcept;
uint64_t now()noexcept;
Collector& local()noexcept;
Snapshot snapshot(uint64_t time)noexcept;
struct Scope {
 bool on=false;Phase previous=Other;
 explicit Scope(Phase phase)noexcept {if(active()){const auto time=now();if(active()){previous=local().enter(phase,time);on=true;}}}
 ~Scope(){if(on&&active())local().enter(previous,now(),false);}
 Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
};
template<class F>decltype(auto) call(Phase phase,F&&function){Scope scope(phase);return function();}
}
#define AV2_RENDER_PHASE(category,expression) av2::render_phase::call(av2::render_phase::category,[&]()->decltype(auto){return (expression);})
