#include "signal_profile.h"
#include "native_pulse.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vblank_source.hpp"
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/thread.h>
#include <cmath>
#include <cstdio>
#include <utility>
namespace av2::vblank {
namespace {uint64_t now_ns(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}}
struct Source::Impl {
 rex::Runtime& runtime;CounterSink publish;DisplayWait wait_display;LogSink log;State state;
 rex::system::object_ref<rex::system::XHostThread> worker;
 bool started=false,joined=false;
 Impl(rex::Runtime& r,CounterSink p,DisplayWait w,LogSink l):runtime(r),publish(std::move(p)),wait_display(std::move(w)),log(std::move(l)){}
 void note(const char* event,uint64_t a=0,uint64_t b=0,uint64_t c=0){if(log){char text[256];std::snprintf(text,sizeof(text),"vblank %s a=%llu b=%llu c=%llu",event,(unsigned long long)a,(unsigned long long)b,(unsigned long long)c);log(text);}}
 bool valid(uint32_t address){return !address||(!(address&3)&&runtime.function_dispatcher()->FindCallerModuleBase(address)&&runtime.function_dispatcher()->GetFunction(address));}
 int run(){
  auto* thread=rex::system::XThread::GetCurrentThread();
  if(!thread||!thread->thread_state()||!thread->pcr_ptr()){note("unbound-worker");state.stop();return 1;}
  thread->SetActiveCpu(2);note("bound",thread->thread_id(),thread->pcr_ptr(),thread->active_cpu());
  while(!state.snapshot().stopping){
   const uint64_t sequence=wait_display();
   if(sequence)state.display_pulse(sequence);
   while(auto tick=state.next()){
    publish(tick->counter);
    if(!tick->callback)continue;
    if(!valid(tick->callback)){note("invalid-callback",tick->callback);state.stop();break;}
    // No registration mutex is held across guest code. Preserve caller data
    // and target from the same admitted generation.
    thread->SetActiveCpu(2);
    auto* mem=runtime.memory();auto* pcr=mem->TranslateVirtual<rex::be<uint32_t>*>(thread->pcr_ptr());
    const uint32_t tls=*pcr;uint64_t args[]={0,tick->data};state.entered();
    const auto begin=now_ns();
    if(tick->ordinal<=5||tick->ordinal%120==0)note("entry",tick->callback,tick->counter,tick->generation);
    uint64_t interrupt_result=0;av2_irq_enter(0,tick->callback,tick->data);
    {struct Scope{Scope(){av2_native_pulse_scope(true);}~Scope(){av2_native_pulse_scope(false);}} scope;
    interrupt_result=runtime.function_dispatcher()->ExecuteInterrupt(thread->thread_state(),tick->callback,args,2);}
    // Existing dispatcher failure branch skips its TLS restore. Ensure this
    // worker's own PCR is restored even on that returned failure path.
    *pcr=tls;state.returned();av2_irq_return(0,tick->callback,tick->data,interrupt_result);
    if(tick->ordinal<=5||tick->ordinal%120==0)note("return",tick->counter,now_ns()-begin,0);
   }

  }
  note("worker-exit");return 0;
 }
};
Source::Source(rex::Runtime& runtime,CounterSink sink,DisplayWait wait,LogSink log):impl_(std::make_shared<Impl>(runtime,std::move(sink),std::move(wait),std::move(log))){}
Source::~Source(){
 request_stop();
 // The worker captures shared state, so destruction never frees a live capture.
 // Runtime lifetime remains an explicit owner's responsibility on failed join.
 if(impl_->started&&!impl_->joined)impl_->note("owner-destroyed-before-join");
}
bool Source::start(){
 auto& p=*impl_;if(p.started||!p.publish||!p.wait_display||!p.runtime.kernel_state()||!p.runtime.function_dispatcher())return false;
 if(!p.state.start(0,60000))return false; // State init only; external cadence.
 auto state=impl_;
 p.worker=rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(p.runtime.kernel_state(),128*1024,0,[state](){return state->run();}));
 p.worker->set_name("ExBox Vblank");
 const auto status=p.worker->Create();if(status){p.note("thread-create-failed",status);p.state.stop();p.worker.reset();return false;}
 p.started=true;p.note("display-source-started");return true;
}
bool Source::register_callback(uint32_t callback,uint32_t data){auto& p=*impl_;if(!p.valid(callback)){p.note("reject",callback,data);return false;}const bool accepted=p.state.replace(callback,data);p.note(accepted?"register":"register-stopped",callback,data);return accepted;}
void Source::request_stop(){impl_->state.stop();}
bool Source::stop_and_join(std::chrono::milliseconds timeout){
 auto& p=*impl_;request_stop();if(!p.started||p.joined)return true;
 if(rex::system::XThread::IsInThread(p.worker.get())){p.note("self-join-refused");return false;}
 const auto result=rex::thread::Wait(p.worker->thread(),false,timeout);
 if(result!=rex::thread::WaitResult::kSuccess){p.note("join-pending",uint64_t(result));return false;}
 p.joined=true;p.worker.reset();p.note("joined");return true;
}
Snapshot Source::snapshot()const{return impl_->state.snapshot();}
}
