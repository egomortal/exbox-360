// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "filtered_limits.h"
#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
unsigned exbox_filtered_capacity(void);
typedef struct ExboxFilteredOwner ExboxFilteredOwner;
typedef struct ExboxFilteredPacket ExboxFilteredPacket;
typedef struct {uint64_t owner,id,generation;int32_t x,y,w,h,dw,dh;uint32_t filter;} ExboxFilteredKey;
typedef struct {ExboxFilteredKey key;const ExboxFilteredPacket*packet;} ExboxFilteredRequest;
typedef struct {ExboxFilteredKey key;VkBuffer buffer;VkDeviceSize bytes;uint32_t width,height,pitch;uint64_t generation;} ExboxFilteredBinding;
typedef struct {uint64_t uploads,uploaded_bytes,hits,misses;} ExboxFilteredStats;
typedef int (*ExboxFilteredUnbind)(void*context,unsigned slot);
enum {EXBOX_FILTERED_IDLE=0,EXBOX_FILTERED_PREPARED=1,EXBOX_FILTERED_RECORDED=2,EXBOX_FILTERED_QUARANTINED=3};
// Publication-time copy only. Input is already normalized straight ARGB and original
// taps, not SDL surfaces. C++ producers can share existing immutable Prepared instead.
ExboxFilteredPacket* exbox_filtered_packet_create(const ExboxFilteredKey*,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int alpha,const uint32_t*pixels,size_t pixel_count,const uint32_t*taps,size_t tap_words);
int exbox_filtered_packet_retain(ExboxFilteredPacket*);
void exbox_filtered_packet_release(ExboxFilteredPacket*);
ExboxFilteredOwner* exbox_filtered_owner_create(VkDevice,const VkPhysicalDeviceMemoryProperties*,const VkPhysicalDeviceProperties*,VkQueueFlags,PFN_vkGetDeviceProcAddr,ExboxGpuBudget*,ExboxFilteredUnbind,void*context);
// Serial presenter only. Packet handles retained by caller through this synchronous
// call; no Prepared vector copying. A NULL packet is allowed only for a cache hit.
// output_count=0 on failure. Bind outputs before preparing consumer commands.
int exbox_filtered_prepare(ExboxFilteredOwner*,const ExboxFilteredRequest*,size_t count,ExboxFilteredBinding*out,size_t capacity,size_t*output_count);
int exbox_filtered_record(ExboxFilteredOwner*,VkCommandBuffer);
// Only pass success after full filter+consumer submission completion is proven.
int exbox_filtered_complete_after_wait(ExboxFilteredOwner*,int proven_success);
int exbox_filtered_cancel_unsubmitted(ExboxFilteredOwner*);
int exbox_filtered_invalidate(ExboxFilteredOwner*,uint64_t resource_owner);
int exbox_filtered_phase(const ExboxFilteredOwner*);
int exbox_filtered_stats(const ExboxFilteredOwner*,ExboxFilteredStats*);
// Returns0 and retains the handle/state/dependencies if completion is unresolved.
// Returns1 and deletes handle only after safe unbind/retirement. No implicit wait.
int exbox_filtered_owner_destroy(ExboxFilteredOwner*);
#ifdef __cplusplus
}
#endif
