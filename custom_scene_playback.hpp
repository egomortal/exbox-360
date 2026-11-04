#pragma once
#include "xui_runtime.hpp"

namespace custom_animation {
// slots/*SlotScene.xur, 9199: preserve each scene's own stop/goto commands.
inline bool focus(xui::Scene &scene,bool selected,bool previous,bool fresh) {
    if (!scene.valid) return false;
    if (fresh) return scene.seek_name(0,selected?"InitFocus":"Normal",true);
    if (selected!=previous) return scene.seek_name(0,selected?"Focus":"KillFocus",true);
    return false;
}
inline float named_frame(const xui::Scene &scene,int owner,const char *name,float fallback=0) {
    for (const auto &a:scene.animations) if (a.owner==owner)
        for (int i=a.range_first;i<a.range_first+a.range_count;i++)
            if (!std::strcmp(scene.str(scene.ranges[i].name),name)) return scene.ranges[i].frame;
    return fallback;
}
// Loading_Large: start every authored timeline, including nested ring groups.
// Metro packs contain only the verified Loop; other eras retain Normal.
inline bool loading_start(xui::Scene &scene) {
    if (!scene.valid) return false;
    bool started=false;
    for (const auto &a:scene.animations) {
        const bool active=scene.seek_name(a.owner,"Normal",true) || scene.seek_name(a.owner,"Loop",true);
        started=active || started;
    }
    return started;
}
// Equalizer Normal ends immediately before NormalDisable; the music control
// restarts that range while playing and selects NormalDisable when paused.
inline void equalizer(xui::Scene &scene,float frames,bool paused) {
    if (!scene.valid) return;
    const float end=named_frame(scene,0,"EndNormal");
    scene.seek(0,paused?named_frame(scene,0,"NormalDisable"):(end>0?std::fmod(frames,end):0));
}
}
