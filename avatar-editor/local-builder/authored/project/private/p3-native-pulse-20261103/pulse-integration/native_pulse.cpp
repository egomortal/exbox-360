#include "native_pulse.h"
#include "pulse_feed.h"
#include "vblank_source.hpp"
#include <rex/runtime.h>
#include <rex/system/mmio_handler.h>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <cstring>
extern "C" int av2_owned_videoout_handle();
extern "C" int sceVideoOutWaitVblank(int);
extern "C" void av2_boot_log(const char*,const char*,...);
extern "C" void av2_boot_request_stop(const char*);
namespace {
// The producer owns only Feed and a process-lifetime VideoOut handle, never Runtime.
struct Owner {std::mutex mutex,stop_mutex;std::shared_ptr<av2::pulse::Feed> feed;std::unique_ptr<av2::vblank::Source> source;std::atomic<uint32_t> surface{0};unsigned registrations=0;uint32_t pending_callback=0,pending_data=0;bool registration_seen=false;bool ready=false,joined=false,error_reported=false;};
Owner& owner(){static Owner* const value=new Owner;return *value;}
thread_local bool pulse_scope=false;
uint32_t read_mmio(void*,void* context,uint32_t address){auto& o=*static_cast<Owner*>(context);return address==0x7fc86544?uint32_t(pulse_scope):address==0x7fc86110?o.surface.load():0;}
void write_mmio(void*,void* context,uint32_t address,uint32_t value){if(address==0x7fc86110)static_cast<Owner*>(context)->surface.store(value);else av2_boot_request_stop("unexpected display status write");}
}
extern "C" void av2_native_pulse_scope(bool active){pulse_scope=active;}
extern "C" bool av2_native_pulse_start(rex::Runtime* runtime){
 auto& o=owner();std::lock_guard lock(o.mutex);if(o.source)return o.ready&&!o.joined;
 auto* mmio=rex::runtime::MMIOHandler::global_handler();const int handle=av2_owned_videoout_handle();
 if(!runtime||!mmio||handle<0||mmio->LookupRange(0x7fc86544)||mmio->LookupRange(0x7fc86110))return false;
 // Generated native loads/stores explicitly call CheckLoad/CheckStore. Exact
 // address registration changes no mappings, permissions or other MMIO ranges.
 if(!mmio->RegisterRange(0x7fc86544,0xffffffff,4,&o,read_mmio,write_mmio)||!mmio->RegisterRange(0x7fc86110,0xffffffff,4,&o,read_mmio,write_mmio))return false;
 o.feed=std::make_shared<av2::pulse::Feed>();auto feed=o.feed;
 o.source=std::make_unique<av2::vblank::Source>(*runtime,[](uint32_t){/* Actual guest handler owns its counter. */},[&o,feed]{auto sequence=feed->wait();if(feed->error()&&!o.error_reported){o.error_reported=true;o.source->request_stop();av2_boot_log("display-error","real pulse wait result=%d; no further pulses",feed->error());av2_boot_request_stop("real display pulse wait failed");}return sequence;},[](const char* text){if(std::strstr(text,"bound ")||std::strstr(text,"worker-exit"))av2_boot_log("summary","%s",text);});
 if(o.registration_seen&&!o.source->register_callback(o.pending_callback,o.pending_data)){feed->stop();return false;}
 if(!o.source->start()){feed->stop();return false;}
 try{std::thread([feed,handle]{while(!feed->stopped()){const int result=sceVideoOutWaitVblank(handle);if(!feed->publish(result))break;}}).detach();}
 catch(...){feed->stop();o.source->request_stop();throw;}
 o.ready=true;av2_boot_log("summary","actual display pulse source started; process-owned producer; guest owns vblank counter");return true;
}
extern "C" void av2_native_pulse_register(uint32_t callback,uint32_t data){
 auto& o=owner();std::lock_guard lock(o.mutex);o.registration_seen=true;o.pending_callback=callback;o.pending_data=callback?data:0;
 const bool deferred=!o.source&&!o.joined;const bool accepted=deferred||(o.source&&o.ready&&!o.joined&&o.source->register_callback(callback,data));
 if(o.registrations++<4)av2_boot_log("summary","display callback registration callback=%08x data=%08x accepted=%d deferred=%d",callback,data,int(accepted),int(deferred));
 if(!accepted)av2_boot_request_stop("display callback registration rejected");
}
extern "C" bool av2_native_pulse_stop(){
 auto& o=owner();std::lock_guard stop(o.stop_mutex);av2::vblank::Source* source;
 {std::lock_guard lock(o.mutex);if(!o.source||o.joined)return true;source=o.source.get();o.feed->stop();}
 // Never hold the registration mutex while guest callback completion is awaited.
 if(!source->stop_and_join(std::chrono::milliseconds(2000)))return false;
 {std::lock_guard lock(o.mutex);o.joined=true;o.ready=false;}
 auto s=source->snapshot();av2_boot_log("summary","display source joined admitted=%llu skipped=%llu entered=%llu returned=%llu",(unsigned long long)s.ticks,(unsigned long long)s.skipped,(unsigned long long)s.entered,(unsigned long long)s.returned);return true;
}
