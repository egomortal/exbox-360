#include "hang_monitor.h"
#include <chrono>
#include <cstring>
namespace av2::diagnostics {
bool parse_gpu_sync(const char* bytes,bool exists,bool& enabled){
 enabled=false;if(!exists)return true;if(!bytes)return false;
 while(*bytes==' '||*bytes=='\r'||*bytes=='\n'||*bytes=='\t')++bytes;
 if(!*bytes){enabled=true;return true;}
 if(*bytes!='0'&&*bytes!='1')return false;
 const bool value=*bytes++=='1';
 while(*bytes==' '||*bytes=='\r'||*bytes=='\n'||*bytes=='\t')++bytes;
 if(*bytes)return false;enabled=value;return true;
}
bool HangMonitor::start(ReadSnapshot read,Report report,RequestStop request,Timing timing){
 if(worker_.joinable()||!read||!report||!request||!timing.poll_ms||!timing.report_ms||!timing.stall_ms)return false;
 stopping_=false;
 try { worker_=std::thread([this,read,report,request,timing]{
  using Clock=std::chrono::steady_clock;
  auto progress=Clock::now(),last_report=progress;auto prior=read().presents;
  while(!stopping_.load(std::memory_order_acquire)){
   std::this_thread::sleep_for(std::chrono::milliseconds(timing.poll_ms));
   if(stopping_.load(std::memory_order_acquire))break;
   const auto current=read();const auto now=Clock::now();
   if(current.fence_state==-999)continue;
   if(current.presents!=prior){prior=current.presents;progress=now;}
   const bool hung=now-progress>=std::chrono::milliseconds(timing.stall_ms);
   if(hung||now-last_report>=std::chrono::milliseconds(timing.report_ms)){
    report(current,hung);last_report=now;
   }
   // Request only. The frontend retains ownership of termination and GPU drain.
   // A timeout is not evidence that GPU resources are safe to release.
   if(hung){request();break;}
  }
 }); }catch(...){return false;}return true;
}
void HangMonitor::stop(){stopping_.store(true,std::memory_order_release);if(worker_.joinable())worker_.join();}
}
