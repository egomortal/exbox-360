#include "raw_capture.h"

#include <atomic>
#include <cstdlib>
#include <exception>
extern "C" void av2_boot_fatal_log(const char*,const char*,...);
extern "C" void av2_diag_import_history_report();
extern "C" [[noreturn]] void __real_abort();
namespace {
std::atomic_flag reporting=ATOMIC_FLAG_INIT;
void terminate_reason() noexcept {
 av2_raw_reason("terminate","std::terminate",0,0);
 av2_boot_fatal_log("terminate","std::terminate invoked; current exception inspected below");
 if(auto exception=std::current_exception()){
  try {std::rethrow_exception(exception);}
  catch(const std::exception& e){av2_boot_fatal_log("terminate","exception: %s",e.what());}
  catch(...){av2_boot_fatal_log("terminate","non-std exception");}
 }else av2_boot_fatal_log("terminate","no current exception (possible explicit terminate or noexcept violation)");
 std::abort();
}
}
// Preserve the real fatal outcome. This records evidence; it does not claim a
// corrupt heap is recoverable or run guest destructors after an allocator abort.
extern "C" [[noreturn]] void __wrap_abort(){
 av2_raw_reason("abort-caller","__wrap_abort",0,
     reinterpret_cast<uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0))));
 av2_raw_abort();
 if(!reporting.test_and_set(std::memory_order_relaxed)){
  av2_boot_fatal_log("abort","abort requested; preceding guard/terminate lines identify known reasons");
  av2_diag_import_history_report();
 }
 __real_abort();
}
extern "C" void av2_install_fatal_diagnostics() noexcept {std::set_terminate(terminate_reason);}
