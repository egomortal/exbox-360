#include "avatar_gpu.h"
#include "gpu_budget.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "avatar_spv.h"
#define GPU_BYTES ((size_t)g->width*g->height*4u)
#define FUNCTIONS(X) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdFillBuffer) X(CmdPipelineBarrier) \
 X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
typedef struct {uint32_t phase,width,height,pad;} Push;
struct ExboxAvatarGpu {
 VkDevice device; VkPhysicalDeviceMemoryProperties memory; uint32_t width,height; size_t allocated;
#define FIELD(n) PFN_vk##n n;
 FUNCTIONS(FIELD)
#undef FIELD
 VkBuffer output,source,scratch; VkDeviceMemory output_mem,source_mem,scratch_mem; void* mapped;
 VkDescriptorSetLayout descriptors; VkPipelineLayout layout; VkPipeline pipeline;
 VkDescriptorPool pool; VkDescriptorSet set;
 VkDescriptorSet actor_sets[4];size_t actor_count,source_alignment;
 uint32_t actor_vertices[4],actor_triangles[4];
 uint32_t vertices,triangles; size_t source_capacity,scratch_capacity; VkDeviceSize storage_limit; ExboxGpuBudget local_budget,*budget; ExboxGpuReservation output_res,source_res,scratch_res;
};
static int buffer(ExboxAvatarGpu* g,size_t bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,VkBuffer* b,VkDeviceMemory* m,ExboxGpuReservation* reservation) {
 VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 if(g->CreateBuffer(g->device,&ci,0,b)!=VK_SUCCESS)return 0;
 VkMemoryRequirements req;g->GetBufferMemoryRequirements(g->device,*b,&req);
 if(!exbox_gpu_budget_reserve(g->budget,reservation,req.size))return 0;
 uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<g->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (g->memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
 if(type==UINT32_MAX)return 0;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 if(g->AllocateMemory(g->device,&ai,0,m)!=VK_SUCCESS){exbox_gpu_budget_release(g->budget,reservation);return 0;}return g->BindBufferMemory(g->device,*b,*m,0)==VK_SUCCESS;
}
void exbox_avatar_destroy(ExboxAvatarGpu* g) {
 if(!g)return;
 if(g->pipeline)g->DestroyPipeline(g->device,g->pipeline,0);
 if(g->pool)g->DestroyDescriptorPool(g->device,g->pool,0);
 if(g->layout)g->DestroyPipelineLayout(g->device,g->layout,0);
 if(g->descriptors)g->DestroyDescriptorSetLayout(g->device,g->descriptors,0);
 if(g->mapped)g->UnmapMemory(g->device,g->source_mem);
 if(g->source)g->DestroyBuffer(g->device,g->source,0);
 if(g->scratch)g->DestroyBuffer(g->device,g->scratch,0);
 if(g->scratch_mem){g->FreeMemory(g->device,g->scratch_mem,0);exbox_gpu_budget_release(g->budget,&g->scratch_res);}
 if(g->output)g->DestroyBuffer(g->device,g->output,0);
 if(g->source_mem){g->FreeMemory(g->device,g->source_mem,0);exbox_gpu_budget_release(g->budget,&g->source_res);}
 if(g->output_mem){g->FreeMemory(g->device,g->output_mem,0);exbox_gpu_budget_release(g->budget,&g->output_res);}
 free(g);
}
ExboxAvatarGpu* exbox_avatar_create_with_budget(uint32_t width,uint32_t height,VkDevice d,const VkPhysicalDeviceMemoryProperties* memory,const VkPhysicalDeviceProperties* props,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get,ExboxGpuBudget* budget) {
 if(!width||!height||width>1200||height>1880||!d||!memory||!get||!props||!(flags&VK_QUEUE_COMPUTE_BIT) ||
 props->limits.maxStorageBufferRange<4096 || props->limits.maxStorageBufferRange<(size_t)width*height*4 ||
 props->limits.maxPushConstantsSize<sizeof(Push) || props->limits.maxComputeWorkGroupInvocations<64 ||
 props->limits.maxComputeWorkGroupSize[0]<64 || props->limits.maxComputeWorkGroupSize[1]<1 ||
 props->limits.maxComputeWorkGroupCount[0]<((width*height+63)/64) || props->limits.maxComputeWorkGroupCount[0]<2048 || props->limits.maxComputeWorkGroupCount[1]<1)return 0;
 ExboxAvatarGpu* g=calloc(1,sizeof *g);if(!g)return 0;g->device=d;g->memory=*memory;g->width=width;g->height=height;g->local_budget=(ExboxGpuBudget)EXBOX_GPU_BUDGET_INIT;g->budget=budget?budget:&g->local_budget;g->storage_limit=props->limits.maxStorageBufferRange;g->source_capacity=g->scratch_capacity=4096;g->source_alignment=props->limits.minStorageBufferOffsetAlignment?props->limits.minStorageBufferOffsetAlignment:1;
#define LOAD(n) g->n=(PFN_vk##n)get(d,"vk" #n);if(!g->n){free(g);return 0;}
 FUNCTIONS(LOAD)
#undef LOAD
 if(!buffer(g,GPU_BYTES,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->output,&g->output_mem,&g->output_res) ||
 !buffer(g,4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&g->source,&g->source_mem,&g->source_res))goto fail;
 if(!buffer(g,4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&g->scratch,&g->scratch_mem,&g->scratch_res))goto fail;
 if(g->MapMemory(d,g->source_mem,0,4096,0,&g->mapped)!=VK_SUCCESS)goto fail;
 VkDescriptorSetLayoutBinding bindings[3]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
 bindings[2]=bindings[1];bindings[2].binding=2;
 VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
 if(g->CreateDescriptorSetLayout(d,&ds,0,&g->descriptors)!=VK_SUCCESS)goto fail;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
 VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&g->descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
 if(g->CreatePipelineLayout(d,&li,0,&g->layout)!=VK_SUCCESS)goto fail;
 VkShaderModule shader=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof avatar_spv,.pCode=avatar_spv};
 if(g->CreateShaderModule(d,&sm,0,&shader)!=VK_SUCCESS)goto fail;
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=g->layout};
 VkResult rc=g->CreateComputePipelines(d,VK_NULL_HANDLE,1,&pi,0,&g->pipeline);g->DestroyShaderModule(d,shader,0);if(rc!=VK_SUCCESS)goto fail;
 VkDescriptorPoolSize sz={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,12};
 VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=4,.poolSizeCount=1,.pPoolSizes=&sz};
 if(g->CreateDescriptorPool(d,&pool,0,&g->pool)!=VK_SUCCESS)goto fail;
 VkDescriptorSetLayout actor_layouts[4]={g->descriptors,g->descriptors,g->descriptors,g->descriptors};
 VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=g->pool,.descriptorSetCount=4,.pSetLayouts=actor_layouts};
 if(g->AllocateDescriptorSets(d,&sa,g->actor_sets)!=VK_SUCCESS)goto fail;g->set=g->actor_sets[0];
 VkDescriptorBufferInfo bi[3]={{g->output,0,GPU_BYTES},{g->source,0,4096},{g->scratch,0,4096}};
 VkWriteDescriptorSet wr[3];memset(wr,0,sizeof wr);
 for(int i=0;i<3;++i){wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstSet=g->set;wr[i].dstBinding=(uint32_t)i;wr[i].descriptorCount=1;wr[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;wr[i].pBufferInfo=bi+i;}
 for(unsigned actor=0;actor<4;++actor){for(unsigned i=0;i<3;++i)wr[i].dstSet=g->actor_sets[actor];g->UpdateDescriptorSets(d,3,wr,0,0);}return g;
 fail:exbox_avatar_destroy(g);return 0;
}

ExboxAvatarGpu* exbox_avatar_create(uint32_t w,uint32_t h,VkDevice d,const VkPhysicalDeviceMemoryProperties*m,const VkPhysicalDeviceProperties*p,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get){return exbox_avatar_create_with_budget(w,h,d,m,p,flags,get,0);}
static int resize_storage(ExboxAvatarGpu*g,size_t source,size_t scratch){
 if(source>g->storage_limit||scratch>g->storage_limit)return 0;
 VkBuffer sb=VK_NULL_HANDLE,tb=VK_NULL_HANDLE;VkDeviceMemory sm=VK_NULL_HANDLE,tm=VK_NULL_HANDLE;void*mapped=0;
 ExboxGpuReservation sr=EXBOX_GPU_RESERVATION_INIT,tr=EXBOX_GPU_RESERVATION_INIT;
 const int grow_source=source>g->source_capacity,grow_scratch=scratch>g->scratch_capacity;
 /* Stage both allocations. Charge the transient peak; preserve old packet on failure. */
 if(grow_source){if(!buffer(g,source,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&sb,&sm,&sr))goto fail;if(g->MapMemory(g->device,sm,0,source,0,&mapped)!=VK_SUCCESS)goto fail;}
 if(grow_scratch&&!buffer(g,scratch,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&tb,&tm,&tr))goto fail;
 if(grow_source){
  if(g->mapped)g->UnmapMemory(g->device,g->source_mem);
  if(g->source)g->DestroyBuffer(g->device,g->source,0);
  if(g->source_mem){g->FreeMemory(g->device,g->source_mem,0);exbox_gpu_budget_release(g->budget,&g->source_res);}
  g->source=sb;g->source_mem=sm;g->source_res=sr;sr=(ExboxGpuReservation)EXBOX_GPU_RESERVATION_INIT;g->mapped=mapped;g->source_capacity=source;
 }
 if(grow_scratch){
  if(g->scratch)g->DestroyBuffer(g->device,g->scratch,0);
  if(g->scratch_mem){g->FreeMemory(g->device,g->scratch_mem,0);exbox_gpu_budget_release(g->budget,&g->scratch_res);}
  g->scratch=tb;g->scratch_mem=tm;g->scratch_res=tr;tr=(ExboxGpuReservation)EXBOX_GPU_RESERVATION_INIT;g->scratch_capacity=scratch;
 }
 VkDescriptorBufferInfo bi[2]={{g->source,0,g->source_capacity},{g->scratch,0,g->scratch_capacity}};VkWriteDescriptorSet wr[2];memset(wr,0,sizeof wr);
 for(int i=0;i<2;i++){wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstBinding=(uint32_t)i+1;wr[i].descriptorCount=1;wr[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;wr[i].pBufferInfo=bi+i;}for(unsigned actor=0;actor<4;++actor){for(unsigned i=0;i<2;++i)wr[i].dstSet=g->actor_sets[actor];g->UpdateDescriptorSets(g->device,2,wr,0,0);}return 1;
 fail:
 if(mapped)g->UnmapMemory(g->device,sm);
 if(sb)g->DestroyBuffer(g->device,sb,0);
 if(tb)g->DestroyBuffer(g->device,tb,0);
 if(sm){g->FreeMemory(g->device,sm,0);exbox_gpu_budget_release(g->budget,&sr);}
 if(tm){g->FreeMemory(g->device,tm,0);exbox_gpu_budget_release(g->budget,&tr);}
 return 0;
}
static int validate_packet(const uint32_t*data,size_t words){
 if(!data||words<32||words>32u*1024u*1024u/4)return 0;
 uint32_t nv=data[0],nt=data[1],vo=data[2],to=data[3],draw=data[4],nd=data[5];
 if(!nv||nv>65536||nt>131072||nd>256||!nd||(uint64_t)vo+nv*12>words||(uint64_t)to+nt*4>words||(uint64_t)draw+nd*16>words)return 0;
 for(size_t j=8;j<=12;j++){float f;memcpy(&f,data+j,4);if(!__builtin_isfinite(f))return 0;}
 for(uint32_t i=0;i<nd;i++){const uint32_t*d=data+draw+i*16;if(!d[1]||!d[2]||(d[1]&(d[1]-1))||(d[2]&(d[2]-1))||(uint64_t)d[0]+(uint64_t)d[1]*d[2]>words||(d[3]!=0&&d[3]!=4)||d[4]>1)return 0;for(uint32_t k=5;k<=6;k++){float f;memcpy(&f,d+k,4);if(!__builtin_isfinite(f))return 0;}}
 for(uint32_t i=0;i<nv;i++){const uint32_t*v=data+vo+i*12;if(v[11]>=nd)return 0;for(uint32_t k=0;k<8;k++){float f;memcpy(&f,v+k,4);if(!__builtin_isfinite(f))return 0;}for(uint32_t k=0;k<4;k++)if((v[9]>>(k*8))&255){uint64_t matrix=(uint64_t)v[10]+((v[8]>>(k*8))&255)*12;if(matrix+12>words)return 0;for(uint32_t j=0;j<12;j++){float f;memcpy(&f,data+matrix+j,4);if(!__builtin_isfinite(f))return 0;}}}
 for(uint32_t i=0;i<nt;i++){const uint32_t*t=data+to+i*4;if(t[0]>=nv||t[1]>=nv||t[2]>=nv||t[3]>=nd)return 0;}
 
 return 1;
}
int exbox_avatar_prepare_group(ExboxAvatarGpu*g,const ExboxAvatarInput*inputs,size_t count){
 if(!g||!inputs||!count||count>4||!g->source_alignment||g->source_alignment>32u*1024u*1024u||(g->source_alignment&(g->source_alignment-1)))return 0;
 size_t offsets[4],used=0,scratch=0;
 const uint32_t tiles=((g->width*2+15)/16)*((g->height*2+15)/16);
 for(size_t i=0;i<count;++i){
  if(!validate_packet(inputs[i].words,inputs[i].word_count))return 0;
  offsets[i]=(used+g->source_alignment-1)&~(g->source_alignment-1);
  if(offsets[i]>32u*1024u*1024u||inputs[i].word_count*4>32u*1024u*1024u-offsets[i])return 0;
  used=offsets[i]+inputs[i].word_count*4;
  const uint32_t*data=inputs[i].words;size_t required=((size_t)data[0]*8+(size_t)data[1]*32+tiles*3+4+2097152)*4;
  if(required>scratch)scratch=required;
 }
 size_t source=(used+4095)&~(size_t)4095;if(!resize_storage(g,source,scratch))return 0;
 for(size_t i=0;i<count;++i){
  memcpy((char*)g->mapped+offsets[i],inputs[i].words,inputs[i].word_count*4);
  uint32_t*header=(uint32_t*)((char*)g->mapped+offsets[i]);uint32_t nv=header[0],nt=header[1];
  header[6]=nv*8;header[7]=nv*8+nt*32;header[13]=header[7]+tiles*3+4;header[14]=2097152;
  g->actor_vertices[i]=nv;g->actor_triangles[i]=nt;
  VkDescriptorBufferInfo info={g->source,offsets[i],inputs[i].word_count*4};VkWriteDescriptorSet wr={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->actor_sets[i],.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};g->UpdateDescriptorSets(g->device,1,&wr,0,0);
 }
 g->actor_count=count;g->vertices=g->actor_vertices[0];g->triangles=g->actor_triangles[0];return 1;
}
int exbox_avatar_prepare(ExboxAvatarGpu*g,const uint32_t*data,size_t words){const ExboxAvatarInput input={data,words};return exbox_avatar_prepare_group(g,&input,1);}
int exbox_avatar_record_actor(ExboxAvatarGpu*g,VkCommandBuffer cb,size_t actor){
 if(!g||!cb||actor>=g->actor_count)return 0;g->vertices=g->actor_vertices[actor];g->triangles=g->actor_triangles[actor];
 VkMemoryBarrier prior={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&prior,0,0,0,0);
 uint32_t tile_base=g->vertices*8+g->triangles*32,tiles=((g->width*2+15)/16)*((g->height*2+15)/16);g->CmdFillBuffer(cb,g->scratch,(VkDeviceSize)tile_base*4,(VkDeviceSize)(tiles*3+4)*4,0);
 VkMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,0,0,0);
 g->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipeline);g->CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->layout,0,1,&g->actor_sets[actor],0,0);
 VkMemoryBarrier ordered={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
 for(uint32_t phase=0;phase<5;phase++){Push p={phase,g->width,g->height,0};g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof p,&p);uint32_t n=phase==0?g->vertices:phase==1||phase==3?g->triangles:phase==2?1:g->width*g->height;g->CmdDispatch(cb,(n+63)/64,1,1);g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ordered,0,0,0,0);}
 VkMemoryBarrier transfer={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT};g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&transfer,0,0,0,0);
 return 1;
}
void exbox_avatar_record(ExboxAvatarGpu*g,VkCommandBuffer cb){(void)exbox_avatar_record_actor(g,cb,0);}
VkBuffer exbox_avatar_output(const ExboxAvatarGpu*g){return g?g->output:VK_NULL_HANDLE;}
uint64_t exbox_avatar_allocated(const ExboxAvatarGpu*g){return g?g->output_res.bytes+g->source_res.bytes+g->scratch_res.bytes:0;}
// Test telemetry is GPU-resident; only the host validation owner copies these four words.
VkBuffer exbox_avatar_statistics(const ExboxAvatarGpu*g,VkDeviceSize*offset){if(!g||!offset)return VK_NULL_HANDLE;uint32_t tiles=((g->width*2+15)/16)*((g->height*2+15)/16);*offset=(VkDeviceSize)(g->vertices*8+g->triangles*32+tiles*3)*4;return g->scratch;}
