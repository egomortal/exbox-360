#include "../custom_scene_playback.hpp"
#include <cassert>
#include <iostream>

int main(int argc,char **argv) {
    assert(argc==2);
    const std::string base=argv[1];
    xui::Scene missing;
    assert(!custom_animation::focus(missing,true,false,true));
    xui::Scene slot;
    assert(slot.load((base+"/9199/custom-scenes/GamesSlotScene.nxui").c_str()));
    const int icon=slot.find("imgIcon");assert(icon>=0);
    assert(custom_animation::focus(slot,false,false,true));
    slot.advance(20);slot.evaluate();
    const float normal=slot.state[size_t(icon)][xui::Scale].v[0];
    assert(custom_animation::focus(slot,true,false,false));
    slot.advance(7);slot.evaluate();
    assert(slot.state[size_t(icon)][xui::Scale].v[0]>normal);
    slot.advance(1000);
    // EndFocus / EndInitFocus jump to FocusLoop in this source scene.
    const auto &state=slot.playback.back();
    assert(state.playing && state.frame>=22 && state.frame<86);
    assert(custom_animation::focus(slot,false,true,false));
    slot.advance(100);slot.evaluate();
    assert(!slot.playback.back().playing);
    assert(std::abs(slot.state[size_t(icon)][xui::Scale].v[0]-normal)<.001f);
    assert(custom_animation::focus(slot,true,false,true));
    slot.advance(8);assert(slot.playback.back().frame>=22 && slot.playback.back().frame<86);
    for (const char *build:{"9199","13604","15574","17559"}) {
        xui::Scene legend;
        assert(legend.load((base+"/"+build+"/custom-scenes/legend.nxui").c_str()));
        const int owner=legend.find("AButton"),highlight=legend.find("Button_Highlight",owner);
        assert(owner>=0 && highlight>=0);
        legend.bind_show(owner,true);
        assert(legend.seek_name(owner,"Press",true));legend.advance(9);legend.evaluate();
        assert(legend.world[size_t(highlight)].visible);
        assert(legend.state[size_t(highlight)][xui::Opacity].v[0]>.9f);
        legend.advance(100);legend.evaluate();assert(!legend.world[size_t(highlight)].visible);
    }
    assert(!custom_animation::loading_start(missing));
    for (const char *build:{"6770","9199","15574","17559"}) {
        xui::Scene loading;
        assert(loading.load((base+"/"+build+"/custom-scenes/loading.nxui").c_str()));
        assert(custom_animation::loading_start(loading));
        loading.advance(1000);loading.evaluate();
        for (size_t i=0;i<loading.animations.size();i++) {
            const auto &a=loading.animations[i];
            const float first=custom_animation::named_frame(loading,a.owner,"Loop");
            assert(loading.playback[i].playing);
            assert(loading.playback[i].frame>=first && loading.playback[i].frame<a.last);
        }
        const int rings=loading.find("ringGroup");
        if (rings>=0) assert(loading.world[size_t(rings)].visible && loading.world[size_t(rings)].opacity==1);
    }
    for (const char *build:{"6770","13604","15574","17559"}) {
        xui::Scene badge;
        assert(badge.load((base+"/"+build+"/custom-scenes/equalizer.nxui").c_str()));
        custom_animation::equalizer(badge,60,false);assert(badge.playback[0].frame==6);
        custom_animation::equalizer(badge,60,true);assert(badge.playback[0].frame==55);
    }
    xui::Scene equalizer;
    assert(equalizer.load((base+"/9199/custom-scenes/equalizer.nxui").c_str()));
    custom_animation::equalizer(equalizer,60,false);assert(equalizer.playback[0].frame==6);
    custom_animation::equalizer(equalizer,60,true);assert(equalizer.playback[0].frame==55);
    std::cout<<"Custom scene playback checks passed\n";
}
