#include "gpu2d.h"
#include <stdlib.h>
#include <string.h>
#include "gpu2d_spv.h"
#define GPU_BYTES (EXBOX_GPU2D_WIDTH*EXBOX_GPU2D_HEIGHT*4u)
#define FUNCTIONS(X) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdFillBuffer) X(CmdPipelineBarrier) \
 X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
typedef struct {int32_t rect[4],clip[4];uint32_t colors[4],texture[4];} Push;
struct ExboxGpu2D {
 VkDevice device; VkPhysicalDeviceMemoryProperties memory;
#define FIELD(n) PFN_vk##n n;
 FUNCTIONS(FIELD)
#undef FIELD
 VkBuffer output,source; VkDeviceMemory output_mem,source_mem; void* mapped;
 VkDescriptorSetLayout descriptors; VkPipelineLayout layout; VkPipeline pipeline;
 VkDescriptorPool pool; VkDescriptorSet set;
 Push commands[EXBOX_GPU2D_MAX_COMMANDS];size_t count;
};
int exbox_gpu2d_validate(const ExboxGpuCommand* c,size_t n,size_t* bytes) {
 size_t total=0;
 if(!bytes || n>EXBOX_GPU2D_MAX_COMMANDS || (n&&!c))return 0;
 for(size_t i=0;i<n;++i) {
  const ExboxGpuCommand* a=c+i;
  if(a->kind<EXBOX_GPU_RECT || a->kind>EXBOX_GPU_TEXTURE || a->opacity>255)return 0;
  const ExboxGpuRect* r[2]={&a->dst,&a->clip};
  for(int j=0;j<2;++j)if(r[j]->x < -16384 || r[j]->y < -16384 || r[j]->x>16384 || r[j]->y>16384 || r[j]->w<0 || r[j]->h<0 || r[j]->w>16384 || r[j]->h>16384)return 0;
  if(a->kind==EXBOX_GPU_TEXTURE) {
   if(!a->pixels || !a->width || !a->height || a->width>8192 || a->height>8192 || a->pitch%4 || a->pitch<a->width*4u)return 0;
   size_t span=(size_t)a->pitch*a->height;
   if(span>a->bytes || span>EXBOX_GPU2D_SOURCE_BYTES-total)return 0;
   total+=span;
  }
 }
 *bytes=total;return 1;
}
static int buffer(ExboxGpu2D* g,size_t bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,VkBuffer* b,VkDeviceMemory* m) {
 VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 if(g->CreateBuffer(g->device,&ci,0,b)!=VK_SUCCESS)return 0;
 VkMemoryRequirements req;g->GetBufferMemoryRequirements(g->device,*b,&req);
 uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<g->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (g->memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
 if(type==UINT32_MAX)return 0;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 return g->AllocateMemory(g->device,&ai,0,m)==VK_SUCCESS && g->BindBufferMemory(g->device,*b,*m,0)==VK_SUCCESS;
}
void exbox_gpu2d_destroy(ExboxGpu2D* g) {
 if(!g)return;
 if(g->pipeline)g->DestroyPipeline(g->device,g->pipeline,0);
 if(g->pool)g->DestroyDescriptorPool(g->device,g->pool,0);
 if(g->layout)g->DestroyPipelineLayout(g->device,g->layout,0);
 if(g->descriptors)g->DestroyDescriptorSetLayout(g->device,g->descriptors,0);
 if(g->mapped)g->UnmapMemory(g->device,g->source_mem);
 if(g->source)g->DestroyBuffer(g->device,g->source,0);
 if(g->output)g->DestroyBuffer(g->device,g->output,0);
 if(g->source_mem)g->FreeMemory(g->device,g->source_mem,0);
 if(g->output_mem)g->FreeMemory(g->device,g->output_mem,0);
 free(g);
}
ExboxGpu2D* exbox_gpu2d_create(VkDevice d,const VkPhysicalDeviceMemoryProperties* memory,const VkPhysicalDeviceProperties* props,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get) {
 if(!d||!memory||!get||!props||!(flags&VK_QUEUE_COMPUTE_BIT) ||
 props->limits.maxStorageBufferRange<EXBOX_GPU2D_SOURCE_BYTES ||
 props->limits.maxPushConstantsSize<sizeof(Push) || props->limits.maxComputeWorkGroupInvocations<64 ||
 props->limits.maxComputeWorkGroupSize[0]<8 || props->limits.maxComputeWorkGroupSize[1]<8 ||
 props->limits.maxComputeWorkGroupCount[0]<480 || props->limits.maxComputeWorkGroupCount[1]<270)return 0;
 ExboxGpu2D* g=calloc(1,sizeof *g);if(!g)return 0;g->device=d;g->memory=*memory;
#define LOAD(n) g->n=(PFN_vk##n)get(d,"vk" #n);if(!g->n){free(g);return 0;}
 FUNCTIONS(LOAD)
#undef LOAD
 if(!buffer(g,GPU_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->output,&g->output_mem) ||
 !buffer(g,EXBOX_GPU2D_SOURCE_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&g->source,&g->source_mem))goto fail;
 if(g->MapMemory(d,g->source_mem,0,EXBOX_GPU2D_SOURCE_BYTES,0,&g->mapped)!=VK_SUCCESS)goto fail;
 VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
 VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
 if(g->CreateDescriptorSetLayout(d,&ds,0,&g->descriptors)!=VK_SUCCESS)goto fail;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
 VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&g->descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(g->CreatePipelineLayout(d,&li,0,&g->layout)!=VK_SUCCESS)goto fail;
 VkShaderModule shader=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof gpu2d_spv,.pCode=gpu2d_spv};
 if(g->CreateShaderModule(d,&sm,0,&shader)!=VK_SUCCESS)goto fail;
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=g->layout};
 VkResult rc=g->CreateComputePipelines(d,VK_NULL_HANDLE,1,&pi,0,&g->pipeline);g->DestroyShaderModule(d,shader,0);if(rc!=VK_SUCCESS)goto fail;
 VkDescriptorPoolSize sz={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};
 VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&sz};
 if(g->CreateDescriptorPool(d,&pool,0,&g->pool)!=VK_SUCCESS)goto fail;
 VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=g->pool,.descriptorSetCount=1,.pSetLayouts=&g->descriptors};
 if(g->AllocateDescriptorSets(d,&sa,&g->set)!=VK_SUCCESS)goto fail;
 VkDescriptorBufferInfo bi[2]={{g->output,0,GPU_BYTES},{g->source,0,EXBOX_GPU2D_SOURCE_BYTES}};
 VkWriteDescriptorSet wr[2];memset(wr,0,sizeof wr);
 for(int i=0;i<2;++i){wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstSet=g->set;wr[i].dstBinding=(uint32_t)i;wr[i].descriptorCount=1;wr[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;wr[i].pBufferInfo=bi+i;}
 g->UpdateDescriptorSets(d,2,wr,0,0);return g;
 fail:exbox_gpu2d_destroy(g);return 0;
}
static int32_t maxi(int32_t a,int32_t b){return a>b?a:b;}
static int32_t mini(int32_t a,int32_t b){return a<b?a:b;}
int exbox_gpu2d_prepare(ExboxGpu2D* g,const ExboxGpuCommand* c,size_t n) {
 size_t bytes;if(!g||!exbox_gpu2d_validate(c,n,&bytes))return 0;
 size_t offset=0;
 for(size_t i=0;i<n;++i){const ExboxGpuCommand* a=c+i;Push* p=g->commands+i;
  p->rect[0]=a->dst.x;p->rect[1]=a->dst.y;p->rect[2]=a->dst.w;p->rect[3]=a->dst.h;
  p->clip[0]=maxi(0,maxi(a->dst.x,a->clip.x));p->clip[1]=maxi(0,maxi(a->dst.y,a->clip.y));
  p->clip[2]=mini(EXBOX_GPU2D_WIDTH,mini(a->dst.x+a->dst.w,a->clip.x+a->clip.w));p->clip[3]=mini(EXBOX_GPU2D_HEIGHT,mini(a->dst.y+a->dst.h,a->clip.y+a->clip.h));
  p->colors[0]=a->top;p->colors[1]=a->bottom;p->colors[2]=a->opacity;p->colors[3]=(uint32_t)a->kind;
  memset(p->texture,0,sizeof p->texture);
  if(a->kind==EXBOX_GPU_TEXTURE){size_t span=(size_t)a->pitch*a->height;memcpy((char*)g->mapped+offset,a->pixels,span);p->texture[0]=(uint32_t)(offset/4);p->texture[1]=a->width;p->texture[2]=a->height;p->texture[3]=a->pitch/4;offset+=span;}
 }
 g->count=n;return 1;
}
void exbox_gpu2d_record(ExboxGpu2D* g,VkCommandBuffer cb) {
 if(!g||!cb)return;
 VkMemoryBarrier prior={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&prior,0,0,0,0);
 g->CmdFillBuffer(cb,g->output,0,GPU_BYTES,0xff000000u);
 VkMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,0,0,0);
 g->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipeline);g->CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->layout,0,1,&g->set,0,0);
 VkMemoryBarrier ordered={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 for(size_t i=0;i<g->count;++i){Push* p=g->commands+i;if(p->clip[2]<=p->clip[0]||p->clip[3]<=p->clip[1])continue;
  g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *p,p);
  g->CmdDispatch(cb,(uint32_t)(p->clip[2]-p->clip[0]+7)/8,(uint32_t)(p->clip[3]-p->clip[1]+7)/8,1);
  g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ordered,0,0,0,0);
 }
 VkMemoryBarrier transfer={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
 g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&transfer,0,0,0,0);
}
VkBuffer exbox_gpu2d_output(const ExboxGpu2D* g){return g?g->output:VK_NULL_HANDLE;}
