// Included after blades_guide_scene.inc.hpp, inside main.cpp's private namespace.
namespace notify_scene {
// Reuse the Guide's two-background alpha recovery, bitmap ownership and cache.
struct Core : guide_scene::Core {
    int build=0,control=-1,visual=-1,image=-1,label=-1;
    float opening=0,closing=0,last_age=0;
    size_t source_strings=0;
    Uint32 started=0;
    bool bound=false,leaving=false,achievement=false;
    std::string title,body;
    SDL_Surface *icon=nullptr;
    void bind_owned_bitmap(const char*name,SDL_Surface*owned) {
        if(gpu_ready){SDL_Surface*copy=owned?SDL_ConvertSurfaceFormat(owned,SDL_PIXELFORMAT_ARGB8888,0):nullptr;
            if(owned&&!copy)gpu_ready=false;else gpu_resources.bind_bitmap(name,copy);}
        painter.bind_bitmap(name,owned);gpu_dirty=true;cpu_valid=false;
    }
    float frame(const char *name) const {
        for (const auto &a:scene.animations) if (a.owner==visual)
            for (int i=a.range_first;i<a.range_first+a.range_count;i++)
                if (!std::strcmp(scene.str(scene.ranges[i].name),name)) return scene.ranges[i].frame;
        return -1;
    }
    void reset() {
        clear();scene=xui::Scene{};tried=false;build=0;bound=false;achievement=false;
        title.clear();body.clear();icon=nullptr;
    }
    bool prepare(int selected) {
        if (build==selected && raster_density==render_density::factor) return ready;
        reset();build=selected;
        const char *era=selected<=6770?"blades":selected==9199?"nxe":selected==13604?"kinect":selected==15574?"metro1":"metro";
        const std::string name="notify-"+std::to_string(selected),folder=std::string(era)+"/"+name;
        const float canvas_w=selected<=6770?1120.f:852.f,canvas_h=selected<=6770?770.f:480.f;
        const float scale=float(H)/canvas_h;
        auto view=xui::Matrix::scale(scale,scale);
        // notify.xur (1888..17559) is a local 460x87 popup with no screen position.
        // Ex-Box addition: no 360 original host placement; bottom centre, 5% safe inset.
        view.a[0][3]=(float(W)-canvas_w*scale)/2+(canvas_w-460)*scale/2;
        view.a[1][3]=(canvas_h-87-canvas_h*.05f)*scale;
        const std::string saved=guide_scene::font_path;
        if (selected>=15574 && !guide_scene::segoe_path.empty()) guide_scene::font_path=guide_scene::segoe_path;
        const bool ok=load(folder.c_str(),(name+".nxui").c_str(),{W/8,H/2,3*W/4,H-H/2},view,"Notification");
        guide_scene::font_path=saved;
        if (!ok) return false;
        control=node("PopupControl");visual=visual_of(control);
        image=node("Image",visual);label=node("TextPresenter",visual);
        // skin.xur calls opening TransTo (0..65, then loop 65..185), closing
        // TransFrom (186..240), in all six recovered builds. Keep those names.
        opening=frame("loop")-frame("TransTo");closing=frame("EndTransFrom")-frame("TransFrom");
        if (control<0 || visual<0 || image<0 || label<0 || frame("TransTo")<0 || frame("TransFrom")<0 || opening<=0 || closing<=0) {
            clear();return false;
        }
        if (SDL_Surface *raw=IMG_Load((art_directory+"Achievement.png").c_str())) {
            SDL_Surface *copy=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);
            if (copy) {bind_owned_bitmap("host:achievement",copy);achievement=true;}
        }
        source_strings=scene.strings.size();
        return true;
    }
    void content() {
        // Discard previous host strings so a long music session stays bounded.
        scene.host_bindings.clear();scene.strings.resize(source_strings);
        // Host token offsets are reused; invalidate cached bitmap resolutions too.
        bind_owned_bitmap("host:notify-icon",nullptr);
        text_of(control,toast.title+(toast.text.empty()?"":"\n"+toast.text));
        const bool trophy=toast.icon && (toast.icon==toast_trophy || toast.icon==trophy_icon);
        if (trophy && achievement) scene.bind_string(image,xui::Texture,"host:achievement");
        else if (toast.icon) {
            if (SDL_Surface *copy=SDL_ConvertSurfaceFormat(toast.icon,SDL_PIXELFORMAT_ARGB8888,0)) {
                bind_owned_bitmap("host:notify-icon",copy);scene.bind_string(image,xui::Texture,"host:notify-icon");
            } else scene.bind_string(image,xui::Texture,"xenonLogo.png");
        } else scene.bind_string(image,xui::Texture,"xenonLogo.png");
        scene.bind_image_mode(image,16);
        // The skin controls visibility and alternates its logo with the image slot.
        for (auto &play:scene.playback) play={0,false};
        scene.seek_name(visual,"TransTo",true);
        started=toast.at;title=toast.title;body=toast.text;icon=toast.icon;
        bound=true;leaving=false;last_age=0;
    }
};
Core core;
int selected_build() {
    if (model.era==0) return model.dashboard_build>=5759?6770:1888;
    return model.era==1?9199:model.era==2?13604:model.metro_v1()?15574:17559;
}
void refresh() {if (core.build && (core.build!=selected_build() || core.raster_density!=render_density::factor)) core.reset();}
void clear() {core.reset();}
bool draw(Uint32 now,Uint32 hold) {
    if (!core.prepare(selected_build())) return false;
    const float age=float(now-toast.at)*60/1000,exit_at=core.opening+float(hold)*60/1000;
    if (age>=exit_at+core.closing) {toast.live=false;core.bound=false;return true;}
    const bool changed=!core.bound || core.started!=toast.at || core.title!=toast.title || core.body!=toast.text || core.icon!=toast.icon;
    if (changed) core.content();
    float frames=age-core.last_age;
    if (age>=exit_at && !core.leaving) {
        core.scene.seek_name(core.visual,"TransFrom",true);core.leaving=true;frames=age-exit_at;
    } else if (age<exit_at && core.leaving) {
        // Notification duration can change while a toast is visible.
        core.scene.seek_name(core.visual,"TransTo",true);core.leaving=false;frames=age;
    }
    core.finish(changed,frames);core.last_age=age;
    return true;
}
}
