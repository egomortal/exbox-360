// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
namespace look3d {
avkit::Kit kit;
std::unique_ptr<avkit::Renderer> painter;
avkit::Look look, saved;
bool ready=false, imported=false, saved_imported=false;
// The CC0 kit's look, or the private 360 kit's (its own file: different categories).
const char *look_path="/data/nxe/avatar_look.txt";
constexpr int kTurns=36; // stage and editor angles (Left / Right step through them)
struct Canvas {
    avkit::Image img;
    SDL_Surface *surface=nullptr;
    Uint32 drawn=0;
    SDL_Surface *ensure(int w, int h) {
        if (surface && img.w==w && img.h==h) return surface;
        if (surface) SDL_FreeSurface(surface);
        img.resize(w,h);
        surface=SDL_CreateRGBSurfaceWithFormatFrom(img.argb.data(),w,h,32,w*4,SDL_PIXELFORMAT_ARGB8888);
        if (surface) SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);
        drawn=0;
        return surface;
    }
    void free() {if (surface) SDL_FreeSurface(surface); surface=nullptr; img.resize(0,0);}
};
Canvas hub, stage;
SDL_Surface *head_shot=nullptr; // the avatar gamerpic (also in gamerpics)
// What the avatar is doing: a clip, from when.
std::string clip="idle";
Uint32 clip_at=0, next_emote=0, changed_at=0;
float zoom=0, yaw=0; // the editor's camera, eased toward its targets
bool active() {return ready && !(imported && avatar::installed());}
// Editor rows: the avatar's source first when an imported one exists.
bool source_row() {return ready && avatar::installed();}
int animation_choice=1;
bool animation_row() {return painter && painter->has_custom_rig();}
std::string animation_label() {
    const auto *body=painter?painter->body():nullptr;
    if (!body || body->clips.empty()) return "Idle";
    const int n=std::clamp(animation_choice,0,int(body->clips.size())-1);
    const std::string &name=body->clips[size_t(n)].name;
    const char *friendly=name=="idle"?"Idle":name=="wave"?"Taunt":name=="dance"?"Breakdance":name=="talk"?"Kiss":name=="interact"?"Walk":name=="jump"?"Jump":name=="run"?"Run":nullptr;
    return std::to_string(n+1)+"/"+std::to_string(body->clips.size())+" "+(friendly?friendly:name);
}
int rows() {return int(avkit::categories().size())+(source_row()?1:0)+(animation_row()?1:0);}
int category_of(int row) {if (animation_row() && row==0) return -2; return row-(source_row()?1:0)-(animation_row()?1:0);} // -1: the source row
void play(const std::string &name, Uint32 now) {clip=name; clip_at=now;}
// The clip's time; one-shot emotes go back to idling when they end.
float clip_time(Uint32 now) {
    const avkit::Body *b=painter?painter->body():nullptr;
    if (clip!="idle" && b) {
        const avkit::Clip *c=b->clip(clip);
        if (!c || float(now-clip_at)/1000.0f>=c->seconds()) {clip="idle"; clip_at=now;}
    }
    return float(now-clip_at)/1000.0f;
}
void apply_look() {
    const bool had_animation=animation_row();
    if (painter) painter->set_look(look);
    if (had_animation!=animation_row()) model.avatar_category+=animation_row()?1:-1;
    model.avatar_categories=rows();
    model.avatar_category=std::clamp(model.avatar_category,0,std::max(0,rows()-1));
    changed_at=SDL_GetTicks(); hub.drawn=stage.drawn=0;
}
std::string look_text() {return avkit::save_look(look)+"imported="+(imported?"1":"0")+"\n";}
void save() {
    const std::string tmp=std::string(look_path)+".tmp";
    if (FILE *f=fopen(tmp.c_str(),"w")) {
        const std::string text=look_text();
        fwrite(text.data(),1,text.size(),f);
        fclose(f);
        rename(tmp.c_str(),look_path);
    }
}
// The head shot used as a gamerpic: the avatar on a green backdrop.
SDL_Surface *make_head_shot() {
    if (!painter || !painter->body()) return nullptr;
    avkit::Image img; img.resize(256,256);
    avkit::Camera cam=painter->head_camera();
    cam.span*=0.82f;
    cam.center_y+=0.03f;
    cam.yaw=painter->has_front_trace()?0.0f:0.18f;
    painter->render(img,painter->pose("idle",0.3f),cam,3);
    SDL_Surface *pic=SDL_CreateRGBSurfaceWithFormat(0,256,256,32,SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *face=SDL_CreateRGBSurfaceWithFormatFrom(img.argb.data(),256,256,32,256*4,SDL_PIXELFORMAT_ARGB8888);
    if (!pic || !face) {if (pic) SDL_FreeSurface(pic); if (face) SDL_FreeSurface(face); return nullptr;}
    for (int y=0;y<256;y++) {
        const float t=float(y)/255.0f;
        SDL_Rect row{0,y,256,1};
        SDL_FillRect(pic,&row,SDL_MapRGB(pic->format,Uint8(150-70*t),Uint8(205-80*t),Uint8(90-50*t)));
    }
    SDL_SetSurfaceBlendMode(face,SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(face,nullptr,pic,nullptr);
    SDL_FreeSurface(face);
    return pic;
}
// Puts the current head shot into the gamerpics (replacing the last one).
void refresh_head_shot() {
    clear_scaled_art();
    SDL_Surface *pic=active()?make_head_shot():load_png(std::string(root)+"private_art/avatar/avatar_head.png");
    auto it=std::find(gamerpics.begin(),gamerpics.end(),head_shot);
    if (head_shot && it!=gamerpics.end()) {
        if (pic) *it=pic; else gamerpics.erase(it);
        SDL_FreeSurface(head_shot);
    } else if (pic) gamerpics.insert(gamerpics.begin(),pic);
    head_shot=pic;
}
void load() {
    const Uint32 started=SDL_GetTicks();
    // Your own 360's avatar items, if you built and installed that private kit
    // (tools/build_avatar_kit360.py); otherwise the CC0 kit that ships.
    if (kit.load_file((std::string(root)+"private_art/avatar/kit360.nxak").c_str())) {
        look_path="/data/nxe/avatar_look360.txt";
        log("avatar kit: the private 360 kit (private_art/avatar/kit360.nxak)");
    } else if (!kit.load_file((std::string(root)+"avatar/kit.nxak").c_str())) {log("avatar kit: none (assets/avatar/kit.nxak not installed)"); return;}
    avkit::use_kit(&kit);
    look=avkit::fresh_look();
    painter=std::make_unique<avkit::Renderer>(kit);
#ifdef __PROSPERO__
    painter->set_threads(3); // three helper threads, each drawing a band
#else
    painter->set_threads(1);
#endif
    std::string text;
    if (FILE *f=fopen(look_path,"r")) {
        char buf[4096]; size_t n;
        while ((n=fread(buf,1,sizeof buf,f))>0 && text.size()<65536) text.append(buf,n);
        fclose(f);
        look=avkit::parse_look(text);
        imported=text.find("imported=1")!=std::string::npos;
    }
    ready=true;
    apply_look();
    model.avatar_editable=true;
    model.avatar_categories=rows();
    if (active()) model.avatar_angles=kTurns;
    char line[128];
    std::snprintf(line,sizeof line,"avatar kit: %zu parts, %zu textures, %zu clips (%u ms)",kit.parts.size(),kit.textures.size(),
                  kit.bodies.empty()?size_t(0):kit.bodies[0].clips.size(),unsigned(SDL_GetTicks()-started));
    log(line);
}
// Draws into a canvas, at most every `every` ms unless the look just changed.
SDL_Surface *render(Canvas &c, int w, int h, const avkit::Camera &cam, Uint32 now, Uint32 every) {
    if (!active() || !painter) return nullptr;
    SDL_Surface *surface=c.ensure(w,h);
    if (!surface) return nullptr;
    if (c.drawn && now-c.drawn<every) return surface;
    c.drawn=now?now:1;
    const Uint64 started=SDL_GetPerformanceCounter();
    painter->render(c.img,painter->pose(clip,clip_time(now)),cam,2);
    // How long drawing takes on this console: the first few, then now and then.
    static unsigned renders=0;
    if (++renders<=4 || renders%600==0) {
        char line[96];
        std::snprintf(line,sizeof line,"avatar: %dx%d drawn in %.1f ms",w,h,
                      1000.0*double(SDL_GetPerformanceCounter()-started)/double(SDL_GetPerformanceFrequency()));
        log(line);
    }
    return surface;
}
// On My Xbox: idling, with a wave, a dance or a chat now and then.
SDL_Surface *hub_frame(Uint32 now) {
    if (!next_emote) next_emote=now+20000;
    if (clip=="idle" && Sint32(now-next_emote)>=0) {
        static const char *emotes[]={"wave","talk","wave","dance"};
        play(emotes[(now/1000u)%4u],now);
        next_emote=now+25000+(now*7919u)%20000u;
    }
    avkit::Camera cam=avkit::Camera::full();
    cam.yaw=painter && painter->has_front_trace()?0.0f:-0.22f;
    return render(hub,400,660,cam,now,66);
}
float turn_yaw() {return -float(model.viewer_item)*6.2831853f/float(kTurns);}
// The stage and the editor: the angle Left / Right chose, eased.
void ease(Uint32 now, bool head) {
    static Uint32 last=0;
    const float dt=last?std::min(0.1f,float(now-last)/1000.0f):0.0f;
    last=now;
    float target=turn_yaw(), d=target-yaw;
    while (d>3.14159265f) d-=6.2831853f;
    while (d<-3.14159265f) d+=6.2831853f;
    yaw+=d*std::min(1.0f,dt*10);
    zoom+=((head?1.0f:0.0f)-zoom)*std::min(1.0f,dt*7);
}
avkit::Camera stage_camera() {
    avkit::Camera full=avkit::Camera::full(), head=painter?painter->head_camera():full;
    const float e=zoom*zoom*(3-2*zoom);
    avkit::Camera c;
    c.center_y=full.center_y+(head.center_y-full.center_y)*e;
    c.span=full.span+(head.span-full.span)*e;
    c.yaw=yaw;
    return c;
}
void close() {
    hub.free(); stage.free();
    avkit::use_kit(nullptr);
    painter.reset();
    head_shot=nullptr; // freed with the gamerpics
}
// The editor's controls.
void start_edit() {saved=look; saved_imported=imported; play("idle",SDL_GetTicks());}
void change(int row, int by) {
    const int cat=category_of(row);
    if (cat==-2) {
        const auto *body=painter->body(); const int count=int(body->clips.size());
        if (count) {animation_choice=(animation_choice+by%count+count)%count; play(body->clips[size_t(animation_choice)].name,SDL_GetTicks());}
        return;
    }
    if (cat<0) {imported=!imported; model.avatar_angles=active()?kTurns:avatar::turn.count(); apply_look(); return;}
    avkit::step(look,cat,by);
    // A Quaternius outfit piece starts in its own colours ("Original").
    const std::string name=avkit::categories()[size_t(cat)].label;
    if (avkit::catalog_kit()) {apply_look(); return;}
    if (name=="Top" && avkit::kTops[look.top].textured) look.top_colour=0;
    if (name=="Bottoms" && avkit::kBottoms[look.bottom].textured) look.bottom_colour=0;
    if (name=="Shoes" && avkit::kShoes[look.shoes].textured) look.shoes_colour=0;
    if (name=="Hat" && avkit::kHats[look.hat].textured) look.hat_colour=0;
    apply_look();
}
void randomize(Uint32 now) {look=avkit::random_look(now*2654435761u+1u); apply_look(); play("wave",now);}
void emote(Uint32 now) {
    static const char *list[]={"wave","dance","talk","interact","jump"};
    static int at=0;
    const avkit::Body *body=painter?painter->body():nullptr;
    if (painter && painter->has_custom_rig() && body && body->clips.size()>1) {
        const size_t index=size_t(std::clamp(animation_choice,0,int(body->clips.size())-1));
        play(body->clips[index].name,now);
    } else play(list[at++%5],now);
}
// Saving makes the new head shot the gamerpic, as the 360 offered.
void keep() {
    save();
    refresh_head_shot();
    model.avatar_angles=active()?kTurns:avatar::turn.count();
    auto it=std::find(gamerpics.begin(),gamerpics.end(),head_shot);
    if (head_shot && it!=gamerpics.end()) {
        gamerpic=int(it-gamerpics.begin());
        if (FILE *f=fopen(gamerpic_path,"w")) {fprintf(f,"%d\n",gamerpic); fclose(f);}
    }
}
void cancel() {look=saved; imported=saved_imported; apply_look(); model.avatar_angles=active()?kTurns:avatar::turn.count();}
}
