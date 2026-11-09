#pragma once
#include "hang_monitor.h"
extern "C" void av2_diag_submit(uint64_t index,uintptr_t fence,int stage,int result);
extern "C" void av2_diag_fence(uintptr_t fence,int stage,int result);
extern "C" void av2_diag_present(int result);
namespace av2::diagnostics { Snapshot gpu_snapshot(); void report_gpu(Snapshot,bool); }
