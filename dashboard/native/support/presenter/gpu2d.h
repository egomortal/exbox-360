/* Ordered 2D compute compositor. No device, queue or display ownership. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
#define EXBOX_GPU2D_WIDTH 3840u
#define EXBOX_GPU2D_HEIGHT 2160u
#define EXBOX_GPU2D_MAX_COMMANDS 16384u
#define EXBOX_GPU2D_SOURCE_BYTES (64u*1024u*1024u)
typedef struct {int32_t x,y,w,h;} ExboxGpuRect;
typedef enum {EXBOX_GPU_RECT=0, EXBOX_GPU_GRADIENT=1, EXBOX_GPU_TEXTURE=2} ExboxGpuKind;
/* Colors packed 0xAARRGGBB. Textures are little-endian BGRA8, straight alpha.
 * Rectangle/clip are physical pixels, half-open. Logical1080 callers multiply
 * geometry by 2; text must already be rasterized at physical resolution. */
typedef struct {
 ExboxGpuKind kind; ExboxGpuRect dst,clip;
 uint32_t top,bottom,opacity; /* opacity 0..255; rect uses top */
 const void* pixels; size_t bytes; uint32_t width,height,pitch;
} ExboxGpuCommand;
typedef struct ExboxGpu2D ExboxGpu2D;
/* All calls serial. Supplied device/queue family belongs to existing presenter.
 * Requires COMPUTE queue, >=32MiB storage range and >=64MiB source range.
 * create/destroy only when no submitted work references this helper. */
ExboxGpu2D* exbox_gpu2d_create(VkDevice device,
 const VkPhysicalDeviceMemoryProperties* memory, const VkPhysicalDeviceProperties* properties,
 VkQueueFlags queue_flags, PFN_vkGetDeviceProcAddr get);
void exbox_gpu2d_destroy(ExboxGpu2D*);
/* Validate/copy ALL source bytes before modifying GPU commands. Sources may be
 * freed after return. Fail leaves previous prepared packet untouched. Caller
 * must have completed previous submission before prepare/record reuse. */
int exbox_gpu2d_prepare(ExboxGpu2D*,const ExboxGpuCommand*,size_t);
/* Record into caller-owned begun command buffer; clear opaque black then draw.
 * Final barrier makes buffer ready for vkCmdCopyBufferToImage transfer read.
 * Does not submit, wait, acquire, present, or alter session ownership. */
void exbox_gpu2d_record(ExboxGpu2D*,VkCommandBuffer);
VkBuffer exbox_gpu2d_output(const ExboxGpu2D*);
/* Standalone validation shared by tests and prepare. */
int exbox_gpu2d_validate(const ExboxGpuCommand*,size_t,size_t* source_bytes);
#ifdef __cplusplus
}
#endif
