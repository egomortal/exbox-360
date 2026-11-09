#pragma once
#include "gpu_edge.h"
namespace rex::system { class IGraphicsSystem; }
namespace av2 {
void use_vulkan_gpu(rex::system::IGraphicsSystem* graphics);
void initialize_gpu_ring(uint32_t ring,int32_t size_log2);
void enable_gpu_writeback(uint32_t address,int32_t block_size_log2);
void set_gpu_interrupt(uint32_t callback,uint32_t data);
GpuEdge& active_gpu_edge();
void note_title_hook(const char* name);
void frame_completed();
uint64_t completed_frames(); // Native frontend progress; no completion side effects.
}