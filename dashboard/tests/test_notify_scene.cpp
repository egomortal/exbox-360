// Host check (no SDL): load each private pack and exercise the actual XUI player.
#include "../xui_runtime.hpp"
#include <cassert>
#include <iostream>

static void check(const std::string &path) {
    xui::Scene scene;
    assert(scene.load(path.c_str()));
    const int control=scene.find("PopupControl"),visual=scene.find("@visual:scr_Notification");
    const int label=scene.find("TextPresenter",visual),image=scene.find("Image",visual);
    assert(control>=0 && visual>=0 && label>=0 && image>=0);
    assert(scene.bind_string(control,xui::Text,"Achievement unlocked\n20 G - Example"));
    assert(scene.bind_string(image,xui::Texture,"host:achievement"));
    size_t animation=0;
    while (animation<scene.animations.size() && scene.animations[animation].owner!=visual) ++animation;
    assert(animation<scene.animations.size());
    assert(scene.seek_name(visual,"TransTo",true));
    scene.evaluate();
    assert(!scene.world[size_t(label)].visible);
    scene.advance(80);scene.evaluate();
    assert(scene.world[size_t(label)].visible && scene.world[size_t(image)].visible);
    assert(std::string(scene.str(scene.state[size_t(control)][xui::Text].token))=="Achievement unlocked\n20 G - Example");
    assert(std::string(scene.str(scene.state[size_t(image)][xui::Texture].token))=="host:achievement");
    scene.advance(120);scene.evaluate();
    assert(std::abs(scene.playback[animation].frame-80)<.001f); // authored 65..185 loop
    assert(scene.world[size_t(label)].visible);
    assert(scene.sound_events.empty());
    for (int hold:{1600,3200,6000}) {
        assert(scene.seek_name(visual,"TransTo",true));
        scene.advance(65+float(hold)*60/1000);scene.evaluate();
        assert(scene.world[size_t(label)].visible);
        assert(scene.seek_name(visual,"TransFrom",true));
        scene.advance(54);scene.evaluate();
        assert(!scene.playback[animation].playing);
        assert(std::abs(scene.playback[animation].frame-240)<.001f);
        assert(!scene.world[size_t(label)].visible && !scene.world[size_t(image)].visible);
    }
    assert(scene.seek_name(visual,"TransTo",true)); // queued toast restarts cleanly
    scene.advance(80);scene.evaluate();
    assert(scene.world[size_t(label)].visible);
}

int main(int argc,char **argv) {
    const std::string base=argc>1?argv[1]:"private/era-skins";
    for (const char *path:{"blades/notify-1888/notify-1888.nxui","blades/notify-6770/notify-6770.nxui",
                         "nxe/notify-9199/notify-9199.nxui","kinect/notify-13604/notify-13604.nxui",
                         "metro1/notify-15574/notify-15574.nxui","metro/notify-17559/notify-17559.nxui"}) check(base+"/"+path);
    xui::Scene missing;assert(!missing.load((base+"/not-installed/notify.nxui").c_str()));
    std::cout<<"Notification scene checks passed\n";
}
