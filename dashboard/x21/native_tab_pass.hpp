#pragma once
#include "tab_sink.hpp"
// Calls the real content producer in original tab order. No model mutation or raster.
// Producer supplies existing prepared resources and emits primitives into the sink.
// Physical transform equals current blades_scene view*pose*inverse(rest)*inverse(view).
struct NativeTabPass {
 int node=-1,channel=0;
 xui::Matrix inverse_rest=xui::Matrix::identity();
};
inline bool record_native_tabs(TabSink& sink,const xui::Scene& scene,
 const std::vector<NativeTabPass>& tabs,const xui::Matrix& logical_view,
 const xui::Matrix& inverse_view,int active_channel,
 const std::function<bool(TabSink&,int,bool)>& draw_content){
 const size_t commands=sink.packet.commands.size(),owned=sink.packet.owned.size();const auto counters=sink.counters;const auto target=sink.target;const auto saved_clip=sink.clip;
 const auto rollback=[&]{sink.packet.commands.resize(commands);sink.packet.owned.resize(owned);sink.counters=counters;sink.target=target;sink.clip=saved_clip;};
 try {
 const auto up=xui::Matrix::scale(float(sink.density),float(sink.density));
 const auto down=xui::Matrix::scale(1.f/sink.density,1.f/sink.density);
 for(const auto&tab:tabs){if(tab.node<0||size_t(tab.node)>=scene.world.size())continue;
  const auto&pose=scene.world[size_t(tab.node)];if(!pose.visible||pose.opacity<=0)continue;
  sink.begin_tab();if(!draw_content(sink,tab.channel,tab.channel==active_channel)){rollback();return false;}
  sink.end_tab(up*logical_view*pose.matrix*tab.inverse_rest*inverse_view*down,pose.opacity);
 }
 return true;
 }catch(...){rollback();throw;}
}
