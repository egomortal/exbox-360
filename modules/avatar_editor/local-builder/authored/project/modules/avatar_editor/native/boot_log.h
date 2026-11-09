#pragma once
#include <cstddef>
#include <cstdint>
// Native-only bring-up observer. Logging never allocates guest/GPU memory.
extern "C" {
bool av2_boot_init(const char* directory);
void av2_boot_log(const char* phase,const char* format,...) __attribute__((format(printf,2,3)));
bool av2_boot_phase(const char* phase); // Records phase; requests a clean frontend stop.
bool av2_boot_should_stop();
bool av2_boot_gpu_sync();
bool av2_boot_trace_pages();
bool av2_boot_vfs_trace();
void av2_boot_vfs_trace_end();
void av2_boot_gpu_fault(int result,const char* operation);
int av2_boot_gpu_result();
void av2_boot_request_stop(const char* reason);
void av2_boot_install_signals();
void av2_boot_startup_probe(const char* assets);
void av2_boot_frame(uint64_t frame);
void av2_boot_thread_created(int result);
void av2_boot_memory_timed(const char* operation,const void* address,size_t bytes,int flags,const char* backing,int result,int64_t committed_delta,uint64_t elapsed_us);
void av2_boot_memory(const char* operation,const void* address,size_t bytes,int flags,const char* backing,int result,int64_t committed_delta);
}

// Thrown only inside native C++ renderer code; caught at renderer boundaries.
struct Av2GpuFailure { int result; };
