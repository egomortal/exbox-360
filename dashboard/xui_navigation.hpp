#pragma once
#include "xui_runtime.hpp"

namespace xui {
// The model remains authoritative. Finish each original adjacent transition
// before following its latest destination; rapid input never seeks mid-curve.
class Navigation {
    Scene *scene=nullptr;
    int first=0,last=4,settled=0,destination=0,arriving=0;
    int root_animation=-1;
    bool moving=false;
    bool named(int from,int to,bool end,bool play) {
        char name[32];std::snprintf(name,sizeof name,"%dTo%d%s",from-first+1,to-first+1,end?"End":"");
        return scene->seek_name(0,name,play);
    }
public:
    bool bind(Scene &value,bool early,int active) {
        scene=&value;first=early?1:0;last=4;
        root_animation=-1;
        for (size_t i=0;i<value.animations.size();i++) if (value.animations[i].owner==0) root_animation=int(i);
        if (root_animation<0) return false;
        return reset(active);
    }
    bool reset(int active) {
        if (!scene) return false;
        settled=destination=arriving=std::clamp(active,first,last);moving=false;
        if (settled==first) scene->seek(0,0);
        else if (!named(settled-1,settled,true,false)) return false;
        scene->evaluate();return true;
    }
    void request(int active) {destination=std::clamp(active,first,last);}
    void advance(float frames) {
        if (!scene || root_animation<0) return;
        scene->advance(frames);
        if (moving && !scene->playback[root_animation].playing) {settled=arriving;moving=false;}
        if (!moving && settled!=destination) {
            arriving=settled+(destination>settled?1:-1);
            moving=named(settled,arriving,false,true);
            if (!moving) reset(destination);
        }
        scene->evaluate();
    }
    int current() const {return settled;}
    int target() const {return destination;}
    bool transitioning() const {return moving;}
    float frame() const {return scene && root_animation>=0?scene->playback[root_animation].frame:0;}
};
}
