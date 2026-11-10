#pragma once
#include <vulkan/vulkan.h>
#include <stddef.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed from the sole session. Never destroy these handles. All calls use
 * the session owner thread, after previous present completes. Destroy GPU
 * client resources before native_close; retain them on unknown completion. */
typedef struct {
 VkDevice device; VkPhysicalDeviceMemoryProperties memory;
 VkPhysicalDeviceProperties properties; VkQueueFlags queue_flags;
 PFN_vkGetDeviceProcAddr get; uint32_t width,height;
 ExboxGpuBudget* budget; /* Borrowed session ledger, same lifetime as device. */
} ExboxNativeGpuContext;
int exbox_native_gpu_context(ExboxNativeGpuContext*);
typedef void (*ExboxNativeGpuRecord)(void*,VkCommandBuffer);
/* Output: BGRA8 tightly packed full extent, TRANSFER_SRC usage. record emits
 * GPU work and its shader-write -> transfer-read barrier into this command.
 * No submit/acquire/wait/readback inside callback. Output/client stays alive
 * until this function succeeds; on failure retain for safe title exit. */
int exbox_native_present_gpu(VkBuffer output,size_t output_bytes,
 ExboxNativeGpuRecord record,void* user);
#ifdef __cplusplus
}
#endif
