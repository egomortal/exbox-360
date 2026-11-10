namespace gpu_home {
std::unique_ptr<TabSink> sink;std::vector<uint32_t> words;
std::vector<x21hook::OwnedPacket::Actor> actors;
uint64_t generation=0;std::string committed_key;
bool frame_avatar=false,fallback_state=false;
int raster_width=0,raster_height=0;SDL_Rect figure_bounds{};float figure_alpha=0;
bool begin(){
 if(!x21hook::allow_frame||model.era==0||sink)return false;
 if(model.era==1&&(model.channel==dash::MyXbox||slide_from==dash::MyXbox)&&!look3d::active()&&!prepared_avatar::active())return false;
 sink=std::make_unique<TabSink>(screen->w,screen->h);sink->clear_output(0);
 frame_avatar=false;fallback_state=false;figure_bounds={};figure_alpha=0;raster_width=raster_height=0;actors.clear();
 x21hook::active=sink.get();x21hook::target=screen;x21hook::sync_clip();return true;
}
bool avatar(SDL_Surface* figure,const SDL_Rect& destination,float opacity){
 if(auto*actor=prepared_avatar::find(figure)){
  if(!x21hook::allow_frame||!sink||opacity<=.01f)return false;
  try{
   if(actors.size()>=4)throw std::runtime_error("four actor frame capacity");
   const int density=render_density::factor;if(frame_avatar&&raster_width!=600*density)throw std::runtime_error("mixed legacy/prepared raster extent");raster_width=600*density;raster_height=660*density;
   auto owned=std::make_shared<const std::vector<uint32_t>>(exbox_avatar_packet(actor->actor->renderer(),actor->pose,actor->camera,raster_width,raster_height));
   x21hook::sync_clip();auto dst=render_density::pixel_rect(destination,density);auto c=sink->base(dst);c.kind=EXBOX_GPU_EXTERNAL;c.top=0xffffffff;c.width=raster_width;c.height=raster_height;c.pitch=raster_width*4;c.transform[0]=float(raster_width)/dst.w;c.transform[4]=float(raster_height)/dst.h;c.transform[2]=-dst.x*c.transform[0];c.transform[5]=-dst.y*c.transform[4];c.transform[8]=float(Uint8(255*opacity))/255;
   actors.push_back({std::move(owned),sink->packet.commands.size(),actor->waving});sink->push(c);frame_avatar=true;figure_bounds=dst;figure_alpha=opacity;++generation;if(!generation)++generation;return true;
  }catch(const std::exception&e){sink->packet.unsupported.push_back(std::string("prepared_actor:")+e.what());return false;}
 }
 if(!x21hook::allow_frame||model.era==0||!sink||!figure||figure!=look3d::hub.surface||!look3d::painter||opacity<=.01f)return false;
 const auto& d=look3d::deferred_hub;
 if(frame_avatar&&raster_width!=400*d.density){sink->packet.unsupported.push_back("mixed_legacy_prepared_raster_extent");return false;}
 if(d.owner!=look3d::painter.get()||d.tick!=look3d::hub.drawn||d.density!=render_density::factor||d.pose.empty())return false;
 try{
  const std::string key=std::to_string(reinterpret_cast<uintptr_t>(d.owner))+":"+std::to_string(d.tick)+":"+std::to_string(d.density)+":"+look3d::look_text();
  raster_width=400*d.density;raster_height=660*d.density;
  if(key!=committed_key){auto prepared=exbox_avatar_packet(*look3d::painter,d.pose,d.camera,raster_width,raster_height);words=std::move(prepared);committed_key=key;++generation;if(!generation)++generation;}
  frame_avatar=true;
  x21hook::sync_clip();const auto dst=render_density::pixel_rect(destination,d.density);auto c=sink->base(dst);c.kind=EXBOX_GPU_EXTERNAL;c.top=0xffffffff;c.width=raster_width;c.height=raster_height;c.pitch=raster_width*4;c.transform[0]=float(raster_width)/dst.w;c.transform[4]=float(raster_height)/dst.h;c.transform[2]=-dst.x*c.transform[0];c.transform[5]=-dst.y*c.transform[4];c.transform[8]=float(Uint8(255*opacity))/255;actors.push_back({std::make_shared<const std::vector<uint32_t>>(words),sink->packet.commands.size()});sink->push(c);figure_bounds=dst;figure_alpha=opacity;return true;
 }catch(const std::exception&e){log((std::string("GPU Home fallback: ")+e.what()).c_str());if(sink)sink->packet.unsupported.push_back("avatar_prepare_failed");return false;}
}
bool finish(){
 if(!sink)return true;x21hook::active=nullptr;x21hook::target=nullptr;
 auto packet=x21hook::own(*sink,model.dashboard_build);if(!packet){{static std::string last_reason;uint64_t filtered_bytes=0;for(const auto&key:sink->filtered_requested)filtered_bytes+=uint64_t(key.dw)*key.dh*4;const std::string reason=(sink->packet.unsupported.empty()?"command_validation":sink->packet.unsupported.front())+"; distinct filtered outputs="+std::to_string(sink->filtered_requested.size())+"; output bytes="+std::to_string(filtered_bytes);if(reason!=last_reason){log(("GPU Home packet refused: "+reason+"; CPU fallback").c_str());last_reason=reason;}}if(!fallback_state)x21hook::available.store(false);sink.reset();return false;}
 packet->channel=model.channel;packet->partial_cpu_background=false;if(frame_avatar){packet->actors=std::move(actors);if(packet->actors.empty())packet->avatar_words=words;packet->avatar_generation=generation;packet->avatar_width=raster_width;packet->avatar_height=raster_height;if(packet->actors.empty()&&x21hook::audit_cpu())packet->avatar_reference=look3d::hub.img.argb;}x21hook::ready=std::move(packet);sink.reset();return true;
}
}
bool gpu_home_reflect(){
 if(!gpu_home::sink)return false;
 if(nxe_reflection::figure&&!gpu_home::frame_avatar){gpu_home::fallback_state=true;gpu_home::sink->packet.unsupported.push_back("legacy_image_reflection_source_pending");return !x21hook::audit_cpu();}
 ExboxGpuNxeReflection metadata{};metadata.density=render_density::factor;metadata.pane_count=uint32_t(nxe_reflection::pane_count);metadata.has_figure=gpu_home::frame_avatar?1:0;metadata.figure_opacity=gpu_home::figure_alpha;
 const auto f=gpu_home::figure_bounds;metadata.figure={f.x,f.y,f.w,f.h};
 for(size_t i=0;i<nxe_reflection::pane_count;i++){const auto p=render_density::pixel_rect(nxe_reflection::panes[i],render_density::factor);metadata.panes[i]={p.x,p.y,p.w,p.h};}
 auto c=exbox_nxe_reflection_command(metadata,gpu_home::raster_width,gpu_home::raster_height,gpu_home::raster_width*4);const size_t first=gpu_home::sink->packet.commands.size();gpu_home::sink->push(c);x21hook::snapshot_commands(first);return !x21hook::audit_cpu();
}
