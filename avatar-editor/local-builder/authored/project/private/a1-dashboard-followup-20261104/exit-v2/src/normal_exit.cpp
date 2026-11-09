#include "normal_exit_api.h"
#include "handoff.hpp"
#include <rex/system/xthread.h>
#include <unistd.h>
namespace {
av2::exit_handoff::Registry<64> registry;
thread_local uint32_t route_hint=0;
std::atomic<bool> frontend_active{false};
}
extern "C" void av2_normal_exit_enable() noexcept {frontend_active.store(true,std::memory_order_release);}
extern "C" bool av2_normal_exit_active() noexcept {return frontend_active.load(std::memory_order_acquire);}
extern "C" void av2_normal_exit_request(uint32_t route) noexcept {
 if(!av2_normal_exit_active()||!rex::system::XThread::IsInThread())return;
 av2_normal_exit_intercept(rex::system::XThread::GetCurrentThread(),route,0,false);
}
extern "C" void av2_normal_exit_route(uint32_t route) noexcept {route_hint=route;}
extern "C" bool av2_normal_exit_intercept(void* opaque,uint32_t route,int status,bool main_only) noexcept {
 auto* thread=static_cast<rex::system::XThread*>(opaque);
 if(!av2_normal_exit_active()||!thread||!rex::system::XThread::IsInThread(thread)||!thread->is_guest_thread()||(main_only&&!thread->main_thread()))return false;
 const uintptr_t identity=reinterpret_cast<uintptr_t>(thread);
 const auto chosen=static_cast<av2::exit_handoff::Route>(route_hint?route_hint:route);
 // Commit is after the last thread/guest/resource read. No locks or owners are
 // acquired here. From publication onward this caller performs no guest work,
 // teardown, APC dispatch, object release, or callbacks; OS replacement reaps it.
 registry.commit(identity,chosen,status);
 for(;;)::sleep(1);
}
extern "C" bool av2_normal_exit_parked(const void* thread) noexcept {return registry.parked(reinterpret_cast<uintptr_t>(thread));}
extern "C" bool av2_normal_exit_requested() noexcept {auto r=registry.request();return r.pending||r.blocked;}
extern "C" bool av2_normal_exit_refused() noexcept {auto r=registry.request();return r.blocked||r.failed;}
extern "C" uint32_t av2_normal_exit_route_value() noexcept {return static_cast<uint32_t>(registry.request().route);}
extern "C" int av2_normal_exit_status() noexcept {return registry.request().status;}

extern "C" const char* av2_normal_exit_route_name() noexcept {switch(av2_normal_exit_route_value()){case 1:return "terminate-title";case 2:return "launch-title";case 3:return "launch-title-ex";case 4:return "prompt-discard";case 5:return "main-exit";case 6:return "main-terminate";case 7:return "start-rejected";default:return "unknown";}}

extern "C" [[noreturn]] void av2_normal_exit_park_unstarted(const void* thread) noexcept {
 // Called only by the retained creation/start gate after its mutex is released,
 // before this OS callback binds TLS or touches guest resources.
 registry.commit(reinterpret_cast<uintptr_t>(thread),av2::exit_handoff::Route::not_started,0);
 for(;;)::sleep(1);
}
