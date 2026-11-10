namespace gpu_overlay {
std::unique_ptr<TabSink> sink;
std::shared_ptr<x21hook::OwnedPacket> base,backdrop;
bool begin(const std::shared_ptr<x21hook::OwnedPacket>&packet){
 if(!packet||sink||x21hook::active)return false;
 base=packet;sink=std::make_unique<TabSink>(packet->width,packet->height);
 sink->packet.commands=packet->commands;sink->publications=packet->publications;sink->filtered_requests=packet->filtered_requests;
 for(const auto&r:sink->filtered_requests)sink->filtered_requested.push_back(r.key);
 x21hook::active=sink.get();x21hook::target=screen;x21hook::sync_clip();return true;
}
bool begin_opaque(){
 if(sink||x21hook::active)return false;
 auto packet=std::make_shared<x21hook::OwnedPacket>();packet->width=screen->w;packet->height=screen->h;packet->build=model.dashboard_build;packet->channel=model.channel;
 if(!begin(packet))return false;sink->clear_output(0);return true;
}
bool finish(int mode,uint64_t cpu_paints){
 if(!sink)return true;x21hook::active=nullptr;x21hook::target=nullptr;
 auto out=x21hook::own(*sink,base->build);
 if(out){out->fullscreen_picture=model.viewer==dash::Viewer::Pictures&&model.viewer_full;out->pictures_grid=model.viewer==dash::Viewer::Pictures&&!model.viewer_full;out->cpu_fitted=x21hook::cpu_fitted_preparations;out->cpu_zoom=x21hook::cpu_picture_zoom_resamples;out->audit_zoom=x21hook::audit_picture_zoom_resamples;out->overlay_mode=mode;out->viewer=int(model.viewer);out->cpu_guide_paints=cpu_paints;out->channel=base->channel;out->actors=base->actors;out->avatar_words=base->avatar_words;out->avatar_generation=base->avatar_generation;out->avatar_width=base->avatar_width;out->avatar_height=base->avatar_height;out->avatar_reference=base->avatar_reference;out->partial_cpu_background=base->partial_cpu_background;x21hook::ready=std::move(out);}
 else {for(const auto&why:sink->packet.unsupported)log(("GPU overlay unsupported: "+why).c_str());x21hook::ready.reset();}
 sink.reset();base.reset();return bool(out)||bool(x21hook::ready);
}
}
