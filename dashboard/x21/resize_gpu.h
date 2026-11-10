// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vulkan/vulkan.h>
#include <stddef.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ExboxResize ExboxResize;
typedef struct ExboxResizeJob ExboxResizeJob;
typedef struct ExboxResizeScratch ExboxResizeScratch;
// Explicit serial presenter batch only. Same command buffer until proven completion.
// Object owns one <=4MiB scratch allocation while pending/recorded jobs reference it.
ExboxResizeScratch* exbox_resize_scratch_create(ExboxResize*);
int exbox_resize_scratch_destroy(ExboxResizeScratch*);
ExboxResizeJob* exbox_resize_prepare_shared(ExboxResize*,ExboxResizeScratch*,VkBuffer,VkDeviceSize,VkDeviceSize,uint32_t,uint32_t,uint32_t,uint32_t,int,const uint32_t*,size_t);
// Presenter-owner only; never creates a device, submits, waits, or reads pixels.
ExboxResize* exbox_resize_create(VkDevice,const VkPhysicalDeviceMemoryProperties*,const VkPhysicalDeviceProperties*,VkQueueFlags,PFN_vkGetDeviceProcAddr,ExboxGpuBudget*);
// Source: already-cropped tightly packed straight ARGB. Keep borrowed storage alive
// through successful fence completion. Bounded by the shared slot count plus one transactional replacement job.
// Table: (dw+dh) {first,count,word_offset} entries followed by original float taps.
ExboxResizeJob* exbox_resize_prepare(ExboxResize*,VkBuffer,VkDeviceSize offset,VkDeviceSize available,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int alpha,const uint32_t*,size_t words);
int exbox_resize_record(ExboxResizeJob*,VkCommandBuffer);
// Fence must cover every command using this output, including downstream UI work.
// Completion frees scratch/tables but keeps cached output. No fence waits.
int exbox_resize_complete(ExboxResizeJob*,VkFence);
// Before recording another draw using a completed cached output, acquire a new
// completion lease. complete() then checks the fence covering that draw.
int exbox_resize_begin_use(ExboxResizeJob*);
// Trusted session receipt only: call after synchronous full-submission wait SUCCESS.
// Never call on record completion, failed/unknown submission, or device-loss.
int exbox_resize_complete_after_wait(ExboxResizeJob*);
VkBuffer exbox_resize_output(const ExboxResizeJob*,VkDeviceSize*bytes);
int exbox_resize_release(ExboxResizeJob*); // refuses recorded/in-flight jobs
int exbox_resize_destroy(ExboxResize*); // refuses outstanding leases
uint64_t exbox_resize_allocated(const ExboxResize*);
#ifdef __cplusplus
}
#endif
