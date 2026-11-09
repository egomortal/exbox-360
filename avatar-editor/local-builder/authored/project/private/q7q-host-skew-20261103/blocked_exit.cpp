#include "blocked_exit.h"
#include "boot_log.h"
#include <unistd.h>
extern "C" int sceSystemServiceLoadExec(const char*,const char* const*);
extern "C" [[noreturn]] void av2_exit_with_live_guests(int status){
 av2_boot_log("teardown","status=%d live guests retained; skip TerminateTitle,guest drain,save finalization,renderer/runtime/pad destruction",status);
 av2_boot_log("teardown","Q4 LoadExec exit begin; process ownership retained for OS termination");
 const int result=sceSystemServiceLoadExec("exit",nullptr);
 av2_boot_log("teardown","Q4 LoadExec returned result=%08x; retain process/resources and await shell termination",unsigned(result));
 // No abort(), exit(), global stdio flush, or unwinding through live owners.
 for(;;)::sleep(1);
}
