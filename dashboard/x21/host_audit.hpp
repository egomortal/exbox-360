#pragma once
#ifndef __PROSPERO__
#include "session.hpp"
#include "source_client.h"
namespace x21hook {
struct HostAudit {
 ExboxSourceClient sources{};ExboxFilteredOwner*filtered=nullptr;
 ~HostAudit(){if(filtered&&!exbox_filtered_owner_destroy(filtered))(void)session.release();}
 static int unbind_filtered(void*context,unsigned slot){return exbox_gpu2d_unbind_filtered(static_cast<ExboxGpu2D*>(context),slot);}
 std::unique_ptr<Session> session;unsigned captures=0,matched=0;
 bool submit(const OwnedPacket&p){
  if(const char*n=std::getenv("NXE_GPU_CAPTURE_ACTORS"))if(p.actors.size()!=std::size_t(std::max(0,std::atoi(n))))return true;
  if(const char*n=std::getenv("NXE_GPU_CAPTURE_WAVE_ACTOR")){auto i=std::size_t(std::max(0,std::atoi(n)));if(i>=p.actors.size()||!p.actors[i].emote)return true;}
  if(const char*mode=std::getenv("NXE_GPU_AUDIT_OVERLAY"))if(p.overlay_mode!=std::atoi(mode))return true;
  if(const char*skip=std::getenv("NXE_GPU_AUDIT_SKIP"))if(matched++<unsigned(std::max(0,std::atoi(skip))))return true;
  if(const char*grid=std::getenv("NXE_GPU_CAPTURE_PICTURE_GRID"))if(*grid=='1'&&!p.pictures_grid)return true;
  if(const char*picture=std::getenv("NXE_GPU_CAPTURE_PICTURE"))if(*picture=='1'&&!p.fullscreen_picture)return true;
  if(const char*video=std::getenv("NXE_GPU_CAPTURE_VIDEO"))if(*video=='1'&&std::none_of(p.commands.begin(),p.commands.end(),[](const ExboxGpuCommand&c){return c.kind==EXBOX_GPU_VIDEO_Q8;}))return true;
  if(const char*toast=std::getenv("NXE_GPU_CAPTURE_TOAST"))if(*toast=='1'&&(p.overlay_mode!=0||std::none_of(p.commands.begin(),p.commands.end(),[](const ExboxGpuCommand&c){return c.kind==EXBOX_GPU_GUIDE_RECOVER;})))return true;
  if(exbox_filtered_capacity()!=EXBOX_FILTERED_MAX_SLOTS||exbox_gpu2d_filtered_capacity()!=EXBOX_FILTERED_MAX_SLOTS)throw std::runtime_error("filtered capacity binary mismatch");
  if(!session){session=std::make_unique<Session>();session->open();session->extent(unsigned(p.width),unsigned(p.height));}
  session->avatar_active=false;session->actor_indices.clear();session->actor_commands=p.commands.size();
  auto commands=p.commands;
  if(!p.filter_requests.empty()||filtered){
   if(!filtered)filtered=exbox_filtered_owner_create(session->device,&session->memory,&session->properties,session->flags,vkGetDeviceProcAddr,&session->budget,unbind_filtered,session->compositor);
   if(!filtered)throw std::runtime_error("filter owner creation refused");
   ExboxFilteredBinding bindings[EXBOX_FILTERED_MAX_SLOTS]{};size_t count=0;
   if(!exbox_filtered_prepare(filtered,p.filter_requests.data(),p.filter_requests.size(),bindings,EXBOX_FILTERED_MAX_SLOTS,&count)||count!=p.filter_requests.size()){std::string detail="filter preparation refused; budget="+std::to_string(session->budget.used)+" peak="+std::to_string(session->budget.peak)+" requests="+std::to_string(p.filter_requests.size());for(const auto&r:p.filter_requests)detail+=" ["+std::to_string(r.key.w)+"x"+std::to_string(r.key.h)+"->"+std::to_string(r.key.dw)+"x"+std::to_string(r.key.dh)+"]";throw std::runtime_error(detail);}
   for(size_t i=0;i<count;i++)if(!exbox_gpu2d_bind_filtered(session->compositor,unsigned(i),bindings[i].buffer,size_t(bindings[i].bytes),bindings[i].width,bindings[i].height,bindings[i].pitch,bindings[i].generation))throw std::runtime_error("filter binding refused");
   for(auto&c:commands)if(c.kind==EXBOX_GPU_FILTERED){size_t slot=size_t(c.transform[9]);if(slot>=count)throw std::runtime_error("filter slot missing");c.bytes=size_t(bindings[slot].generation);}
   session->filtered=filtered;
  }

  for(const auto&c:p.commands)if(c.kind==EXBOX_GPU_GUIDE_SAVE_BLACK){if(!exbox_gpu2d_enable_guide(session->compositor))throw std::runtime_error("Guide allocation refused");break;}
  for(const auto&c:p.commands)if(c.kind==EXBOX_GPU_BEGIN_SLOT){if(!exbox_gpu2d_enable_custom_slot(session->compositor))throw std::runtime_error("slot allocation refused");break;}
  if(!exbox_source_client_prepare(&sources,session->compositor,commands.data(),commands.size(),p.source_ids.data()))throw std::runtime_error("source publication refused");
  if(!p.avatar_words.empty()||!p.actors.empty()){
   if(!session->avatar){session->avatar=exbox_avatar_create_with_budget(p.avatar_width,p.avatar_height,session->device,&session->memory,&session->properties,session->flags,vkGetDeviceProcAddr,&session->budget);session->avatar_w=p.avatar_width;session->avatar_h=p.avatar_height;}
   if(!session->avatar||session->avatar_w!=unsigned(p.avatar_width)||session->avatar_h!=unsigned(p.avatar_height)||!([&](){if(p.actors.empty())return exbox_avatar_prepare(session->avatar,p.avatar_words.data(),p.avatar_words.size());std::vector<ExboxAvatarInput> inputs;for(const auto&a:p.actors){inputs.push_back({a.words->data(),a.words->size()});session->actor_indices.push_back(a.command_index);}return exbox_avatar_prepare_group(session->avatar,inputs.data(),inputs.size());})()){std::string detail="avatar packet refused; budget="+std::to_string(session->budget.used)+" peak="+std::to_string(session->budget.peak)+" ui="+std::to_string(exbox_gpu2d_allocated(session->compositor))+" avatar="+std::to_string(exbox_avatar_allocated(session->avatar))+" words="+std::to_string(p.avatar_words.size());for(const auto&c:commands)if(c.kind==EXBOX_GPU_GUIDE_SAVE_BLACK)detail+=" guide="+std::to_string(c.width)+"x"+std::to_string(c.height);throw std::runtime_error(detail);}
   if(!exbox_gpu2d_bind_external(session->compositor,exbox_avatar_output(session->avatar),size_t(p.avatar_width)*p.avatar_height*4,p.avatar_width,p.avatar_height,p.avatar_width*4,p.avatar_generation)||!exbox_gpu2d_prepare_leased(session->compositor,commands.data(),commands.size(),sources.frame,p.avatar_generation))throw std::runtime_error("external generation refused");session->avatar_active=true;
  }else if(!exbox_gpu2d_prepare_leased(session->compositor,commands.data(),commands.size(),sources.frame,0))throw std::runtime_error("owned packet refused");
  auto pixels=session->verify_only_readback();
  exbox_gpu2d_complete_after_wait(session->compositor);
  if(filtered&&exbox_filtered_phase(filtered)==EXBOX_FILTERED_RECORDED&&!exbox_filtered_complete_after_wait(filtered,1))throw std::runtime_error("filter completion refused");
  if(const char*live=std::getenv("NXE_GPU_CAPTURE_LIVE")){if(*live=='1'&&p.avatar_words.empty()&&p.actors.empty())return true;}
  if(const char*dir=std::getenv("NXE_GPU_CAPTURE_DIR")){
   std::string prefix=std::string(dir)+"/frame-"+std::to_string(captures);
   auto save=[&](const std::string&path,const std::vector<uint32_t>&data){std::ofstream f(path,std::ios::binary);f<<"P6\n"<<p.width<<" "<<p.height<<"\n255\n";for(auto pixel:data){char rgb[3]={char(pixel>>16),char(pixel>>8),char(pixel)};f.write(rgb,3);}};
   save(prefix+"-gpu.ppm",pixels);if(!p.reference.empty())save(prefix+"-cpu.ppm",p.reference);
   if(!p.avatar_reference.empty()){
    auto avatar=session->verify_avatar_only_readback();if(avatar.size()!=p.avatar_reference.size())throw std::runtime_error("avatar audit size mismatch");
    auto raw=[&](const char*name,const std::vector<uint32_t>&data){std::ofstream f(prefix+name,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()*4));};raw("-avatar-gpu.argb",avatar);raw("-avatar-cpu.argb",p.avatar_reference);
   }
   {std::ofstream requests(prefix+"-filter-requests.json");requests<<"[";for(size_t i=0;i<p.filter_requests.size();++i){const auto&k=p.filter_requests[i].key;if(i)requests<<",";requests<<"{\"owner\":"<<k.owner<<",\"id\":"<<k.id<<",\"generation\":"<<k.generation<<",\"x\":"<<k.x<<",\"y\":"<<k.y<<",\"w\":"<<k.w<<",\"h\":"<<k.h<<",\"dw\":"<<k.dw<<",\"dh\":"<<k.dh<<"}";}requests<<"]";}
   {std::ofstream video(prefix+"-video-regions.json");video<<"[";bool comma=false;for(const auto&c:p.commands)if(c.kind==EXBOX_GPU_VIDEO_Q8){if(comma)video<<",";comma=true;video<<"{\"x\":"<<c.dst.x<<",\"y\":"<<c.dst.y<<",\"w\":"<<c.dst.w<<",\"h\":"<<c.dst.h<<",\"source_w\":"<<c.width<<",\"source_h\":"<<c.height<<",\"alpha\":"<<c.transform[8]<<"}";}video<<"]";}
   {std::ofstream actors(prefix+"-actors.json");actors<<"[";for(std::size_t i=0;i<p.actors.size();++i){if(i)actors<<",";const auto&a=p.actors[i];const auto&c=p.commands.at(a.command_index);actors<<"{\"emote\":"<<(a.emote?"true":"false")<<",\"command\":"<<a.command_index<<",\"words\":"<<a.words->size()<<",\"x\":"<<c.dst.x<<",\"y\":"<<c.dst.y<<",\"w\":"<<c.dst.w<<",\"h\":"<<c.dst.h<<"}";}actors<<"]";}
   {std::ofstream regions(prefix+"-regions.json");regions<<"{\"avatar\":";const ExboxGpuCommand*external=nullptr;for(const auto&c:p.commands)if(c.kind==EXBOX_GPU_EXTERNAL){external=&c;break;}if(external)regions<<"{\"x\":"<<external->dst.x<<",\"y\":"<<external->dst.y<<",\"w\":"<<external->dst.w<<",\"h\":"<<external->dst.h<<"}";else regions<<"null";regions<<",\"reflection\":";const ExboxGpuCommand*reflection=nullptr;for(const auto&c:p.commands)if(c.kind==EXBOX_GPU_NXE_REFLECTION){reflection=&c;break;}if(!reflection)regions<<"null";else{ExboxGpuNxeReflection metadata{};if(!reflection->pixels||reflection->bytes!=sizeof metadata)throw std::runtime_error("reflection audit metadata missing");std::memcpy(&metadata,reflection->pixels,sizeof metadata);regions<<"{\"output\":{\"x\":"<<reflection->dst.x<<",\"y\":"<<reflection->dst.y<<",\"w\":"<<reflection->dst.w<<",\"h\":"<<reflection->dst.h<<"},\"figure\":{\"x\":"<<metadata.figure.x<<",\"y\":"<<metadata.figure.y<<",\"w\":"<<metadata.figure.w<<",\"h\":"<<metadata.figure.h<<"},\"figure_opacity\":"<<metadata.figure_opacity<<",\"panes\":[";for(uint32_t i=0;i<metadata.pane_count;i++){if(i)regions<<",";const auto&r=metadata.panes[i];regions<<"{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"w\":"<<r.w<<",\"h\":"<<r.h<<"}";}regions<<"]}";}regions<<"}";}
   unsigned peak=0;uint64_t bad=0;for(size_t i=0;i<p.reference.size();i++)for(int c=0;c<3;c++){unsigned e=unsigned(std::abs(int((pixels[i]>>(8*c))&255)-int((p.reference[i]>>(8*c))&255)));peak=std::max(peak,e);bad+=e>8;}
   std::ofstream f(prefix+".json");f<<"{\"build\":"<<p.build<<",\"channel\":"<<p.channel<<",\"viewer\":"<<p.viewer<<",\"commands\":"<<p.gpu_primitives<<",\"source_bytes\":"<<p.resource_copy_bytes<<",\"unhandled\":"<<p.unhandled<<",\"max_error\":"<<peak<<",\"channels_gt8\":"<<bad<<",\"avatar_generation\":"<<p.avatar_generation<<",\"avatar_words\":"<<p.avatar_words.size()<<",\"partial_cpu_background\":"<<(p.partial_cpu_background?"true":"false")<<",\"gpu_budget_used\":"<<session->budget.used<<",\"gpu_budget_peak\":"<<session->budget.peak<<",\"publication_hashed_total\":"<<p.publication_hashed<<",\"publication_verified_total\":"<<p.publication_verified<<",\"publication_copied_total\":"<<p.publication_copied<<",\"generation_hits_total\":"<<p.generation_hits<<",\"owned_pixel_copy_bytes\":0,\"normalizations_total\":"<<p.normalizations<<",\"normalized_bytes_total\":"<<p.normalized_bytes<<",\"gpu_source_publications_total\":"<<sources.publications<<",\"gpu_source_published_bytes_total\":"<<sources.published_bytes<<",\"gpu_source_reuses_total\":"<<sources.reuses<<",\"gpu_source_retirements_total\":"<<sources.retirements<<",\"filtered_requests\":"<<p.filter_requests.size()<<",\"overlay_mode\":"<<p.overlay_mode<<",\"cpu_guide_paints\":"<<p.cpu_guide_paints<<",\"cpu_fitted_preparations_total\":"<<p.cpu_fitted<<",\"cpu_picture_zoom_resamples_total\":"<<p.cpu_zoom<<",\"audit_picture_zoom_resamples_total\":"<<p.audit_zoom<<"}";
  }
  ++captures;unsigned cap=2;if(const char*v=std::getenv("NXE_GPU_AUDIT_FRAMES"))cap=unsigned(std::clamp(std::atoi(v),1,12));if(captures>=cap)available.store(false);return true;
 }
};
}
#endif
