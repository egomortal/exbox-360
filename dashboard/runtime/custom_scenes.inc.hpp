// Owner dash.xex scenes compiled by tools/xur5/compile_custom_scenes.py.
namespace custom_scene {
int loaded_build=-1;
std::string directory;
xui::Scene legend;
SDL_Surface *highlight=nullptr,*legend_art[4]{};
Uint32 pressed[4]{};
bool has_press[4]{};
int button_slot(xb::Button b) {return b==xb::A?0:b==xb::B?1:b==xb::X?2:b==xb::Y?3:-1;}
const char *button_ids[]={"AButton","BButton","XButton","YButton"};
SDL_Surface *bitmap(const std::string &path) {
    SDL_Surface *raw=load_png(path);if (!raw) return nullptr;
    SDL_Surface *out=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);
    SDL_FreeSurface(raw);return out;
}
struct Painted {
    xui::Scene scene;
    xui::Renderer painter;
    SDL_Surface *backdrop=nullptr;
    SDL_Surface *bound_icon=nullptr; // borrowed identity; painter owns its copy
    Uint32 tick=0;
    int card=-1,channel=-1;
    bool ready=false,selected=false;
    void clear() {
        painter.clear();SDL_FreeSurface(backdrop);backdrop=nullptr;
        scene=xui::Scene{};ready=false;bound_icon=nullptr;card=channel=-1;tick=0;
    }
    void load(const std::string &path,int w,int h) {
        clear();
        if (!scene.load(path.c_str())) return;
        if (!painter.prepare(scene,w,h,"",1)) {clear();return;}
        backdrop=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if (!backdrop) {clear();return;}
        SDL_SetSurfaceBlendMode(backdrop,SDL_BLENDMODE_NONE);ready=true;
    }
    bool draw(SDL_Rect box) {
        if (!ready || box.w<=0 || box.h<=0 || box.x<0 || box.y<0 || box.x+box.w>W || box.y+box.h>H) return false;
        // Paint over a sampled copy of the existing pane, preserving XUI blend
        // modes without claiming its software painter produces transparent art.
        SDL_Rect local{0,0,backdrop->w,backdrop->h};
        if (SDL_BlitScaled(screen,&box,backdrop,&local)!=0) return false;
        painter.draw(scene,xui::Matrix::identity(),false,backdrop);
        return SDL_BlitScaled(painter.canvas,nullptr,screen,&box)==0;
    }
};
Painted slots[6],equalizer,loading;
const char *slot_names[]={"TraySlotScene","GamesSlotScene","MusicSlotScene","PhotosSlotScene","SettingsSlotScene","VideoSlotScene"};
int selected_build() {
    if (model.era==0) return model.dashboard_build>=5759?6770:0;
    if (model.era==1) return 9199;
    if (model.era==2) return 13604;
    return model.metro_v1()?15574:17559;
}
void clear() {
    for (auto &s:slots) s.clear();
    equalizer.clear();loading.clear();legend=xui::Scene{};
    SDL_FreeSurface(highlight);highlight=nullptr;
    for (auto *&s:legend_art) {SDL_FreeSurface(s);s=nullptr;}
    std::fill(std::begin(has_press),std::end(has_press),false);
}
void refresh() {
    const int build=selected_build();if (loaded_build==build) return;
    clear();loaded_build=build;
    std::string base=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) base=std::string(override)+"/";
#endif
    directory=base+std::to_string(build)+"/custom-scenes/";
    if (legend.load((directory+"legend.nxui").c_str())) {
        highlight=bitmap(directory+"Legend_Highlight.png");
        const char *names[]={"Legend_A.png","Legend_B.png","Legend_X.png","Legend_Y.png"};
        for (int i=0;i<4;i++) legend_art[i]=bitmap(directory+names[i]);
        for (const char *id:button_ids) legend.bind_show(legend.find(id),true);
    }
    equalizer.load(directory+"equalizer.nxui",76,32);
    // dashuisk/skin.xur Loading_Large, same-build sharedres textures.
    // 13604 remains on the fallback: its loop still needs unsupported curves.
    const int loading_w=build==6770?99:build==17559?72:96;
    const int loading_h=build==6770?101:loading_w;
    if (build==6770 || build==9199 || build==15574 || build==17559) {
        loading.load(directory+"loading.nxui",loading_w,loading_h);
        loading.painter.bind_bitmaps([](const std::string &name) {return bitmap(directory+name);});
        // Missing referenced art must restore the fallback, not a partial ring.
        if (loading.ready) for (const auto &node:loading.scene.nodes) {
            const char *name=loading.scene.str(node.p[xui::Texture].token);
            if (!*name) continue;
            SDL_Surface *art=bitmap(directory+name);
            if (!art) {loading.clear();break;}
            loading.painter.bind_bitmap(name,art);
        }
    }
    if (build==9199) for (int i=0;i<6;i++) {
        auto &s=slots[i];s.load(directory+slot_names[i]+".nxui",420,320);
        if (!s.ready) continue;
        // Box-art presenters belong to the 360 library; the host provides one
        // icon here. Decorative skin visuals and reflection retain their keys.
        for (const char *id:{"BoxArt1","BoxArt2","BoxArt3"}) s.scene.bind_show(s.scene.find(id),false);
    }
}
void press(xb::Button button,Uint32 now) {
    const int i=button_slot(button);
    if (i>=0 && model.era>=1 && !model.guide) {pressed[i]=now;has_press[i]=true;}
}
bool draw_legend(xb::Button button,int x,int y,int size) {
    const int i=button_slot(button);
    if (i<0 || !legend.valid || !highlight) return false;
    SDL_Surface *base=model.button_style==0?legend_art[i]:button_image(button);
    if (!base) return false;
    const int owner=legend.find(button_ids[i]);
    if (owner<0) return false;
    const float first=custom_animation::named_frame(legend,owner,"Press");
    const float end=custom_animation::named_frame(legend,owner,"PressEnd");
    const float frame=has_press[i]?first+float(SDL_GetTicks()-pressed[i])*.06f:0;
    legend.seek(owner,frame<=end?frame:0);legend.evaluate();
    const auto view=xui::Matrix::translate(float(x),float(y))*xui::Matrix::scale(float(size)/32,float(size)/32);
    for (const char *id:{"Button_Image","Button_Highlight"}) {
        const int n=legend.find(id,owner);if (n<0) continue;
        const auto &w=legend.world[size_t(n)];if (!w.visible) continue;
        SDL_Surface *art=!std::strcmp(id,"Button_Image")?base:highlight;
        // Press only scales/translates these images. SDL handles the display's
        // pixel format rather than treating its native pixels as ARGB8888.
        const auto transform=view*w.matrix;
        const auto a=transform.point(0,0),b=transform.point(w.width,w.height);
        SDL_Rect box{int(std::lround(a.x)),int(std::lround(a.y)),int(std::lround(b.x-a.x)),int(std::lround(b.y-a.y))};
        Uint8 alpha=255;SDL_BlendMode blend=SDL_BLENDMODE_NONE;
        SDL_GetSurfaceAlphaMod(art,&alpha);SDL_GetSurfaceBlendMode(art,&blend);
        SDL_SetSurfaceBlendMode(art,SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(art,Uint8(std::clamp(w.opacity*float(text_alpha),0.f,255.f)));
        if (box.w>0 && box.h>0) SDL_BlitScaled(art,nullptr,screen,&box);
        SDL_SetSurfaceAlphaMod(art,alpha);SDL_SetSurfaceBlendMode(art,blend);
    }
    return true;
}
bool draw_slot(int icon,int card,bool selected,SDL_Rect box,SDL_Surface *art,Uint32 now) {
    if (loaded_build!=9199 || icon<0 || icon>=6 || !art || cards_static) return false;
    auto &s=slots[icon];if (!s.ready) return false;
    const bool fresh=s.card!=card || s.channel!=int(model.channel) || now-s.tick>1000;
    custom_animation::focus(s.scene,selected,s.selected,fresh);
    if (!fresh) s.scene.advance(float(now-s.tick)*.06f);
    s.card=card;s.channel=int(model.channel);s.selected=selected;s.tick=now;
    if (s.bound_icon!=art) {
        SDL_Surface *copy=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);
        if (!copy) return false;
        s.painter.bind_bitmap("@host-icon",copy);s.bound_icon=art;
        for (const char *id:{"imgIcon","reflection"}) {
            const int n=s.scene.find(id);
            s.scene.bind_string(n,xui::Texture,"@host-icon");
            s.scene.bind_show(n,true);s.scene.bind_image_mode(n,4);
        }
        s.scene.bind_show(s.scene.find("iconReflection"),true);
    }
    return s.draw(box);
}
bool draw_loading(int cx,int cy,int diameter) {
    if (!loading.ready) return false;
    const Uint32 now=SDL_GetTicks();
    if (!loading.tick || now-loading.tick>250) {
        if (!custom_animation::loading_start(loading.scene)) return false;
    } else loading.scene.advance(float(now-loading.tick)*.06f);
    loading.tick=now;
    const int span=std::max(loading.backdrop->w,loading.backdrop->h);
    const int w=diameter*loading.backdrop->w/span,h=diameter*loading.backdrop->h/span;
    return loading.draw({cx-w/2,cy-h/2,w,h});
}
bool draw_equalizer(int x,int y) {
    if (model.now_playing<0 || !equalizer.ready) return false;
    custom_animation::equalizer(equalizer.scene,float(SDL_GetTicks())*.06f,model.music_paused);
    return equalizer.draw({x,y,76,32});
}
}
