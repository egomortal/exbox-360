#pragma once
namespace av2::native {
enum class CleanExitDecision { ready, drain_timeout, gpu_completion_unknown };
// No owner is destroyed here. Only successful drain permits save/cache work.
template<class Ops> CleanExitDecision prepare_clean_exit(Ops& ops) {
  ops.stop_watchdog();
  ops.terminate_guest();
  if(!ops.drain_guest()) return CleanExitDecision::drain_timeout;
  ops.guest_drained();
  if(!ops.gpu_completion_known()) return CleanExitDecision::gpu_completion_unknown;
  ops.finalize_save_and_ticket();
  ops.flush_pipeline_cache();
  ops.flush_logs();
  return CleanExitDecision::ready;
}
}
