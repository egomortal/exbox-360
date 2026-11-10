#pragma once
#include <vulkan/vulkan.h>
#include <stddef.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ExboxAvatarGpu ExboxAvatarGpu;
ExboxAvatarGpu* exbox_avatar_create(uint32_t,uint32_t,VkDevice,const VkPhysicalDeviceMemoryProperties*,const VkPhysicalDeviceProperties*,VkQueueFlags,PFN_vkGetDeviceProcAddr);
ExboxAvatarGpu* exbox_avatar_create_with_budget(uint32_t,uint32_t,VkDevice,const VkPhysicalDeviceMemoryProperties*,const VkPhysicalDeviceProperties*,VkQueueFlags,PFN_vkGetDeviceProcAddr,ExboxGpuBudget*);
VkBuffer exbox_avatar_statistics(const ExboxAvatarGpu*,VkDeviceSize*);
uint64_t exbox_avatar_allocated(const ExboxAvatarGpu*);
int exbox_avatar_prepare(ExboxAvatarGpu*,const uint32_t*,size_t);
typedef struct {const uint32_t* words;size_t word_count;} ExboxAvatarInput;
/* Up to four immutable inputs, one output and one shared scratch allocation.
 * Caller interleaves record_actor with compositor ranges before reusing the
 * output for the next actor. Total input admission remains 32 MiB. */
int exbox_avatar_prepare_group(ExboxAvatarGpu*,const ExboxAvatarInput*,size_t);
int exbox_avatar_record_actor(ExboxAvatarGpu*,VkCommandBuffer,size_t);
void exbox_avatar_record(ExboxAvatarGpu*,VkCommandBuffer);
VkBuffer exbox_avatar_output(const ExboxAvatarGpu*);
void exbox_avatar_destroy(ExboxAvatarGpu*);
#ifdef __cplusplus
}
#endif
