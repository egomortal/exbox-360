/* Ordered 2D compute compositor. No device, queue or display ownership. */
#pragma once
#include "filtered_limits.h"
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXBOX_GPU2D_WIDTH 3840u
#define EXBOX_GPU2D_HEIGHT 2160u
#define EXBOX_GPU2D_MAX_COMMANDS 16384u
#define EXBOX_GPU2D_SOURCE_BYTES (64u*1024u*1024u)
typedef struct {int32_t x,y,w,h;} ExboxGpuRect;
typedef enum {EXBOX_GPU_RECT=0, EXBOX_GPU_GRADIENT=1, EXBOX_GPU_TEXTURE=2, EXBOX_GPU_PATH=3, EXBOX_GPU_STROKE=4, EXBOX_GPU_AFFINE=5, EXBOX_GPU_BITMAP_PATH=6, EXBOX_GPU_NINE=7, EXBOX_GPU_CLEAR_LAYER=8, EXBOX_GPU_GROUP=9, EXBOX_GPU_ROUNDED=10, EXBOX_GPU_EXTERNAL=11, EXBOX_GPU_NXE_REFLECTION=12, EXBOX_GPU_RING=13, EXBOX_GPU_DISC=14, EXBOX_GPU_VERTICAL_STRIP=15, EXBOX_GPU_BEGIN_SLOT=16, EXBOX_GPU_COMPOSITE_SLOT=17, EXBOX_GPU_FILTERED=18,EXBOX_GPU_SDL_GROUP=19,EXBOX_GPU_GUIDE_SAVE_BLACK=20,EXBOX_GPU_GUIDE_RECOVER=21, EXBOX_GPU_DIRECT_LANCZOS=22, EXBOX_GPU_VIDEO_Q8=23} ExboxGpuKind;
/* Colors packed 0xAARRGGBB. Textures are little-endian BGRA8, straight alpha.
 * Rectangle/clip are physical pixels, half-open. Logical1080 callers multiply
 * geometry by 2; text must already be rasterized at physical resolution. */
typedef struct {
 ExboxGpuKind kind; ExboxGpuRect dst,clip;
 uint32_t top,bottom,opacity; /* opacity 0..255; rect uses top */
 const void* pixels; size_t bytes; uint32_t width,height,pitch;
 /* transform[0..5]: inverse affine or gradient coefficients; [6]: stroke radius;
  * [7]: closed stroke; [8]: floating world opacity. [9..10] reserved zero; [11] target0 output/1 transparent layer.
  * CLEAR_LAYER requires target1. GROUP reads layer onto target0; no host pixels.
  * GRADIENT is vertical stepped fill ([7] step1/3); ROUNDED [6] physical radius.
  * Path payload: width points of two float32 words, optionally4097BGRA gradient words;
  * height=1,pitch=payload bytes. Images remain BGRA8 rows. Blend uses XUI modes0..6.
  * No device/queue/display ownership is transferred. */
 float transform[12]; uint32_t blend,fill_type;
} ExboxGpuCommand;
/* Snapshot pass consumes the existing layer scratch; any later GROUP needs a new CLEAR_LAYER.
 * Physical half-open bounds. Mask is the same bound external committed-generation image.
 * density 1|2, pane_count<=10; mask opacity preserves the authored figure fade. */
typedef struct {uint32_t pane_count,density,has_figure;float figure_opacity;ExboxGpuRect figure;ExboxGpuRect panes[10];} ExboxGpuNxeReflection;
typedef struct ExboxGpu2D ExboxGpu2D;
/* Content keys are byte-verified; epoch retires all prior resources. No pointers retained. */
typedef struct {
 uint64_t epoch,hits,in_packet_hits,misses,uploaded_bytes,hashed_bytes,verified_bytes;
 uint64_t evictions,compactions,invalidations,last_uploaded_bytes,last_hits,last_misses;
 uint64_t published_bytes,publications,lease_hits,retirements;
 size_t resident_bytes,entries,cpu_metadata_bytes;
} ExboxGpuSourceStats;
typedef uint64_t ExboxGpuSourceLease;
typedef struct {uint64_t producer_id,owner_epoch,generation;} ExboxGpuSourceKey;
/* Presenter thread, after prior completion. Publication owns a byte copy inside
 * the existing source buffer until retirement. Registered leases cannot be evicted.
 * Failed publication or prepare preserves prior commands. Changed-key publication
 * is explicit; same key with differing bytes is rejected. Never use pointer keys. */
int exbox_gpu2d_source_publish(ExboxGpu2D*,ExboxGpuSourceKey,const void*,size_t,ExboxGpuSourceLease*);
int exbox_gpu2d_source_retire(ExboxGpu2D*,ExboxGpuSourceLease);
int exbox_gpu2d_prepare_leased(ExboxGpu2D*,const ExboxGpuCommand*,size_t,const ExboxGpuSourceLease*,uint64_t external_generation);
int exbox_gpu2d_source_epoch(ExboxGpu2D*,uint64_t);
int exbox_gpu2d_source_stats(const ExboxGpu2D*,ExboxGpuSourceStats*);

/* All calls serial. Supplied device/queue family belongs to existing presenter.
 * Requires COMPUTE queue, >=32MiB storage range and >=64MiB source range.
 * create/destroy only when no submitted work references this helper. */
ExboxGpu2D* exbox_gpu2d_create(uint32_t width,uint32_t height,VkDevice device,
 const VkPhysicalDeviceMemoryProperties* memory, const VkPhysicalDeviceProperties* properties,
 VkQueueFlags queue_flags, PFN_vkGetDeviceProcAddr get);
ExboxGpu2D* exbox_gpu2d_create_with_budget(uint32_t,uint32_t,VkDevice,const VkPhysicalDeviceMemoryProperties*,const VkPhysicalDeviceProperties*,VkQueueFlags,PFN_vkGetDeviceProcAddr,ExboxGpuBudget*);
/* Kind18: pixels=NULL, bytes=nonzero generation, transform9=slot0..3;
 * width/height/pitch match borrowed BGRA8. transform10=bilinear0/nearest1.
 * Bind on presenter after prior completion; caller retains output through fence. */
unsigned exbox_gpu2d_filtered_capacity(void);
int exbox_gpu2d_bind_filtered(ExboxGpu2D*,unsigned,VkBuffer,size_t,uint32_t,uint32_t,uint32_t,uint64_t);
int exbox_gpu2d_unbind_filtered(ExboxGpu2D*,unsigned); /* before retiring borrowed output */
void exbox_gpu2d_complete_after_wait(ExboxGpu2D*); /* only after proven consumer completion */
int exbox_gpu2d_filtered_pending(const ExboxGpu2D*);
size_t exbox_gpu2d_allocated(const ExboxGpu2D*);
/* Optional one-time allocation on serial presenter owner; existing packet retained on failure. */
int exbox_gpu2d_enable_custom_slot(ExboxGpu2D*);
/* Borrowed same-device STORAGE_BUFFER straight-BGRA image; bind only after prior
 * submission completes. Success invalidates old prepared commands. Never freed
 * by compositor. Owner retains it through completion (and unsafe exit). */
int exbox_gpu2d_bind_external(ExboxGpu2D*,VkBuffer,size_t,uint32_t,uint32_t,uint32_t,uint64_t);
/* External draws must explicitly match the bound committed generation. */
int exbox_gpu2d_prepare_external(ExboxGpu2D*,const ExboxGpuCommand*,size_t,uint64_t);
void exbox_gpu2d_destroy(ExboxGpu2D*);
/* Validate/copy ALL source bytes before modifying GPU commands. Sources may be
 * freed after return. Fail leaves previous prepared packet untouched. Caller
 * must have completed previous submission before prepare/record reuse. */
int exbox_gpu2d_prepare(ExboxGpu2D*,const ExboxGpuCommand*,size_t);
/* Record into caller-owned begun command buffer; clear opaque black then draw.
 * Final barrier makes buffer ready for vkCmdCopyBufferToImage transfer read.
 * Does not submit, wait, acquire, present, or alter session ownership. */
void exbox_gpu2d_record(ExboxGpu2D*,VkCommandBuffer);
/* Record a contiguous portion of one prepared stream. begin must start at 0;
 * only begin clears the output. Continuations use the same command buffer
 * and render target, preserving pixels, groups and snapshots across ranges.
 * This permits ordered avatar raster/composite interleaving with one target.
 * Returns 0 before recording if the range or continuation is invalid. */
int exbox_gpu2d_record_range(ExboxGpu2D*,VkCommandBuffer,size_t first,size_t count,int begin);
VkBuffer exbox_gpu2d_output(const ExboxGpu2D*);
/* Standalone validation shared by tests and prepare. */
int exbox_gpu2d_validate(const ExboxGpuCommand*,size_t,size_t* source_bytes);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
int exbox_gpu2d_enable_guide(ExboxGpu2D*);
#ifdef __cplusplus
}
#endif
