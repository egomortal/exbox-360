#include "hitch_profile.h"
#include "signal_profile.h"
#include "launch_profile.h"
#include "p6_input_latency.h"
extern "C" unsigned long long av2_audio_underruns();
extern "C" unsigned long long av2_audio_failures();
#include "raw_capture.h"
#include "guest_diagnostics.h"
#include "crash_record.h"
#include "boot_log.h"
#include "hang_monitor.h"
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
namespace {
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
int log_fd=-1;
bool initialized=false, sync_gpu=false, trace_pages=false, verbose_logging=false;
std::atomic<uint64_t> suppressed_lines{0};
char stop_phase[32]{};
std::atomic<bool> stop_requested{false};
std::atomic<bool> vfs_trace{true};
std::atomic<int> gpu_result{0};
std::atomic<uint64_t> created_threads{0};
std::atomic<int64_t> direct_bytes{0}, flexible_bytes{0};
volatile sig_atomic_t signal_received=0;
uint64_t last_frame_ns=0, last_heartbeat_frame=0;
const char* const phases[]={"memory","runtime","xex","assets","vulkan","swapchain","shaders","first-frame","frames-300"};
bool valid_phase(const char* value) {
  for(const char* phase:phases)if(std::strcmp(value,phase)==0)return true;
  return false;
}
uint64_t monotonic_ns() {
  timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
  return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);
}
bool write_all(int fd,const char* data,size_t size) {
  while(size) {
    const auto n=write(fd,data,size);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)return false;
    data+=n;size-=size_t(n);
  }
  return true;
}
bool error_text(const char* value) {
  for(const char* token:{"failed","failure","fatal","invalid","exception","fault","missing ","timeout","timed out","refusing","HANG","error"})
    if(std::strstr(value,token))return true;
  return false;
}
bool urgent_line(const char* phase,const char* message) {
  if(std::strcmp(phase,"heartbeat")==0)return false; // Counters such as audio-failures=0 are not error events.
  return error_text(phase)||error_text(message)||std::strcmp(phase,"exit")==0||std::strcmp(phase,"stop")==0||std::strcmp(phase,"teardown")==0;
}
[[maybe_unused]] bool detailed_phase(const char* phase) {
  for(const char* item:{"submit","present","guest-call","memory","thread","watchdog-fence","watchdog-thread","vfs","vfs-open","vfs-read"})
    if(std::strcmp(item,phase)==0)return true;
  return false;
}
bool release_line(const char* phase,const char* message) {
  if(urgent_line(phase,message))return true;
  for(const char* item:{"boot","controls","previous-run","phase","heartbeat","summary","main","runtime","xex","vulkan-early","self-test","budget","asset-root","asset-path","asset-open","vfs-self-test","import","stdio","CRT/start","raw-abort","optional"})
    if(std::strcmp(item,phase)==0)return true;
  return std::strcmp(phase,"interrupt")==0 && std::strstr(message,"registration");
}
void durable_line(const char* phase,const char* message,bool force_flush=false) {
  timespec now{};(void)clock_gettime(CLOCK_REALTIME,&now);
  char line[2048];
  const int n=std::snprintf(line,sizeof(line),"%lld.%09ld [%s] %s\n",static_cast<long long>(now.tv_sec),now.tv_nsec,phase,message);
  const size_t size=n<0?0:(size_t(n)<sizeof(line)?size_t(n):sizeof(line)-1);
  if(log_fd>=0 && write_all(log_fd,line,size)) {
    if(!verbose_logging && !force_flush && !urgent_line(phase,message))return;
    int result;do {result=fsync(log_fd);}while(result<0 && errno==EINTR);
    if(result==0)return;
  }
  (void)write_all(STDERR_FILENO,line,size);
}
void termination_signal(int value){signal_received=value;}
}
extern "C" bool av2_boot_init(const char* directory) {
  pthread_mutex_lock(&lock);
  if(initialized){const bool ok=log_fd>=0;pthread_mutex_unlock(&lock);return ok;}
  initialized=true;
#if defined(__PROSPERO__)
  (void)mkdir("/data/nxe",0755);
#endif
  (void)mkdir(directory,0755);
  char current[1024],one[1024],two[1024],control[1024];
  const int n=std::snprintf(current,sizeof(current),"%s/boot.log",directory);
  if(n<0 || size_t(n)>=sizeof(current)-3){pthread_mutex_unlock(&lock);return false;}
  std::snprintf(one,sizeof(one),"%s/boot.log.1",directory);
  std::snprintf(two,sizeof(two),"%s/boot.log.2",directory);
  // Consume previous-run evidence BEFORE the existing three-log rotation.
  // Reporting is best-effort: diagnostics must never stop normal startup.
  char marker[1024],report[1024];
  const int mn=std::snprintf(marker,sizeof(marker),"%s/crash.signal",directory);
  const int rn=std::snprintf(report,sizeof(report),"%s/previous-run.txt",directory);
  bool report_failed=false;
  if(access(current,F_OK)==0) {
    bool reported=false;
    if(mn>=0 && size_t(mn)<sizeof(marker) && rn>=0 && size_t(rn)<sizeof(report)) {
      try {reported=exbox::diagnostics::previous_report(current,marker,report);}
      catch(...) {reported=false;} // Allocation errors must not prevent boot.
    }
    if(reported) {
      // Only a durable completed report permits consuming the prior marker.
      // If marker removal fails, keep the log too rather than re-pairing it.
      if(unlink(marker)!=0 && errno!=ENOENT)report_failed=true;
    } else report_failed=true;
  }
  if(!report_failed){(void)rename(one,two);(void)rename(current,one);}

  // A pending old marker must never be paired with new phases. Preserve the
  // original pair byte-for-byte and append current activity to a recovery log.
  char recovery[1024];
  const int recovery_n=std::snprintf(recovery,sizeof(recovery),"%s/boot.recovery.log",directory);
  const char* destination=report_failed?recovery:current;
  if(!report_failed || (recovery_n>=0 && size_t(recovery_n)<sizeof(recovery)))
    log_fd=open(destination,O_WRONLY|O_CREAT|O_APPEND,0644);
  std::snprintf(control,sizeof(control),"%s/verbose.txt",directory);
  verbose_logging=access(control,F_OK)==0;
  std::snprintf(control,sizeof(control),"%s/hitch-profile.txt",directory);
  av2_hitch_configure(access(control,F_OK)==0,monotonic_ns());
  last_frame_ns=monotonic_ns();
  const int dir_fd=verbose_logging?open(directory,O_RDONLY):-1;
  if(dir_fd>=0){(void)fsync(dir_fd);(void)close(dir_fd);}
  std::snprintf(control,sizeof(control),"%s/stop-after.txt",directory);
  int fd=open(control,O_RDONLY);
  bool invalid=false;
  if(fd>=0) {
    char value[64]{};const auto count=read(fd,value,sizeof(value)-1);(void)close(fd);
    if(count>0) {
      size_t end=size_t(count);while(end && (value[end-1]=='\n'||value[end-1]=='\r'||value[end-1]==' '))value[--end]=0;
      if(valid_phase(value))std::memcpy(stop_phase,value,std::strlen(value)+1);
      else invalid=true;
    } else invalid=true;
  }
  std::snprintf(control,sizeof(control),"%s/gpu-sync.txt",directory);
  // Explicit 0 disables the old presence-only switch; empty preserves legacy1.
  int sync_fd=open(control,O_RDONLY);char sync_value[64]{};
  bool sync_valid=true;
  if(sync_fd>=0){
    const auto got=read(sync_fd,sync_value,sizeof(sync_value)-1);(void)close(sync_fd);
    sync_valid=got>=0 && got<static_cast<ssize_t>(sizeof(sync_value)-1) &&
      av2::diagnostics::parse_gpu_sync(sync_value,true,sync_gpu);
  }else{
    sync_gpu=false;sync_valid=errno==ENOENT;
  }
  if(!sync_valid)durable_line("controls","invalid/unreadable gpu-sync.txt; sync disabled; continuing");
  std::snprintf(control,sizeof(control),"%s/trace-pages.txt",directory);
  trace_pages=access(control,F_OK)==0;
  if(report_failed)durable_line("previous-run","evidence retry pending; prior log and marker retained; current startup uses separate recovery log");
  durable_line("boot",verbose_logging?"log opened; verbose; append + per-line fsync":"log opened; release; filtered + 1s heartbeat; deferred fsync");
  char settings[128];std::snprintf(settings,sizeof(settings),"stop-after=%s gpu-sync=%d trace-pages=%d",*stop_phase?stop_phase:"none",int(sync_gpu),int(trace_pages));
  durable_line("controls",settings);
  if(invalid){durable_line("controls","invalid stop-after value; refusing startup");stop_requested=true;}
  const bool ok=log_fd>=0;pthread_mutex_unlock(&lock);return ok;
}
extern "C" void av2_boot_log(const char* phase,const char* format,...) {
  char message[1536];va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);
  if(!verbose_logging && !release_line(phase,message)){++suppressed_lines;return;}
  pthread_mutex_lock(&lock);durable_line(phase,message);pthread_mutex_unlock(&lock);
}
extern "C" bool av2_boot_phase(const char* phase) {
  av2_launch_phase(phase);
  if(std::strcmp(phase,"first-frame")==0)av2_launch_dump("before-first-frame");
  if(std::strcmp(phase,"shaders")==0)av2_diag_arm();
  av2_boot_log("phase","%s reached",phase);
  if(std::strcmp(phase,stop_phase)==0)av2_boot_request_stop(phase);
  return av2_boot_should_stop();
}
extern "C" bool av2_boot_should_stop(){return stop_requested.load() || signal_received!=0;}
extern "C" bool av2_boot_gpu_sync(){return sync_gpu;}
extern "C" bool av2_boot_trace_pages(){return trace_pages;}
extern "C" bool av2_boot_vfs_trace(){return verbose_logging && vfs_trace.load();}
extern "C" void av2_boot_vfs_trace_end(){vfs_trace=false;}
extern "C" void av2_boot_request_stop(const char* reason){av2_boot_log("stop","requested: %s",reason);stop_requested=true;}
extern "C" void av2_boot_install_signals() {
  struct sigaction action{};action.sa_handler=termination_signal;sigemptyset(&action.sa_mask);
  for(int signal:{SIGTERM,SIGHUP,SIGINT})if(sigaction(signal,&action,nullptr))av2_boot_log("signals","install %d failed errno=%d",signal,errno);
}
extern "C" void av2_boot_thread_created(int result) {
  const auto count=result==0?++created_threads:created_threads.load();
  av2_boot_log(result?"thread-error":"thread","create result=%d cumulative-created=%llu (not an OS live-thread count)",result,static_cast<unsigned long long>(count));
}
extern "C" void av2_boot_memory_timed(const char* operation,const void* address,size_t bytes,int flags,const char* backing,int result,int64_t delta,uint64_t elapsed_us) {
  av2_launch_elapsed(operation,elapsed_us*1000);
  // Callers report actual charged-byte deltas, including partial failures.
  if(std::strcmp(backing,"direct")==0)direct_bytes+=delta;
  if(std::strcmp(backing,"flexible")==0)flexible_bytes+=delta;
  const size_t pages=bytes?(bytes+(reinterpret_cast<uintptr_t>(address)&0x3fff)+0x3fff)/0x4000:0;
  av2_boot_log(result?"memory-error":"memory","op=%s address=%p bytes=%zu pages=%zu flags=%x backing=%s result=%x elapsed-us=%llu tracked-direct=%lld tracked-flexible=%lld",operation,address,bytes,pages,flags,backing,result,static_cast<unsigned long long>(elapsed_us),static_cast<long long>(direct_bytes.load()),static_cast<long long>(flexible_bytes.load()));
}
extern "C" void av2_boot_memory(const char* operation,const void* address,size_t bytes,int flags,const char* backing,int result,int64_t delta) {
  av2_boot_memory_timed(operation,address,bytes,flags,backing,result,result?0:delta,0);
}
extern "C" void av2_boot_frame(uint64_t frame) {
  const auto now=monotonic_ns();
  av2_input_present(now);
  av2_hitch_frame(frame,now);
  av2_launch_poll();
  if(frame==1)av2_boot_phase("first-frame");
  if(frame==300)av2_boot_phase("frames-300");
  pthread_mutex_lock(&lock);
  if(!last_frame_ns){last_frame_ns=now;last_heartbeat_frame=frame;}
  const auto elapsed=now>=last_frame_ns?now-last_frame_ns:0;
  if(elapsed<1000000000ULL){pthread_mutex_unlock(&lock);return;}
  const auto frames=frame>=last_heartbeat_frame?frame-last_heartbeat_frame:0;
  const double ms=frames?double(elapsed)/double(frames)/1000000.0:0;
  last_frame_ns=now;last_heartbeat_frame=frame;
  av2::input_latency::Snapshot input{};av2_input_latency_snapshot(&input);
  char summary[1536];std::snprintf(summary,sizeof(summary),"frame=%llu interval-frames=%llu average-frame-ms=%.3f created-threads=%llu tracked-direct=%lld tracked-flexible=%lld suppressed-lines=%llu audio-underruns=%llu audio-failures=%llu input-n=%llu input-qn=%llu input-total=%llu input-mean-us=%llu input-p50-us=%llu input-p90-us=%llu input-p99-us=%llu input-max-us=%llu input-dropped=%llu input-clock-failures=%llu",static_cast<unsigned long long>(frame),static_cast<unsigned long long>(frames),ms,static_cast<unsigned long long>(created_threads.load()),static_cast<long long>(direct_bytes.load()),static_cast<long long>(flexible_bytes.load()),static_cast<unsigned long long>(suppressed_lines.load()),av2_audio_underruns(),av2_audio_failures(),static_cast<unsigned long long>(input.interval),static_cast<unsigned long long>(input.quantile_samples),static_cast<unsigned long long>(input.total),static_cast<unsigned long long>(input.mean_ns/1000),static_cast<unsigned long long>(input.p50_ns/1000),static_cast<unsigned long long>(input.p90_ns/1000),static_cast<unsigned long long>(input.p99_ns/1000),static_cast<unsigned long long>(input.max_ns/1000),static_cast<unsigned long long>(input.dropped),static_cast<unsigned long long>(input.clock_failures));
  char signal[512];av2_signal_heartbeat(signal,sizeof(signal));
  const size_t used=std::strlen(summary);std::snprintf(summary+used,sizeof(summary)-used," %s",signal);
  durable_line("heartbeat",summary);
  pthread_mutex_unlock(&lock);
}
#if defined(__PROSPERO__)
extern "C" void av2_install_fatal_diagnostics() noexcept;
__attribute__((constructor)) static void av2_boot_constructor() {
  av2_install_fatal_diagnostics();
  av2_boot_init("/data/nxe/avatar-editor");
  av2_launch_phase("log-open");
  const bool raw_ok=av2_raw_open("/data/nxe/avatar-editor/abort-current.log","/data/nxe/avatar-editor/previous-abort.txt");
  av2_boot_log("raw-abort","preopened=%d append=abort-current.log recovered=previous-abort.txt",int(raw_ok));
  av2_boot_log("CRT/start","constructor reached uid=%d",int(getuid()));
}
#endif

#if defined(__PROSPERO__)
extern "C" int sceKernelConfiguredFlexibleMemorySize(size_t*);
extern "C" int sceKernelAvailableFlexibleMemorySize(size_t*);
extern "C" int sceKernelMapNamedFlexibleMemory(void**,size_t,int,int,const char*);
extern "C" int sceKernelMunmap(void*,size_t);
extern "C" int64_t sceKernelGetDirectMemorySize();
extern "C" int sceKernelAvailableDirectMemorySize(int64_t,int64_t,size_t,int64_t*,size_t*);
extern "C" void av2_boot_startup_probe(const char* assets) {
  av2_boot_log("self-test","uid=%d asset-root=%s",int(getuid()),assets);
  size_t configured=0,available=0;
  int rc=sceKernelConfiguredFlexibleMemorySize(&configured);
  av2_boot_log("budget","configured-flexible rc=%x bytes=%zu",rc,configured);
  rc=sceKernelAvailableFlexibleMemorySize(&available);
  av2_boot_log("budget","available-flexible query rc=%x bytes=%zu (not true remaining budget)",rc,available);
  // Same measured-budget method as Q8, before pad/runtime/GPU startup. All
  // owned probe mappings are released before proceeding, including on failure.
  constexpr size_t chunk=1u<<20,cap=2048;
  static void* mappings[cap]{};
  size_t count=0;
  for(;count<cap;++count) {
    if(av2_boot_should_stop()){rc=-1;break;}
    rc=sceKernelMapNamedFlexibleMemory(&mappings[count],chunk,3,0,"av2-budget-probe");
    av2_boot_memory("probe-map",mappings[count],chunk,0,"flexible",rc,chunk);
    if(rc)break;
  }
  av2_boot_log("budget","true-flexible bytes=%zu capped=%d result=%x",count*chunk,int(count==cap),rc);
  bool cleanup_ok=true;
  while(count) {
    --count;rc=sceKernelMunmap(mappings[count],chunk);
    av2_boot_memory("probe-unmap",mappings[count],chunk,0,"flexible",rc,-int64_t(chunk));
    if(rc)cleanup_ok=false;
    mappings[count]=nullptr;
  }
  if(!cleanup_ok)av2_boot_request_stop("budget-probe cleanup failure");
  const int64_t total=sceKernelGetDirectMemorySize();int64_t start=0;size_t largest=0;
  rc=total>0?sceKernelAvailableDirectMemorySize(0,total,0x10000,&start,&largest):-1;
  av2_boot_log("budget","direct total=%lld largest=%zu start=%lld rc=%x",static_cast<long long>(total),largest,static_cast<long long>(start),rc);
  for(const char* name:{"avatareditor.xex","ae.pack","fonts","closet"}) {
    char path[1024];const int length=std::snprintf(path,sizeof(path),"%s/%s",assets,name);
    if(length<0 || size_t(length)>=sizeof(path)){av2_boot_request_stop("asset path too long");break;}
    struct stat st{};errno=0;rc=stat(path,&st);
    av2_boot_log("asset-path","path=%s stat=%d errno=%d bytes=%lld",path,rc,errno,rc?0:static_cast<long long>(st.st_size));
    if(rc==0 && S_ISREG(st.st_mode)) {
      errno=0;const int fd=open(path,O_RDONLY);
      av2_boot_log("asset-open","path=%s fd=%d errno=%d",path,fd,errno);
      if(fd>=0)(void)close(fd);
    }
  }
}
#else
extern "C" void av2_boot_startup_probe(const char* assets){av2_boot_log("self-test","host asset-root=%s",assets);}
#endif

extern "C" int av2_boot_gpu_result(){return gpu_result.load();}
extern "C" void av2_boot_gpu_fault(int result,const char* operation){
 int expected=0;
 if(result==-4)gpu_result.store(result); // Vulkan device lost supersedes a timeout.
 else (void)gpu_result.compare_exchange_strong(expected,result);
 av2_boot_log("gpu-fault","operation=%s result=%d",operation,result);
 av2_boot_request_stop(operation);
}

extern "C" int dup2(int,int) __attribute__((weak));
extern "C" bool av2_boot_capture_stdio() {
    // Release boot.log must not be bypassed by unfiltered third-party stdio.
    if(!verbose_logging)return false;
    // Native stdio descriptors may differ from 1/2. Capture both the actual
    // FILE descriptors and conventional descriptors into the existing append log.
    // No pipe, background thread, or recursive logger is introduced here.
    if(log_fd<0 || !dup2)return false;
    (void)fflush(stdout); (void)fflush(stderr);
    const int descriptors[]={fileno(stdout),fileno(stderr),STDOUT_FILENO,STDERR_FILENO};
    bool complete=true;
    for(int fd:descriptors) {
        if(fd<0 || fd==log_fd)continue;
        if(dup2(log_fd,fd)<0){av2_boot_log("stdio","redirect failed fd=%d errno=%d (nonfatal)",fd,errno);complete=false;}
    }
    av2_boot_log("stdio","stdout/stderr redirect complete=%d (best-effort)",int(complete));
    return complete;
}

// Fatal diagnostics are ordinary-thread calls, not signal handlers. A failure
// while the regular logger is locked must not recursively wait on that lock.
extern "C" void av2_boot_fatal_log(const char* phase,const char* format,...) {
 // The allocator hook reaches us BEFORE the abort wrapper. Guard formatting
 // itself: a recursive allocation failure must not enter stdio a second time.
 static std::atomic_flag formatting=ATOMIC_FLAG_INIT;
 if(formatting.test_and_set(std::memory_order_relaxed)){
  constexpr char nested[]="[fatal logger reentry/concurrent report unavailable]\n";
  (void)write(STDERR_FILENO,nested,sizeof(nested)-1);return;
 }
 char message[1536];va_list args;va_start(args,format);
 std::vsnprintf(message,sizeof(message),format,args);va_end(args);
 if(pthread_mutex_trylock(&lock)==0){durable_line(phase,message,true);pthread_mutex_unlock(&lock);}
 else {const char prefix[]="[fatal logger busy] ";(void)write_all(STDERR_FILENO,prefix,sizeof(prefix)-1);
  (void)write_all(STDERR_FILENO,message,std::strlen(message));(void)write_all(STDERR_FILENO,"\n",1);}
 formatting.clear(std::memory_order_relaxed);
}
