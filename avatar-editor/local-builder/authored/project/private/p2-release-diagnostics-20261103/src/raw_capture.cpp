
// SPDX-License-Identifier: GPL-3.0-or-later
#include "raw_capture.h"
#include <atomic>
#include <unwind.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
namespace {
std::atomic<int> output{-1};

std::atomic_flag capturing=ATOMIC_FLAG_INIT;
static_assert(std::atomic<uintptr_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free && std::atomic<int>::is_always_lock_free);
struct Line {
 char data[320];unsigned used=0;
 void text(const char* s){if(!s)return;for(unsigned i=0;i<120 && s[i] && used<sizeof(data)-1;++i)data[used++]=s[i];}
 void hex(uint64_t x){text("0x");for(int i=15;i>=0 && used<sizeof(data)-1;--i)data[used++]="0123456789abcdef"[(x>>(i*4))&15];}
};
bool bytes(int fd,const char* p,size_t n){while(n){auto k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return false;p+=k;n-=size_t(k);}return true;}
void emit(Line& line){if(line.used<sizeof(line.data))line.data[line.used++]='\n';const int fd=output.load(std::memory_order_relaxed);if(fd>=0)(void)bytes(fd,line.data,line.used);}
}
namespace {
struct Trace {unsigned count=0;bool emit_frames=false;};
_Unwind_Reason_Code trace_frame(_Unwind_Context* context,void* opaque){
 auto& trace=*static_cast<Trace*>(opaque);
 if(trace.count>=32)return _URC_END_OF_STACK;
 const auto ip=static_cast<uintptr_t>(_Unwind_GetIP(context));if(!ip)return _URC_END_OF_STACK;
 if(trace.emit_frames){Line line;line.text("backtrace frame=");line.hex(trace.count);line.text(" pc=");line.hex(ip);emit(line);}
 ++trace.count;return _URC_NO_REASON;
}
void trace_stack(bool output_frames){
 Trace trace;trace.emit_frames=output_frames;
 if(output_frames){Line line;line.text("backtrace begin: ordinary-thread unwind; best effort, not signal-safe");emit(line);}
 const auto result=_Unwind_Backtrace(trace_frame,&trace);
 if(output_frames){Line line;line.text("backtrace end count=");line.hex(trace.count);line.text(" unwind-result=");line.hex(static_cast<unsigned>(result));emit(line);}
}
}
extern "C" bool av2_raw_open(const char* current,const char* previous) noexcept {
 // Startup only. Preserve the append-only original even if recovery fails.
 int old=open(current,O_RDONLY);if(old>=0){
  int report=open(previous,O_WRONLY|O_CREAT|O_TRUNC,0644);
  if(report>=0){char buffer[4096];size_t budget=1024*1024;
   const auto end=lseek(old,0,SEEK_END);if(end>0)(void)lseek(old,end>off_t(budget)?end-off_t(budget):0,SEEK_SET);
   while(budget){auto n=read(old,buffer,budget<sizeof(buffer)?budget:sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<=0)break;if(!bytes(report,buffer,size_t(n)))break;budget-=size_t(n);}
   (void)fsync(report);(void)close(report);
  }(void)close(old);
 }
 int fd=open(current,O_WRONLY|O_CREAT|O_APPEND|O_SYNC,0644);output.store(fd,std::memory_order_release);
 if(fd<0)return false;Line line;line.text("session begin pid=");line.hex(uint64_t(getpid()));emit(line);
 // Warm runtime unwind lookup while startup state is healthy. This does not
 // guarantee allocation-free unwinding later after arbitrary corruption.
 trace_stack(false);return true;
}
// Release ABI compatibility; active import observers do not call this function.
extern "C" void av2_raw_import(uint32_t,const char*,uint32_t,uint32_t) noexcept {}
extern "C" void av2_raw_reason(const char* kind,const char* function,uint32_t line_number,uintptr_t pointer) noexcept {
 const int saved=errno;Line line;line.text("reason=");line.text(kind);line.text(" function=");line.text(function);line.text(" line=");line.hex(line_number);line.text(" pointer=");line.hex(pointer);emit(line);errno=saved;
}
extern "C" void av2_raw_abort() noexcept {
 const int saved=errno;if(capturing.test_and_set(std::memory_order_relaxed)){errno=saved;return;}
 Line heading;heading.text("abort capture begin (explicit abort interception; no signal handler)");emit(heading);
 // Release: terminal reason/backtrace retained, per-import history absent.
 trace_stack(true);
 Line end;end.text("abort capture end");emit(end);errno=saved;
}
