#include "gpu2d.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <math.h>
#include "gpu2d_spv.h"
#include "source_residency.h"
#define GPU_BYTES ((size_t)g->width*g->height*4u)
#define FUNCTIONS(X) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdCopyBuffer) X(CmdFillBuffer) X(CmdPipelineBarrier) \
 X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
typedef struct {int32_t rect[4],clip[4];uint32_t colors[4],texture[4],extent[4];float transform[12];} Push;
typedef struct {VkBuffer buffer;size_t bytes;uint32_t width,height,pitch;uint64_t generation;} Filtered;
struct ExboxGpu2D {
 Filtered filtered[EXBOX_FILTERED_MAX_SLOTS];VkDescriptorSet filtered_sets[EXBOX_FILTERED_MAX_SLOTS];unsigned filtered_pending;
 SourceCache residency;ExboxGpuCommand resolved[EXBOX_GPU2D_MAX_COMMANDS];
 VkDevice device; VkPhysicalDeviceMemoryProperties memory; uint32_t width,height; size_t allocated; ExboxGpuBudget local_budget,*budget; ExboxGpuReservation reservations[6];
 VkBuffer external; size_t external_bytes; uint32_t external_width,external_height,external_pitch; uint64_t external_generation;
#define FIELD(n) PFN_vk##n n;
 FUNCTIONS(FIELD)
#undef FIELD
 VkBuffer output,source,scratch,layer,slot,guide; VkDeviceMemory output_mem,source_mem,scratch_mem,layer_mem,slot_mem,guide_mem; void* mapped;
 VkDescriptorSetLayout descriptors; VkPipelineLayout layout; VkPipeline pipeline;
 VkDescriptorPool pool; VkDescriptorSet set;
 Push commands[EXBOX_GPU2D_MAX_COMMANDS];size_t count;
 size_t record_cursor;VkCommandBuffer record_command;int record_open;
};
int exbox_gpu2d_validate(const ExboxGpuCommand* c,size_t n,size_t* bytes) {
 size_t total=0;int layer_ready=0,slot_active=0;uint32_t slot_width=0,slot_height=0,guide_width=0,guide_height=0;
 if(!bytes || n>EXBOX_GPU2D_MAX_COMMANDS || (n&&!c))return 0;
 for(size_t i=0;i<n;++i) {
  const ExboxGpuCommand* a=c+i;
  if(a->kind<EXBOX_GPU_RECT || (a->kind>EXBOX_GPU_GUIDE_RECOVER && a->kind!=EXBOX_GPU_DIRECT_LANCZOS&&a->kind!=EXBOX_GPU_VIDEO_Q8) || a->opacity>255)return 0;
  const ExboxGpuRect* r[2]={&a->dst,&a->clip};
  for(int j=0;j<2;++j)if(r[j]->x < -16384 || r[j]->y < -16384 || r[j]->x>16384 || r[j]->y>16384 || r[j]->w<0 || r[j]->h<0 || r[j]->w>16384 || r[j]->h>16384)return 0;
  if(a->blend>10 || (a->blend==7&&a->kind!=EXBOX_GPU_AFFINE&&a->kind!=EXBOX_GPU_FILTERED&&a->kind!=EXBOX_GPU_DIRECT_LANCZOS&&a->kind!=EXBOX_GPU_VIDEO_Q8) || (a->blend>=8?(a->fill_type!=0x53444cu||(a->transform[11]!=1 && !(a->transform[11]==0 && (a->blend==8||a->blend==9||a->blend==10)))):a->fill_type>3))return 0;
  for(int k=0;k<12;k++)if(!__builtin_isfinite(a->transform[k]))return 0;
  /* Target and group source remain serial, bounded and nonaliasing. */
  if(a->blend==8&&a->kind!=EXBOX_GPU_RECT)return 0;
  if((a->blend==9||a->blend==10)&&(a->top&0xffffffu)!=0xffffffu)return 0;
  if((a->blend==9||a->blend==10)&&(a->top&0xffffffu)!=0xffffffu)return 0;
  if(a->blend==9&&a->kind!=EXBOX_GPU_AFFINE&&a->kind!=EXBOX_GPU_FILTERED&&a->kind!=EXBOX_GPU_DIRECT_LANCZOS)return 0;
  if(a->blend==10&&(a->kind!=EXBOX_GPU_AFFINE||a->transform[10]!=1||!(a->bottom>>16)||!(a->bottom&65535u)||a->transform[6]<0||a->transform[7]<0||a->transform[6]+(a->bottom>>16)>a->width||a->transform[7]+(a->bottom&65535u)>a->height))return 0;
  if(a->transform[8]<0 || a->transform[8]>1)return 0;

  if(a->kind==EXBOX_GPU_FILTERED){if(a->transform[9]<0||a->transform[9]>=EXBOX_FILTERED_MAX_SLOTS||a->transform[9]!=floorf(a->transform[9]))return 0;}
  else if(a->transform[9]!=0&&a->transform[9]!=1&&a->transform[9]!=2&&a->transform[9]!=3)return 0;
  if(a->transform[10]!=0&&a->transform[10]!=1)return 0;
  if(a->transform[10]==1){
   if(a->kind!=EXBOX_GPU_AFFINE&&a->kind!=EXBOX_GPU_FILTERED)return 0;
   const int fields[6]={0,4,2,5,6,7};
   for(int z=0;z<6;z++){float v=a->transform[fields[z]];if(v!=floorf(v)||v < -16384.f||v>536870912.f)return 0;}
   for(int axis=0;axis<2;axis++){
    int64_t step=(int64_t)a->transform[axis?4:0],origin=(int64_t)a->transform[axis?5:2],offset=(int64_t)a->transform[axis?7:6];
    int64_t lo=axis?a->clip.y:a->clip.x,hi=lo+(axis?a->clip.h:a->clip.w);
    int64_t first=axis?a->dst.y:a->dst.x,last=first+(axis?a->dst.h:a->dst.w);
    if(lo<first)lo=first;if(hi>last)hi=last;
    if(step<1||offset<0||offset>8192)return 0;
    if(hi>lo){int64_t low=step/2+(lo-origin)*step,high=step/2+(hi-1-origin)*step;if(low<0||high>2147483647)return 0;}
   }
  }
  if(a->transform[11]!=0 && a->transform[11]!=1 && a->transform[11]!=2)return 0;
  if(a->transform[11]==2 && a->kind!=EXBOX_GPU_BEGIN_SLOT && !slot_active)return 0;
  if(slot_active && a->transform[11]!=2 && a->kind!=EXBOX_GPU_COMPOSITE_SLOT)return 0;
  if(a->kind==EXBOX_GPU_VIDEO_Q8){
   if(!a->pixels||!a->width||!a->height||a->width>3840||a->height>2160||a->pitch%4||a->pitch<a->width*4u||a->dst.w<1||a->dst.h<1||a->dst.w>3840||a->dst.h>2160||a->transform[11]||(a->blend!=1&&a->blend!=7)||a->top!=0xffffffffu)return 0;
   for(int z=0;z<12;z++)if(z!=8&&a->transform[z]!=0)return 0;
   size_t span=(size_t)a->pitch*a->height;if(span>a->bytes||span>EXBOX_GPU2D_SOURCE_BYTES-total)return 0;
   for(uint32_t y=0;y<a->height;y++)for(uint32_t x=0;x<a->width;x++){uint32_t v;memcpy(&v,(const char*)a->pixels+(size_t)y*a->pitch+x*4,4);if((v>>24)!=255)return 0;}total+=span;
  }
  if(a->kind==EXBOX_GPU_DIRECT_LANCZOS){
   uint32_t h[8];if(!a->pixels||a->bytes<sizeof h||a->bytes%4||a->bytes>2u*1024*1024||a->bytes>EXBOX_GPU2D_SOURCE_BYTES-total)return 0;memcpy(h,a->pixels,sizeof h);
   if(!h[0]||h[0]>16||!h[1]||h[1]>2160||!h[2]||h[2]>3840||!h[3]||h[3]>2160||h[4]>1||h[5]!=8||h[7]!=(uint32_t)(a->bytes/4))return 0;
   if(a->width!=h[0]||a->height!=h[1]||a->pitch!=h[0]*4||a->dst.w!=(int)h[2]||a->dst.h!=(int)h[3]||a->transform[11]||(a->blend!=1&&a->blend!=7&&a->blend!=9))return 0;
   for(int z=0;z<12;z++)if(z!=8&&a->transform[z]!=0)return 0;
   uint64_t head=(uint64_t)(h[2]+h[3])*3;if(h[6]<8+head||(uint64_t)h[6]+(uint64_t)h[0]*h[1]!=h[7])return 0;
   const unsigned char*data=(const unsigned char*)a->pixels;
   for(uint32_t j=0;j<h[2]+h[3];j++){uint32_t tap[3];memcpy(tap,data+(8+j*3)*4,sizeof tap);uint32_t bound=j<h[2]?h[0]:h[1];if(!tap[1]||(uint64_t)tap[0]+tap[1]>bound||tap[1]>16||tap[2]<head||(uint64_t)8+tap[2]+tap[1]>h[6])return 0;
    for(uint32_t k=0;k<tap[1];k++){float v;memcpy(&v,data+(8+tap[2]+k)*4,4);if(!__builtin_isfinite(v)||v< -2||v>2)return 0;}
   }total+=a->bytes;
  }
  if(a->kind==EXBOX_GPU_VERTICAL_STRIP){
   if(a->width<1||a->width>16||a->height!=(uint32_t)a->dst.h||a->opacity!=255||a->blend||a->fill_type||a->transform[8]!=1||a->transform[11])return 0;
   for(int z=0;z<12;z++)if(z!=8&&a->transform[z]!=0)return 0;
  }
  if(a->kind==EXBOX_GPU_RING||a->kind==EXBOX_GPU_DISC){
   if(a->pixels||a->bytes||a->width||a->height||a->pitch||a->opacity!=255||a->blend||a->fill_type||a->transform[8]!=1||a->transform[9]||a->transform[10]||a->transform[11]||(a->top>>24)!=255)return 0;
   for(int z=0;z<4;z++)if(a->transform[z]!=floorf(a->transform[z]))return 0;
   if(a->transform[0]<-8192||a->transform[0]>8192||a->transform[1]<-8192||a->transform[1]>8192||a->transform[2]<0||a->transform[2]>4096||a->transform[3]<0||a->transform[3]>8192)return 0;
   int radius=(int)a->transform[2],extra=a->kind==EXBOX_GPU_DISC?1:0;
   if(a->dst.x!=(int)a->transform[0]-radius-extra||a->dst.y!=(int)a->transform[1]-radius||a->dst.w!=radius*2+1+extra*2||a->dst.h!=radius*2+1)return 0;
  }
  if(a->kind==EXBOX_GPU_CLEAR_LAYER && a->transform[11]!=1)return 0;
  if((a->kind==EXBOX_GPU_GROUP||a->kind==EXBOX_GPU_SDL_GROUP) && a->transform[11]!=0)return 0;
  if(a->kind==EXBOX_GPU_CLEAR_LAYER){if(a->dst.x||a->dst.y||a->clip.x||a->clip.y||a->dst.w!=a->clip.w||a->dst.h!=a->clip.h)return 0;layer_ready=1;}
  else if((a->transform[11]==1||a->kind==EXBOX_GPU_GROUP)&&!layer_ready)return 0;
  if(a->kind==EXBOX_GPU_BEGIN_SLOT){
   if(slot_active||a->transform[11]!=2||a->pixels||a->bytes||a->dst.x||a->dst.y||memcmp(&a->dst,&a->clip,sizeof a->dst)||!((a->dst.w==420&&a->dst.h==320)||(a->dst.w==840&&a->dst.h==640)))return 0;
   for(int j=0;j<4;j++)if(a->transform[j]!=floorf(a->transform[j])||a->transform[j]<0||a->transform[j]>8192)return 0;
   if(a->transform[2]<1||a->transform[3]<1)return 0;slot_active=1;slot_width=(uint32_t)a->dst.w;slot_height=(uint32_t)a->dst.h;layer_ready=0;
  }
  if(a->kind==EXBOX_GPU_COMPOSITE_SLOT){
   if(!slot_active||a->transform[11]!=0||a->pixels||a->bytes||a->width!=slot_width||a->height!=slot_height||a->dst.w<1||a->dst.h<1)return 0;slot_active=0;
  }
  if(a->kind==EXBOX_GPU_SDL_GROUP){
   if(a->pixels||a->bytes||a->width||a->height||a->pitch||a->blend||a->fill_type||a->transform[8]!=1||a->transform[9]||a->transform[10]||a->dst.w<1||a->dst.h<1)return 0;
   for(int k=0;k<4;k++)if(a->transform[k]!=floorf(a->transform[k])||a->transform[k]<0||a->transform[k]>8192)return 0;
   if(a->transform[2]<1||a->transform[3]<1)return 0;
  }
  if(a->kind==EXBOX_GPU_GUIDE_SAVE_BLACK||a->kind==EXBOX_GPU_GUIDE_RECOVER){
   if(a->pixels||a->bytes||!a->width||!a->height||a->width>3840||a->height>2160||(uint64_t)a->width*a->height>3888000u||a->pitch!=a->width*4u||a->transform[11]||a->blend||a->fill_type||a->transform[8]!=1||a->opacity!=255||!layer_ready)return 0;
   if(a->kind==EXBOX_GPU_GUIDE_SAVE_BLACK){if(a->dst.x||a->dst.y||a->dst.w!=(int32_t)a->width||a->dst.h!=(int32_t)a->height||memcmp(&a->dst,&a->clip,sizeof a->dst))return 0;guide_width=a->width;guide_height=a->height;}
   else {if(a->width!=guide_width||a->height!=guide_height||a->dst.w!=(int32_t)a->width||a->dst.h!=(int32_t)a->height)return 0;guide_width=guide_height=0;layer_ready=0;}
  }
  if(a->kind==EXBOX_GPU_NXE_REFLECTION)layer_ready=0;

  if((a->kind==EXBOX_GPU_GRADIENT && a->transform[7]!=1 && a->transform[7]!=3)||(a->kind==EXBOX_GPU_ROUNDED && (a->transform[6]<0||a->transform[6]>8192)))return 0;
  if((a->kind==EXBOX_GPU_PATH || a->kind==EXBOX_GPU_STROKE || a->kind==EXBOX_GPU_BITMAP_PATH) && (a->height!=1 || a->width<2 || a->width>1024 || a->pitch<(size_t)a->width*8))return 0;
  if(a->kind==EXBOX_GPU_FILTERED && (a->pixels||!a->bytes||!a->width||!a->height||a->width>8192||a->height>8192||a->pitch%4||a->pitch<a->width*4u))return 0;
  if(a->kind==EXBOX_GPU_EXTERNAL && (a->pixels||a->bytes||!a->width||!a->height||a->width>8192||a->height>8192||a->pitch%4||a->pitch<a->width*4u))return 0;
  if(a->kind==EXBOX_GPU_NXE_REFLECTION){
   ExboxGpuNxeReflection m;if(!a->pixels||a->bytes!=sizeof m||a->transform[11]!=0||a->bytes>EXBOX_GPU2D_SOURCE_BYTES-total)return 0;memcpy(&m,a->pixels,sizeof m);
   if(m.pane_count>10||(m.density!=1&&m.density!=2)||m.has_figure>1||!__builtin_isfinite(m.figure_opacity)||m.figure_opacity<0||m.figure_opacity>1)return 0;
   const ExboxGpuRect* rr[11];rr[0]=&m.figure;for(unsigned j=0;j<10;j++)rr[j+1]=&m.panes[j];
   for(unsigned j=0;j<11;j++)if(rr[j]->x < -16384||rr[j]->y < -16384||rr[j]->x>16384||rr[j]->y>16384||rr[j]->w<0||rr[j]->h<0||rr[j]->w>16384||rr[j]->h>16384)return 0;
   if(m.has_figure&&(!m.figure.w||!m.figure.h||!a->width||!a->height||a->width>8192||a->height>8192||a->pitch%4||a->pitch<a->width*4u))return 0;
   if(a->dst.x!=130*(int)m.density||a->dst.y!=852*(int)m.density||a->dst.w!=1670*(int)m.density||a->dst.h!=147*(int)m.density||memcmp(&a->dst,&a->clip,sizeof a->dst))return 0;
   total+=sizeof m;
  }
  if(((a->kind>=EXBOX_GPU_TEXTURE && a->kind<=EXBOX_GPU_NINE)||a->kind==EXBOX_GPU_VERTICAL_STRIP)) {
   if(!a->pixels || !a->width || !a->height || a->width>8192 || a->height>8192 || a->pitch%4 || a->pitch<a->width*4u)return 0;
   size_t span=(size_t)a->pitch*a->height;
   if(span>a->bytes || span>EXBOX_GPU2D_SOURCE_BYTES-total)return 0;
   if(a->kind==EXBOX_GPU_PATH || a->kind==EXBOX_GPU_STROKE || a->kind==EXBOX_GPU_BITMAP_PATH) {
    /* memcpy permits an unaligned caller payload without undefined reads. */
    for(uint32_t k=0;k<a->width*2;k++){float value;memcpy(&value,(const char*)a->pixels+k*4,4);if(!__builtin_isfinite(value)||value < -1048576.f||value > 1048576.f)return 0;}
    if(a->kind==EXBOX_GPU_STROKE && (a->transform[6]<0||a->transform[6]>16384))return 0;
   }
   if(a->kind==EXBOX_GPU_BITMAP_PATH || a->kind==EXBOX_GPU_NINE){
    const size_t head=a->kind==EXBOX_GPU_BITMAP_PATH?(size_t)a->width*8:0;
    uint32_t meta[12];if(a->height!=1 || span<head+sizeof meta)return 0;memcpy(meta,(const char*)a->pixels+head,sizeof meta);
    if(!meta[0]||!meta[1]||meta[0]>8192||meta[1]>8192||meta[2]<meta[0]||meta[2]>32768)return 0;
    if((uint64_t)meta[2]*meta[1]*4!=span-head-sizeof meta)return 0;
    if(a->kind==EXBOX_GPU_NINE){float v[6];memcpy(v,meta+3,sizeof v);for(int j=0;j<6;j++)if(!__builtin_isfinite(v[j])||v[j]<0||v[j]>8192)return 0;
     if(v[0]<=0||v[1]<=0||v[0]*v[1]*4>16u*1024*1024)return 0;
    }
   }
   total+=span;
  }
 }
 if(slot_active)return 0;*bytes=total;return 1;
}
static int buffer(ExboxGpu2D* g,size_t bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,VkBuffer* b,VkDeviceMemory* m) {
 VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 if(g->CreateBuffer(g->device,&ci,0,b)!=VK_SUCCESS)return 0;
 VkMemoryRequirements req;g->GetBufferMemoryRequirements(g->device,*b,&req);
 unsigned slot=m==&g->output_mem?0:m==&g->source_mem?1:m==&g->scratch_mem?2:m==&g->layer_mem?3:m==&g->slot_mem?4:5;
 if(!exbox_gpu_budget_reserve(g->budget,&g->reservations[slot],req.size))return 0;
 uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<g->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (g->memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
 if(type==UINT32_MAX){exbox_gpu_budget_release(g->budget,&g->reservations[slot]);return 0;}
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 if(g->AllocateMemory(g->device,&ai,0,m)!=VK_SUCCESS){exbox_gpu_budget_release(g->budget,&g->reservations[slot]);return 0;}g->allocated+=req.size;return g->BindBufferMemory(g->device,*b,*m,0)==VK_SUCCESS;
}
void exbox_gpu2d_destroy(ExboxGpu2D* g) {
 if(g&&(g->filtered_pending||g->record_open))return;
 if(!g)return;
 if(g->pipeline)g->DestroyPipeline(g->device,g->pipeline,0);
 if(g->pool)g->DestroyDescriptorPool(g->device,g->pool,0);
 if(g->layout)g->DestroyPipelineLayout(g->device,g->layout,0);
 if(g->descriptors)g->DestroyDescriptorSetLayout(g->device,g->descriptors,0);
 if(g->mapped)g->UnmapMemory(g->device,g->source_mem);
 if(g->guide)g->DestroyBuffer(g->device,g->guide,0);
 if(g->guide_mem){g->FreeMemory(g->device,g->guide_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[5]);}
 if(g->slot)g->DestroyBuffer(g->device,g->slot,0);
 if(g->slot_mem){g->FreeMemory(g->device,g->slot_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[4]);}
 if(g->source)g->DestroyBuffer(g->device,g->source,0);
 if(g->scratch)g->DestroyBuffer(g->device,g->scratch,0);
 if(g->scratch_mem){g->FreeMemory(g->device,g->scratch_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[2]);}
 if(g->layer)g->DestroyBuffer(g->device,g->layer,0);
 if(g->layer_mem){g->FreeMemory(g->device,g->layer_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[3]);}
 if(g->output)g->DestroyBuffer(g->device,g->output,0);
 if(g->source_mem){g->FreeMemory(g->device,g->source_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[1]);}
 if(g->output_mem){g->FreeMemory(g->device,g->output_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[0]);}
 free(g);
}
ExboxGpu2D* exbox_gpu2d_create_with_budget(uint32_t width,uint32_t height,VkDevice d,const VkPhysicalDeviceMemoryProperties* memory,const VkPhysicalDeviceProperties* props,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get,ExboxGpuBudget* budget) {
 if(!width||!height||width>3840||height>2160||!d||!memory||!get||!props||!(flags&VK_QUEUE_COMPUTE_BIT) ||
 props->limits.maxStorageBufferRange<EXBOX_GPU2D_SOURCE_BYTES || props->limits.maxPerStageDescriptorStorageBuffers<8 || props->limits.maxDescriptorSetStorageBuffers<8 ||
 props->limits.maxPushConstantsSize<sizeof(Push) || props->limits.maxComputeWorkGroupInvocations<64 ||
 props->limits.maxComputeWorkGroupSize[0]<8 || props->limits.maxComputeWorkGroupSize[1]<8 ||
 props->limits.maxComputeWorkGroupCount[0]<480 || props->limits.maxComputeWorkGroupCount[1]<270)return 0;
 ExboxGpu2D* g=calloc(1,sizeof *g);if(!g)return 0;g->device=d;g->memory=*memory;g->width=width;g->height=height;g->local_budget=(ExboxGpuBudget)EXBOX_GPU_BUDGET_INIT;g->budget=budget?budget:&g->local_budget;
#define LOAD(n) g->n=(PFN_vk##n)get(d,"vk" #n);if(!g->n){free(g);return 0;}
 FUNCTIONS(LOAD)
#undef LOAD
 if(!buffer(g,GPU_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->output,&g->output_mem) ||
 !buffer(g,EXBOX_GPU2D_SOURCE_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&g->source,&g->source_mem))goto fail;
 if(!buffer(g,(size_t)height*2*1025*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->scratch,&g->scratch_mem))goto fail;
 if(!buffer(g,GPU_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->layer,&g->layer_mem))goto fail;
 if(g->MapMemory(d,g->source_mem,0,EXBOX_GPU2D_SOURCE_BYTES,0,&g->mapped)!=VK_SUCCESS)goto fail;
 VkDescriptorSetLayoutBinding bindings[8]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
 bindings[2]=bindings[1];bindings[2].binding=2;bindings[3]=bindings[1];bindings[3].binding=3;bindings[4]=bindings[1];bindings[4].binding=4;bindings[5]=bindings[1];bindings[5].binding=5;bindings[6]=bindings[1];bindings[6].binding=6;bindings[7]=bindings[1];bindings[7].binding=7;
 VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=8,.pBindings=bindings};
 if(g->CreateDescriptorSetLayout(d,&ds,0,&g->descriptors)!=VK_SUCCESS)goto fail;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
 VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&g->descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(g->CreatePipelineLayout(d,&li,0,&g->layout)!=VK_SUCCESS)goto fail;
 VkShaderModule shader=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof gpu2d_spv,.pCode=gpu2d_spv};
 if(g->CreateShaderModule(d,&sm,0,&shader)!=VK_SUCCESS)goto fail;
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=g->layout};
 VkResult rc=g->CreateComputePipelines(d,VK_NULL_HANDLE,1,&pi,0,&g->pipeline);g->DestroyShaderModule(d,shader,0);if(rc!=VK_SUCCESS)goto fail;
 VkDescriptorPoolSize sz={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8*(EXBOX_FILTERED_MAX_SLOTS+1)};
 VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=EXBOX_FILTERED_MAX_SLOTS+1,.poolSizeCount=1,.pPoolSizes=&sz};
 if(g->CreateDescriptorPool(d,&pool,0,&g->pool)!=VK_SUCCESS)goto fail;
 VkDescriptorSetLayout layouts[EXBOX_FILTERED_MAX_SLOTS+1];VkDescriptorSet sets[EXBOX_FILTERED_MAX_SLOTS+1];for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS+1;j++)layouts[j]=g->descriptors;
 VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=g->pool,.descriptorSetCount=EXBOX_FILTERED_MAX_SLOTS+1,.pSetLayouts=layouts};
 if(g->AllocateDescriptorSets(d,&sa,sets)!=VK_SUCCESS)goto fail;g->set=sets[0];for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS;j++)g->filtered_sets[j]=sets[j+1];
 VkDescriptorBufferInfo bi[8]={{g->output,0,GPU_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES},{g->scratch,0,(size_t)height*2*1025*4},{g->layer,0,GPU_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES}};
 for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS+1;j++){VkWriteDescriptorSet wr[8];memset(wr,0,sizeof wr);for(unsigned i=0;i<8;i++){wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstSet=sets[j];wr[i].dstBinding=i;wr[i].descriptorCount=1;wr[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;wr[i].pBufferInfo=bi+i;}g->UpdateDescriptorSets(d,8,wr,0,0);}return g;
 fail:exbox_gpu2d_destroy(g);return 0;
}
static int32_t maxi(int32_t a,int32_t b){return a>b?a:b;}
static int32_t mini(int32_t a,int32_t b){return a<b?a:b;}
static int prepare_packet(ExboxGpu2D* g,const ExboxGpuCommand* c,size_t n,int external,uint64_t generation,const ExboxGpuSourceLease*leases) {
 size_t bytes;if(!g||g->filtered_pending||g->record_open||!exbox_gpu2d_validate(c,n,&bytes))return 0;
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_FILTERED){const Filtered*f=g->filtered+(unsigned)c[i].transform[9];if(!f->buffer||f->generation!=(uint64_t)c[i].bytes||f->width!=c[i].width||f->height!=c[i].height||f->pitch!=c[i].pitch)return 0;}
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_SDL_GROUP && (c[i].transform[0]+c[i].transform[2]>g->width||c[i].transform[1]+c[i].transform[3]>g->height))return 0;
 for(size_t i=0;i<n;i++)if((c[i].kind==EXBOX_GPU_GUIDE_SAVE_BLACK||c[i].kind==EXBOX_GPU_GUIDE_RECOVER)&&(!g->guide||c[i].width>g->width||c[i].height>g->height))return 0;
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_CLEAR_LAYER && (c[i].dst.w!=(int32_t)g->width||c[i].dst.h!=(int32_t)g->height))return 0;
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_EXTERNAL && (!external||!g->external||generation!=g->external_generation||c[i].width!=g->external_width||c[i].height!=g->external_height||c[i].pitch!=g->external_pitch))return 0;
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_NXE_REFLECTION){ExboxGpuNxeReflection m;memcpy(&m,c[i].pixels,sizeof m);if(g->width!=1920*m.density||g->height!=1080*m.density)return 0;if(m.has_figure&&(!external||!g->external||generation!=g->external_generation||c[i].width!=g->external_width||c[i].height!=g->external_height||c[i].pitch!=g->external_pitch))return 0;}
 for(size_t i=0;i<n;i++)if(c[i].kind==EXBOX_GPU_BEGIN_SLOT){
  if(!g->slot||g->width!=(uint32_t)c[i].dst.w*1920/420||g->height!=(uint32_t)c[i].dst.h*1080/320||c[i].transform[0]+c[i].transform[2]>g->width||c[i].transform[1]+c[i].transform[3]>g->height)return 0;
 }
 for(size_t i=0;i<n;i++){size_t span=c[i].kind==EXBOX_GPU_DIRECT_LANCZOS?c[i].bytes:c[i].kind==EXBOX_GPU_NXE_REFLECTION?sizeof(ExboxGpuNxeReflection):(((c[i].kind>=EXBOX_GPU_TEXTURE&&c[i].kind<=EXBOX_GPU_NINE)||c[i].kind==EXBOX_GPU_VERTICAL_STRIP||c[i].kind==EXBOX_GPU_VIDEO_Q8)?(size_t)c[i].pitch*c[i].height:0);g->residency.spans[i]=(SourceSpan){c[i].pixels,(uint32_t)span,0,0,leases?leases[i]:0};}
 if(!source_apply(&g->residency,g->mapped,EXBOX_GPU2D_SOURCE_BYTES,n))return 0;uint32_t slot_w=0,slot_h=0;
 for(size_t i=0;i<n;++i){const ExboxGpuCommand* a=c+i;Push* p=g->commands+i;
  if(a->kind==EXBOX_GPU_BEGIN_SLOT){slot_w=(uint32_t)a->dst.w;slot_h=(uint32_t)a->dst.h;}
  uint32_t target_w=a->transform[11]==2?slot_w:g->width,target_h=a->transform[11]==2?slot_h:g->height;
  p->rect[0]=a->dst.x;p->rect[1]=a->dst.y;p->rect[2]=a->dst.w;p->rect[3]=a->dst.h;
  p->clip[0]=maxi(0,maxi(a->dst.x,a->clip.x));p->clip[1]=maxi(0,maxi(a->dst.y,a->clip.y));
  p->clip[2]=mini(target_w,mini(a->dst.x+a->dst.w,a->clip.x+a->clip.w));p->clip[3]=mini(target_h,mini(a->dst.y+a->dst.h,a->clip.y+a->clip.h));
  p->colors[0]=a->top;p->colors[1]=a->bottom;p->colors[2]=a->opacity;p->colors[3]=(uint32_t)a->kind;
  p->extent[0]=target_w;p->extent[1]=target_h;p->extent[2]=a->blend;p->extent[3]=a->fill_type;memcpy(p->transform,a->transform,sizeof p->transform);
  memset(p->texture,0,sizeof p->texture);
  if(a->kind==EXBOX_GPU_AFFINE&&a->blend==10){
   uint32_t source_w=a->bottom>>16,source_h=a->bottom&65535u;
   int l=p->clip[0],t=p->clip[1],r=p->clip[2],b=p->clip[3];
   if(r>l&&b>t){double sx=(double)a->dst.w/source_w,sy=(double)a->dst.h/source_h;
    double x0=(l-a->dst.x)/sx,y0=(t-a->dst.y)/sy,x1=source_w-((a->dst.x+a->dst.w)-r)/sx,y1=source_h-((a->dst.y+a->dst.h)-b)/sy;
    uint32_t ox=(uint32_t)floor(x0+.5),oy=(uint32_t)floor(y0+.5),sw=(uint32_t)floor(x1-x0+.5),sh=(uint32_t)floor(y1-y0+.5);
    if(ox+sw>source_w)sw=source_w-ox;if(oy+sh>source_h)sh=source_h-oy;
    p->transform[0]=(float)(((uint64_t)sw<<16)/(uint32_t)(r-l));p->transform[4]=(float)(((uint64_t)sh<<16)/(uint32_t)(b-t));
    p->transform[2]=(float)l;p->transform[5]=(float)t;p->transform[6]=a->transform[6]+ox;p->transform[7]=a->transform[7]+oy;
    if(!sw||!sh){p->clip[2]=l;p->clip[3]=t;}
   }
  }
  if(a->kind==EXBOX_GPU_DIRECT_LANCZOS){p->texture[0]=g->residency.spans[i].offset/4;p->texture[1]=a->height;}
  if(a->kind==EXBOX_GPU_FILTERED){p->texture[1]=a->width;p->texture[2]=a->height;p->texture[3]=a->pitch/4;}
  if(a->kind==EXBOX_GPU_EXTERNAL){p->texture[1]=g->external_width;p->texture[2]=g->external_height;p->texture[3]=g->external_pitch/4;}
  if(a->kind==EXBOX_GPU_NXE_REFLECTION){p->texture[0]=g->residency.spans[i].offset/4;p->texture[1]=g->external_width;p->texture[2]=g->external_height;p->texture[3]=g->external_pitch/4;}
  if(a->kind==EXBOX_GPU_BEGIN_SLOT){p->texture[1]=g->width;p->texture[2]=g->height;p->texture[3]=g->width;}
  if(a->kind==EXBOX_GPU_COMPOSITE_SLOT){
   /* SDL upper-scaled blit clips in double precision, rounds source crop,
    * then runs nearest scaling on the clipped rectangles (not original UVs). */
   int l=p->clip[0],t=p->clip[1],r=p->clip[2],b=p->clip[3];
   if(r>l&&b>t){double sx=(double)a->dst.w/slot_w,sy=(double)a->dst.h/slot_h;
    double x0=(l-a->dst.x)/sx,y0=(t-a->dst.y)/sy,x1=slot_w-((a->dst.x+a->dst.w)-r)/sx,y1=slot_h-((a->dst.y+a->dst.h)-b)/sy;
    uint32_t ox=(uint32_t)floor(x0+.5),oy=(uint32_t)floor(y0+.5),sw=(uint32_t)floor(x1-x0+.5),sh=(uint32_t)floor(y1-y0+.5);
    if(ox+sw>slot_w)sw=slot_w-ox;if(oy+sh>slot_h)sh=slot_h-oy;
    p->texture[0]=oy*slot_w+ox;p->texture[1]=sw;p->texture[2]=sh;p->texture[3]=slot_w;
    p->rect[0]=l;p->rect[1]=t;p->rect[2]=r-l;p->rect[3]=b-t;if(!sw||!sh){p->clip[2]=l;p->clip[3]=t;}
   }
  }
  if(a->kind==EXBOX_GPU_SDL_GROUP){
   uint32_t source_x=(uint32_t)a->transform[0],source_y=(uint32_t)a->transform[1],source_w=(uint32_t)a->transform[2],source_h=(uint32_t)a->transform[3];
   p->transform[4]=(a->dst.w==(int32_t)source_w&&a->dst.h==(int32_t)source_h)?1.f:0.f;
   int l=p->clip[0],t=p->clip[1],r=p->clip[2],b=p->clip[3];
   if(r>l&&b>t){double sx=(double)a->dst.w/source_w,sy=(double)a->dst.h/source_h;
    double x0=(l-a->dst.x)/sx,y0=(t-a->dst.y)/sy,x1=source_w-((a->dst.x+a->dst.w)-r)/sx,y1=source_h-((a->dst.y+a->dst.h)-b)/sy;
    uint32_t ox=(uint32_t)floor(x0+.5),oy=(uint32_t)floor(y0+.5),sw=(uint32_t)floor(x1-x0+.5),sh=(uint32_t)floor(y1-y0+.5);
    if(ox+sw>source_w)sw=source_w-ox;if(oy+sh>source_h)sh=source_h-oy;
    p->texture[0]=(source_y+oy)*g->width+source_x+ox;p->texture[1]=sw;p->texture[2]=sh;p->texture[3]=g->width;
    p->rect[0]=l;p->rect[1]=t;p->rect[2]=r-l;p->rect[3]=b-t;if(!sw||!sh){p->clip[2]=l;p->clip[3]=t;}
   }
  }
  if(a->kind==EXBOX_GPU_GUIDE_SAVE_BLACK||a->kind==EXBOX_GPU_GUIDE_RECOVER){p->texture[1]=a->width;p->texture[2]=a->height;p->texture[3]=a->width;}
  if(a->kind==EXBOX_GPU_GROUP){p->texture[1]=g->width;p->texture[2]=g->height;p->texture[3]=g->width;}
  if(((a->kind>=EXBOX_GPU_TEXTURE && a->kind<=EXBOX_GPU_NINE)||a->kind==EXBOX_GPU_VERTICAL_STRIP||a->kind==EXBOX_GPU_VIDEO_Q8)){p->texture[0]=g->residency.spans[i].offset/4;p->texture[1]=a->width;p->texture[2]=a->height;p->texture[3]=a->pitch/4;}
 }
 g->count=n;return 1;
}
int exbox_gpu2d_record_range(ExboxGpu2D* g,VkCommandBuffer cb,size_t first,size_t count,int begin) {
 if(!g||!cb||first>g->count||count>g->count-first)return 0;
 if(begin){if(first||g->record_open||g->filtered_pending)return 0;}
 else if(!g->record_open||g->record_command!=cb||first!=g->record_cursor)return 0;
 if(begin){g->record_cursor=0;g->record_command=cb;g->record_open=1;}
 VkDescriptorSet active=g->set;
 if(begin){
 VkMemoryBarrier prior={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&prior,0,0,0,0);
 g->CmdFillBuffer(cb,g->output,0,GPU_BYTES,0xff000000u);
 }
 VkMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,0,0,0);
 VkMemoryBarrier producer={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&producer,0,0,0,0);
 g->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipeline);g->CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->layout,0,1,&g->set,0,0);
 VkMemoryBarrier ordered={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 for(size_t i=first;i<first+count;++i){Push* p=g->commands+i;if(p->clip[2]<=p->clip[0]||p->clip[3]<=p->clip[1])continue;
  VkDescriptorSet needed=p->colors[3]==EXBOX_GPU_FILTERED?g->filtered_sets[(unsigned)p->transform[9]]:g->set;if(needed!=active){g->CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->layout,0,1,&needed,0,0);active=needed;}if(p->colors[3]==EXBOX_GPU_FILTERED)g->filtered_pending=1;
  if(p->colors[3]==EXBOX_GPU_NXE_REFLECTION||p->colors[3]==EXBOX_GPU_BEGIN_SLOT){
   VkMemoryBarrier snapshot={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
   g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&snapshot,0,0,0,0);VkBufferCopy cp={0,0,GPU_BYTES};g->CmdCopyBuffer(cb,g->output,g->layer,1,&cp);
   snapshot.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;snapshot.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&snapshot,0,0,0,0);
  }
  if(p->colors[3]==EXBOX_GPU_DIRECT_LANCZOS){
   const uint32_t sh=p->texture[1],capacity=(uint32_t)(((uint64_t)g->height*2*1025)/(4u*sh));
   if(capacity){
    const uint32_t tile=capacity<512?capacity:512;int first=p->clip[0]-p->rect[0],end=p->clip[2]-p->rect[0];
    for(int x=first;x<end;){uint32_t width=(uint32_t)(end-x)<tile?(uint32_t)(end-x):tile;Push part=*p;part.texture[1]=(uint32_t)x;part.texture[2]=width;part.texture[3]=1;part.colors[3]|=512u;
     g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof part,&part);g->CmdDispatch(cb,(width+7)/8,(sh+7)/8,1);
     g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ordered,0,0,0,0);
     part.colors[3]=EXBOX_GPU_DIRECT_LANCZOS;part.clip[0]=p->rect[0]+x;part.clip[2]=part.clip[0]+(int)width;
     g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof part,&part);g->CmdDispatch(cb,(width+7)/8,(uint32_t)(part.clip[3]-part.clip[1]+7)/8,1);
     VkMemoryBarrier reuse=ordered;reuse.srcAccessMask|=VK_ACCESS_SHADER_READ_BIT;g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&reuse,0,0,0,0);x+=(int)width;
    }continue;
   }
  }
  if(p->colors[3]==EXBOX_GPU_PATH || p->colors[3]==EXBOX_GPU_BITMAP_PATH){uint32_t kind=p->colors[3];p->colors[3]|=256;g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *p,p);g->CmdDispatch(cb,((uint32_t)(p->clip[3]-p->clip[1])*2u+63u)/64u,1,1);g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ordered,0,0,0,0);p->colors[3]=kind;}
  g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *p,p);
  g->CmdDispatch(cb,(uint32_t)(p->clip[2]-p->clip[0]+7)/8,(uint32_t)(p->clip[3]-p->clip[1]+7)/8,1);
  g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ordered,0,0,0,0);
 }
 VkMemoryBarrier transfer={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&transfer,0,0,0,0);
 g->record_cursor=first+count;if(g->record_cursor==g->count)g->record_open=0;return 1;
}
void exbox_gpu2d_record(ExboxGpu2D* g,VkCommandBuffer cb) {
 if(g)(void)exbox_gpu2d_record_range(g,cb,0,g->count,1);
}
VkBuffer exbox_gpu2d_output(const ExboxGpu2D* g){return g?g->output:VK_NULL_HANDLE;}

ExboxGpu2D* exbox_gpu2d_create(uint32_t w,uint32_t h,VkDevice d,const VkPhysicalDeviceMemoryProperties*m,const VkPhysicalDeviceProperties*p,VkQueueFlags f,PFN_vkGetDeviceProcAddr get){return exbox_gpu2d_create_with_budget(w,h,d,m,p,f,get,NULL);}
size_t exbox_gpu2d_allocated(const ExboxGpu2D*g){return g?g->allocated:0;}
int exbox_gpu2d_prepare(ExboxGpu2D*g,const ExboxGpuCommand*c,size_t n){return prepare_packet(g,c,n,0,0,NULL);}
int exbox_gpu2d_prepare_external(ExboxGpu2D*g,const ExboxGpuCommand*c,size_t n,uint64_t generation){return prepare_packet(g,c,n,1,generation,NULL);}
int exbox_gpu2d_bind_external(ExboxGpu2D*g,VkBuffer b,size_t bytes,uint32_t w,uint32_t h,uint32_t pitch,uint64_t generation){
 if(!g||g->filtered_pending||g->record_open||!b||b==g->output||b==g->layer||b==g->scratch||b==g->source||b==g->slot||b==g->guide||!w||!h||w>8192||h>8192||pitch%4||pitch<w*4u||(uint64_t)pitch*h>bytes||bytes>EXBOX_GPU2D_SOURCE_BYTES)return 0;
 VkDescriptorBufferInfo info={b,0,(VkDeviceSize)pitch*h};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->set,.dstBinding=4,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS+1;j++){write.dstSet=j?g->filtered_sets[j-1]:g->set;g->UpdateDescriptorSets(g->device,1,&write,0,NULL);}
 g->external=b;g->external_bytes=bytes;g->external_width=w;g->external_height=h;g->external_pitch=pitch;g->external_generation=generation;g->count=0;return 1;
}

int exbox_gpu2d_enable_custom_slot(ExboxGpu2D*g){
 if(g&&(g->filtered_pending||g->record_open))return 0;
 if(!g)return 0;if(g->slot_mem)return 1;const size_t before=g->allocated;
 if(!buffer(g,840u*640u*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->slot,&g->slot_mem)){
  if(g->guide)g->DestroyBuffer(g->device,g->guide,0);
 if(g->guide_mem){g->FreeMemory(g->device,g->guide_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[5]);}
 if(g->slot)g->DestroyBuffer(g->device,g->slot,0);g->slot=VK_NULL_HANDLE;
  if(g->slot_mem){g->FreeMemory(g->device,g->slot_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[4]);}g->slot_mem=VK_NULL_HANDLE;g->allocated=before;return 0;
 }
 VkDescriptorBufferInfo bi={g->slot,0,840u*640u*4u};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->set,.dstBinding=5,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS+1;j++){w.dstSet=j?g->filtered_sets[j-1]:g->set;g->UpdateDescriptorSets(g->device,1,&w,0,NULL);}return 1;
}


int exbox_gpu2d_source_epoch(ExboxGpu2D*g,uint64_t epoch){if(!g||g->filtered_pending||g->record_open)return 0;if(g->residency.stats.epoch!=epoch){source_epoch(&g->residency,epoch);g->count=0;}return 1;}
int exbox_gpu2d_source_stats(const ExboxGpu2D*g,ExboxGpuSourceStats*out){if(!g||!out)return 0;*out=g->residency.stats;return 1;}
static atomic_uint_fast64_t source_ids=ATOMIC_VAR_INIT(1);
static uint64_t source_new_id(void){uint_fast64_t value=atomic_load(&source_ids);while(value!=UINT64_MAX){if(atomic_compare_exchange_weak(&source_ids,&value,value+1))return value;}return 0;}
int exbox_gpu2d_source_publish(ExboxGpu2D*g,ExboxGpuSourceKey key,const void*bytes,size_t size,ExboxGpuSourceLease*out){
 if(!g||g->filtered_pending||g->record_open||!out||!key.producer_id||!bytes||!size||size%4||size>EXBOX_GPU2D_SOURCE_BYTES)return 0;
 uintptr_t p=(uintptr_t)bytes,m=(uintptr_t)g->mapped;if(p>UINTPTR_MAX-size||m>UINTPTR_MAX-EXBOX_GPU2D_SOURCE_BYTES||(p<m+EXBOX_GPU2D_SOURCE_BYTES&&p+size>m))return 0;
 SourceCache*c=&g->residency;
 for(size_t j=0;j<c->count;j++){SourceEntry*e=c->live+j;if(e->lease&&e->key.producer_id==key.producer_id&&e->key.owner_epoch==key.owner_epoch&&e->key.generation==key.generation){if(e->bytes!=size||memcmp((char*)g->mapped+e->offset,bytes,size))return 0;*out=e->lease;return 1;}}
 memcpy(c->plan,c->live,c->count*sizeof(SourceEntry));c->planned=c->count;uint32_t offset=0;size_t where=0,evicted=0;
 while(!source_gap(c,(uint32_t)size,EXBOX_GPU2D_SOURCE_BYTES,&offset,&where)||c->planned==EXBOX_SOURCE_ENTRIES){size_t victim=c->planned;for(size_t j=0;j<c->planned;j++)if(!c->plan[j].lease&&(victim==c->planned||c->plan[j].age<c->plan[victim].age))victim=j;if(victim==c->planned)return 0;source_remove(c,victim);evicted++;}
 uint64_t id=source_new_id();if(!id)return 0;
 memmove(c->plan+where+1,c->plan+where,(c->planned-where)*sizeof(SourceEntry));c->plan[where]=(SourceEntry){.offset=offset,.bytes=(uint32_t)size,.lease=id,.key=key};c->planned++;
 memcpy((char*)g->mapped+offset,bytes,size);memcpy(c->live,c->plan,c->planned*sizeof(SourceEntry));c->count=c->planned;c->stats.publications++;c->stats.published_bytes+=size;c->stats.evictions+=evicted;source_update_size(c);g->count=0;*out=id;return 1;
}
int exbox_gpu2d_source_retire(ExboxGpu2D*g,ExboxGpuSourceLease lease){if(!g||g->filtered_pending||g->record_open)return 0;SourceCache*c=&g->residency;SourceEntry*e=source_find(c,lease);if(!e)return 0;size_t j=(size_t)(e-c->live);memmove(c->live+j,c->live+j+1,(c->count-j-1)*sizeof(SourceEntry));c->count--;c->stats.retirements++;source_update_size(c);g->count=0;return 1;}
int exbox_gpu2d_prepare_leased(ExboxGpu2D*g,const ExboxGpuCommand*c,size_t n,const ExboxGpuSourceLease*leases,uint64_t external_generation){
 if(!g||g->filtered_pending||g->record_open||n>EXBOX_GPU2D_MAX_COMMANDS||(n&&(!c||!leases)))return 0;
 for(size_t i=0;i<n;i++){g->resolved[i]=c[i];if(leases[i]){SourceEntry*e=source_find(&g->residency,leases[i]);if(!e||e->bytes!=c[i].bytes)return 0;g->resolved[i].pixels=(char*)g->mapped+e->offset;}}
 return prepare_packet(g,g->resolved,n,1,external_generation,leases);
}

int exbox_gpu2d_bind_filtered(ExboxGpu2D*g,unsigned slot,VkBuffer b,size_t bytes,uint32_t w,uint32_t h,uint32_t pitch,uint64_t generation){
 if(!g||g->filtered_pending||g->record_open||slot>=EXBOX_FILTERED_MAX_SLOTS||!b||!generation||!w||!h||w>8192||h>8192||pitch%4||pitch<w*4u||(uint64_t)pitch*h>bytes||bytes>EXBOX_GPU2D_SOURCE_BYTES||b==g->output||b==g->source||b==g->scratch||b==g->layer||b==g->slot||b==g->guide)return 0;
 VkDescriptorBufferInfo info={b,0,(VkDeviceSize)pitch*h};VkWriteDescriptorSet wr={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->filtered_sets[slot],.dstBinding=6,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};g->UpdateDescriptorSets(g->device,1,&wr,0,NULL);g->filtered[slot]=(Filtered){b,bytes,w,h,pitch,generation};g->count=0;return 1;
}
void exbox_gpu2d_complete_after_wait(ExboxGpu2D*g){if(g){g->filtered_pending=0;g->record_open=0;g->record_command=VK_NULL_HANDLE;g->record_cursor=0;}}
int exbox_gpu2d_filtered_pending(const ExboxGpu2D*g){return g?g->filtered_pending:0;}

int exbox_gpu2d_unbind_filtered(ExboxGpu2D*g,unsigned slot){if(!g||g->filtered_pending||g->record_open||slot>=EXBOX_FILTERED_MAX_SLOTS)return 0;VkDescriptorBufferInfo info={g->source,0,EXBOX_GPU2D_SOURCE_BYTES};VkWriteDescriptorSet wr={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->filtered_sets[slot],.dstBinding=6,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};g->UpdateDescriptorSets(g->device,1,&wr,0,NULL);memset(g->filtered+slot,0,sizeof g->filtered[slot]);g->count=0;return 1;}

unsigned exbox_gpu2d_filtered_capacity(void){return EXBOX_FILTERED_MAX_SLOTS;}

int exbox_gpu2d_enable_guide(ExboxGpu2D*g){
 if(!g||g->filtered_pending||g->record_open)return 0;if(g->guide)return 1;size_t before=g->allocated;
 if(!buffer(g,3888000u*4u,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->guide,&g->guide_mem)){
  if(g->guide)g->DestroyBuffer(g->device,g->guide,0);g->guide=VK_NULL_HANDLE;
  if(g->guide_mem){g->FreeMemory(g->device,g->guide_mem,0);exbox_gpu_budget_release(g->budget,&g->reservations[5]);}g->guide_mem=VK_NULL_HANDLE;g->allocated=before;return 0;
 }
 VkDescriptorBufferInfo bi={g->guide,0,3888000u*4u};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->set,.dstBinding=7,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};for(unsigned j=0;j<EXBOX_FILTERED_MAX_SLOTS+1;j++){w.dstSet=j?g->filtered_sets[j-1]:g->set;g->UpdateDescriptorSets(g->device,1,&w,0,NULL);}return 1;
}
