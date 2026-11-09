#include "gpu_diagnostics.h"
#include "boot_log.h"
#include <mutex>
namespace {
std::mutex mutex;
struct Fence { uintptr_t handle=0; uint64_t submit=0; int stage=0,result=0; };
Fence fences[64];uint64_t presents=0,last_submit=0,overflow=0;int last_stage=0,last_result=0;
// Numeric handles are diagnostic identities only; never dereferenced or queried.
void update(uintptr_t handle,uint64_t submit,int stage,int result){
 if(!handle)return;
 for(auto& f:fences)if(f.handle==handle){if(submit)f.submit=submit;f.stage=stage;f.result=result;if(stage==4)f.handle=0;return;}
 if(stage==4)return;
 for(auto& f:fences)if(!f.handle){f={handle,submit,stage,result};return;}
 ++overflow;
}
}
extern "C" void av2_diag_submit(uint64_t index,uintptr_t fence,int stage,int result){
 std::lock_guard guard(mutex);last_submit=index;last_stage=stage;last_result=result;update(fence,index,stage,result);
}
extern "C" void av2_diag_fence(uintptr_t fence,int stage,int result){std::lock_guard guard(mutex);update(fence,0,stage,result);}
extern "C" void av2_diag_present(int result){if(result==0||result==1000001003){std::lock_guard guard(mutex);++presents;}}
namespace av2::diagnostics {
Snapshot gpu_snapshot(){
 // No GPU calls and no blocking on a producer while diagnosing a stall.
 std::unique_lock guard(mutex,std::try_to_lock);
 if(!guard)return {0,0,-999};
 return {presents,last_submit,last_stage};
}
void report_gpu(Snapshot snapshot,bool hang){
 Fence copy[64];uint64_t dropped;int result;
 {std::unique_lock guard(mutex,std::try_to_lock);
  if(!guard){av2_boot_log("watchdog","snapshot busy; HANG=%d",int(hang));return;}
  for(unsigned i=0;i<64;++i)copy[i]=fences[i];dropped=overflow;result=last_result;
 }
 av2_boot_log("watchdog","%s presents=%llu submit=%llu observed-stage=%d result=%d overflow=%llu (cached observations,not live fence query)",hang?"HANG":"snapshot",(unsigned long long)snapshot.presents,(unsigned long long)snapshot.submit,snapshot.fence_state,result,(unsigned long long)dropped);
 for(auto f:copy)if(f.handle)av2_boot_log("watchdog-fence","handle=%llx submit=%llu observed-stage=%d result=%d",(unsigned long long)f.handle,(unsigned long long)f.submit,f.stage,f.result);
}
}
