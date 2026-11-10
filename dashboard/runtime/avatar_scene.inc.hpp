// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
namespace look3d {
void invalidate_preparation(bool reload_saved=true);
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
        w*=render_density::factor;h*=render_density::factor;
        if (surface && img.w==w && img.h==h) return surface;
        if (surface) SDL_FreeSurface(surface);
        img.resize(w,h);
        surface=SDL_CreateRGBSurfaceWithFormatFrom(img.argb.data(),w,h,32,w*4,SDL_PIXELFORMAT_ARGB8888);
        if (surface) {render_density::tag(surface,render_density::factor);SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);}
        drawn=0;
        return surface;
    }
    void free() {if (surface) SDL_FreeSurface(surface); surface=nullptr; img.resize(0,0);}
};
Canvas hub, stage;
// Three deterministic offline friends rendered from the same owner-supplied
// avatar kit as the signed-in avatar.  They are prepared once on the avatar
// worker and then remain stable GPU-uploadable surfaces; Social/Theater never
// fall back to the old coloured mannequin primitives.
constexpr int kLocalGuestCount=3;
std::array<avkit::Image,kLocalGuestCount> local_guest_images;
std::array<SDL_Surface*,kLocalGuestCount> local_guest_surfaces{};
SDL_Surface *local_guest(int index) {
    return index>=0 && index<kLocalGuestCount?local_guest_surfaces[size_t(index)]:nullptr;
}
void release_local_guests() {
    for(SDL_Surface*& surface:local_guest_surfaces){if(surface)SDL_FreeSurface(surface);surface=nullptr;}
    for(auto& image:local_guest_images)image.resize(0,0);
}
void publish_local_guests(std::array<avkit::Image,kLocalGuestCount>& images,int density) {
    release_local_guests();
    for(int i=0;i<kLocalGuestCount;++i) {
        local_guest_images[size_t(i)]=std::move(images[size_t(i)]);
        auto& image=local_guest_images[size_t(i)];
        if(!image.w||!image.h||image.argb.empty())continue;
        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(image.argb.data(),image.w,image.h,32,image.w*4,SDL_PIXELFORMAT_ARGB8888);
        if(surface){render_density::tag(surface,density);avatar_alpha::clean(surface);SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);}
        local_guest_surfaces[size_t(i)]=surface;
    }
}
SDL_Surface *head_shot=nullptr; // the avatar gamerpic (also in gamerpics)
// What the avatar is doing: a clip, from when.
std::string clip="idle";
Uint32 clip_at=0, changed_at=0;
exbox::dashboard_director::Director behavior_director;
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
void play(const std::string &name, Uint32 now) {clip=name;clip_at=now;hub.drawn=stage.drawn=0;}
exbox::dashboard_director::AvatarCapabilities clip_capabilities() {
    const avkit::Body*body=painter?painter->body():nullptr;
    return {body&&body->clip("idle"),body&&body->clip("wave"),body&&body->clip("dance"),body&&body->clip("look"),false};
}
bool moment(exbox::dashboard_director::AvatarMoment value,Uint32 now) {
    if(!active())return false;const auto cue=exbox::dashboard_director::avatar_cue(value,clip_capabilities());
    if(!cue)return false;play(std::string(cue.clip),now);return true;
}
bool director_started=false;
void observe_dashboard(const exbox::dashboard_director::Observation& observation,
                       const exbox::dashboard_director::Capabilities& capabilities,
                       Uint32 now,bool avatar_visible) {
    const auto plan=behavior_director.observe(observation,capabilities);
    if(!director_started){director_started=true;if(avatar_visible&&clip=="idle")moment(exbox::dashboard_director::AvatarMoment::Startup,now);}
    for(std::size_t i=0;i<plan.count;++i){
        const auto event=plan.actions[i].event;
        if(avatar_visible&&clip=="idle"&&event==exbox::dashboard_director::Event::ViewEnter)moment(exbox::dashboard_director::AvatarMoment::EnterStage,now);
        else if(avatar_visible&&clip=="idle"&&event==exbox::dashboard_director::Event::LaunchReturn)moment(exbox::dashboard_director::AvatarMoment::Return,now);
        // Focus look-at remains deliberately inactive: no original tile-to-head
        // binding is proven. Guide/notification/equalizer actions are consumed
        // by their existing recovered scene players.
    }
}
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
    invalidate_preparation(false);
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
avkit::Image render_head_pixels(avkit::Renderer &renderer,int density) {
    avkit::Image img;if(!renderer.body())return img;
    const int side=256*density;img.resize(side,side);
    avkit::Camera cam=renderer.head_camera();cam.span*=0.82f;cam.center_y+=0.03f;
    cam.yaw=renderer.has_front_trace()?0.0f:0.18f;
    renderer.render(img,renderer.pose("idle",0.3f),cam,3);return img;
}
SDL_Surface *compose_head_pixels(avkit::Image &img,int density) {
    const int side=256*density;if(img.w!=side||img.h!=side)return nullptr;
    SDL_Surface *pic=SDL_CreateRGBSurfaceWithFormat(0,side,side,32,SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *face=SDL_CreateRGBSurfaceWithFormatFrom(img.argb.data(),side,side,32,side*4,SDL_PIXELFORMAT_ARGB8888);
    if(!pic||!face){if(pic)SDL_FreeSurface(pic);if(face)SDL_FreeSurface(face);return nullptr;}
    for(int y=0;y<side;y++){
        const float t=float(y)/float(side-1);SDL_Rect row{0,y,side,1};
        SDL_FillRect(pic,&row,SDL_MapRGB(pic->format,Uint8(150-70*t),Uint8(205-80*t),Uint8(90-50*t)));
    }
    SDL_SetSurfaceBlendMode(face,SDL_BLENDMODE_BLEND);SDL_UpperBlit(face,nullptr,pic,nullptr);SDL_FreeSurface(face);
    return render_density::tag(pic,density);
}
SDL_Surface *make_head_shot() {
    if(!painter)return nullptr;auto img=render_head_pixels(*painter,render_density::factor);
    return compose_head_pixels(img,render_density::factor);
}
// Puts the current head shot into the gamerpics (replacing the last one).
void refresh_head_shot(SDL_Surface *prepared=nullptr,bool supplied=false) {
    SDL_Surface *selected=gamerpic>=0 && gamerpic<int(gamerpics.size())?gamerpics[size_t(gamerpic)]:nullptr;
    const bool selected_head=selected && selected==head_shot;
    invalidate_scaled_art(head_shot);
    SDL_Surface *pic=supplied?prepared:(active()?make_head_shot():load_png(std::string(root)+"private_art/avatar/avatar_head.png"));
    auto it=std::find(gamerpics.begin(),gamerpics.end(),head_shot);
    if (head_shot && it!=gamerpics.end()) {
        if (pic) *it=pic; else gamerpics.erase(it);
        SDL_FreeSurface(head_shot);
    } else if (pic) gamerpics.insert(gamerpics.begin(),pic);
    head_shot=pic;
    if (selected && !selected_head) {
        auto selected_at=std::find(gamerpics.begin(),gamerpics.end(),selected);
        if(selected_at!=gamerpics.end())gamerpic=int(selected_at-gamerpics.begin());
    } else if(selected_head && pic) {
        gamerpic=int(std::find(gamerpics.begin(),gamerpics.end(),pic)-gamerpics.begin());
    } else gamerpic=std::clamp(gamerpic,0,std::max(0,int(gamerpics.size())-1));
}
struct LookSource {
    std::string bytes;bool present=false,valid=true;
    bool operator==(const LookSource& b)const{return present==b.present&&valid==b.valid&&bytes==b.bytes;}
};
LookSource read_look_source(const std::string& path){
    LookSource out;FILE* f=fopen(path.c_str(),"rb");if(!f)return out;
    out.present=true;char buf[4096];while(out.bytes.size()<65536){const size_t want=std::min(sizeof buf,size_t(65536-out.bytes.size()));const size_t n=fread(buf,1,want,f);out.bytes.append(buf,n);if(n<want)break;}
    if(ferror(f)||fgetc(f)!=EOF)out.valid=false;fclose(f);return out;
}
struct PreparedAvatar {
    std::unique_ptr<avkit::Renderer> renderer;avkit::Image head;avkit::Look selected;
    std::array<avkit::Image,kLocalGuestCount> local_guests;
    LookSource before,after;std::string path;std::uint64_t generation=0;
    int density=1,helpers=1;bool render_head=false,unchanged=false;
};
exbox::StartupJob<int> kit_job,prep_job;
std::unique_ptr<PreparedAvatar> preparation;
bool kit_requested=false,kit_available=false,needs_preparation=false,reload_preparation=false,preparation_reading=false;
std::uint64_t preparation_generation=0;Uint32 kit_started=0;
std::uint64_t preparation_completion_ms(){return preparation_reading?0:prep_job.completion_ms();}
void invalidate_preparation(bool reload_saved){
    if(ready)return;
    ++preparation_generation;reload_preparation=reload_preparation||reload_saved;
    if(kit_available)needs_preparation=true;
}
void parse_saved_look(const LookSource& source){
    look=avkit::fresh_look();imported=false;
    if(source.present){look=avkit::parse_look(source.bytes);imported=source.bytes.find("imported=1")!=std::string::npos;}
    avkit::clamp(look);
}
void begin_preparation(LookSource source,bool parse){
    if(!source.valid){log("startup: avatar look exceeds bounded input; keeping placeholder");needs_preparation=false;return;}
    (void)avkit::categories();if(parse)parse_saved_look(source);else avkit::clamp(look);
    preparation=std::make_unique<PreparedAvatar>();auto* slot=preparation.get();
    slot->selected=look;slot->before=std::move(source);slot->path=look_path;
    slot->density=render_density::factor;slot->generation=preparation_generation;
    slot->render_head=!(imported&&avatar::installed());
#ifdef __PROSPERO__
    slot->helpers=3;
#else
    slot->helpers=1;
#endif
    needs_preparation=false;reload_preparation=false;preparation_reading=false;
    if(!prep_job.start([slot]{
        auto renderer=std::make_unique<avkit::Renderer>(kit);
        renderer->set_threads(slot->helpers);renderer->set_look(slot->selected);
        if(slot->render_head){slot->head=render_head_pixels(*renderer,slot->density);renderer->warm_scratch(400*slot->density,660*slot->density,2);}
        // These are actual kit characters, not geometric stand-ins.  Fixed
        // seeds keep their appearance stable between launches and fixed idle
        // samples avoid per-frame CPU avatar raster work for background guests.
        constexpr std::array<std::uint32_t,kLocalGuestCount> seeds{{0x6400u,0x6404u,0x6411u}};
        for(int i=0;i<kLocalGuestCount;++i) {
            avkit::Renderer guest(kit);guest.set_threads(1);guest.set_look(avkit::random_look(seeds[size_t(i)]));
            auto& image=slot->local_guests[size_t(i)];image.resize(240*slot->density,400*slot->density);
            avkit::Camera camera=avkit::Camera::full();camera.yaw=(i-1)*0.10f;
            guest.render(image,guest.pose("idle",0.35f+0.17f*i),camera,2);
        }
        slot->after=read_look_source(slot->path);slot->unchanged=slot->before==slot->after;
        slot->renderer=std::move(renderer);return 1;
    })){preparation.reset();log("startup: avatar preparation unavailable; keeping placeholder");}
}
void begin_source_read(){
    preparation=std::make_unique<PreparedAvatar>();auto* slot=preparation.get();
    slot->path=look_path;slot->generation=preparation_generation;
    preparation_reading=true;needs_preparation=false;reload_preparation=false;
    if(!prep_job.start([slot]{slot->after=read_look_source(slot->path);return 2;})){
        preparation.reset();log("startup: avatar source read unavailable; keeping placeholder");
    }
}
void load(){
    if(kit_requested)return;kit_requested=true;kit_started=SDL_GetTicks();
    if(!kit_job.start([]{
        if(kit.load_file((std::string(root)+"private_art/avatar/kit360.nxak").c_str()))return 2;
        // The local importer/validation workspace stores the same verified kit
        // directly under avatar/.  Supporting that layout avoids copying an
        // 875 MiB owner file merely to preview the dashboard.
        if(kit.load_file((std::string(root)+"avatar/kit360.nxak").c_str()))return 2;
        return kit.load_file((std::string(root)+"avatar/kit.nxak").c_str())?1:0;
    }))log("startup: avatar worker unavailable; keeping placeholder");
}
bool poll_load(){
    if(ready)return false;
    int loaded=0;if(kit_job.take(loaded)){
        if(!loaded){log("startup: avatar kit unavailable; keeping placeholder");return false;}
        kit_available=true;look_path=loaded==2?"/data/nxe/avatar_look360.txt":"/data/nxe/avatar_look.txt";
        avkit::use_kit(&kit);(void)avkit::categories();needs_preparation=true;reload_preparation=true;
    }
    int prepared=0;if(prep_job.take(prepared)){
        auto result=std::move(preparation);
        if(!prepared||!result){log("startup: avatar preparation failed; keeping placeholder");return false;}
        if(prepared==2){
            if(result->generation!=preparation_generation||reload_preparation){result.reset();begin_source_read();return false;}
            auto source=std::move(result->after);result.reset();begin_preparation(std::move(source),true);return false;
        }
        if(!result->renderer){log("startup: avatar preparation incomplete; keeping placeholder");return false;}
        const bool stale=result->generation!=preparation_generation||result->density!=render_density::factor||!result->unchanged;
        if(stale){
            auto source=std::move(result->after);const bool parse=!result->unchanged;
            result.reset(); // Dispose renderer/helpers before another preparation.
            if(reload_preparation)begin_source_read();else begin_preparation(std::move(source),parse);
            return false;
        }
        const Uint32 publish_started=SDL_GetTicks();painter=std::move(result->renderer);ready=true;
        if(animation_row())++model.avatar_category;
        model.avatar_editable=true;model.avatar_categories=rows();model.avatar_category=std::clamp(model.avatar_category,0,std::max(0,rows()-1));
        changed_at=SDL_GetTicks();hub.drawn=stage.drawn=0;if(active())model.avatar_angles=kTurns;
        publish_local_guests(result->local_guests,result->density);
        if(result->render_head)refresh_head_shot(compose_head_pixels(result->head,result->density),true);else refresh_head_shot();
        if(diagnostic_logging()){char line[192];std::snprintf(line,sizeof line,"startup: avatar publication_ms=%u total_ms=%u",unsigned(SDL_GetTicks()-publish_started),unsigned(SDL_GetTicks()-kit_started));log(line);}
        return true;
    }
    if(kit_available&&needs_preparation&&!prep_job.active())begin_source_read();
    return false;
}

// Shared raster operation; sampling and cadence remain caller-owned.
void raster_sample(Canvas &c,const std::vector<avkit::M34> &pose,const avkit::Camera &cam,int w,int h) {
    const Uint64 started=SDL_GetPerformanceCounter();
    painter->render(c.img,pose,cam,2);
    static unsigned renders=0;
    if (diagnostic_logging() && (++renders<=4 || renders%600==0)) {
        char line[96];
        std::snprintf(line,sizeof line,"avatar: %dx%d drawn in %.1f ms",w,h,
                      1000.0*double(SDL_GetPerformanceCounter()-started)/double(SDL_GetPerformanceFrequency()));
        log(line);
    }
}
// Draws into a canvas, at most every `every` ms unless the look just changed.
SDL_Surface *render(Canvas &c, int w, int h, const avkit::Camera &cam, Uint32 now, Uint32 every) {
    if (!active() || !painter) return nullptr;
    SDL_Surface *surface=c.ensure(w,h);
    if (!surface) return nullptr;
    if (c.drawn && now-c.drawn<every) return surface;
    c.drawn=now?now:1;
    raster_sample(c,painter->pose(clip,clip_time(now)),cam,w,h);
    return surface;
}
struct DeferredHub {
    std::vector<avkit::M34> pose;
    avkit::Camera camera;
    avkit::Renderer *owner=nullptr;
    Uint32 tick=0;
    int density=0,width=0,height=0;
    bool pending=false;
};
DeferredHub deferred_hub;
// Called only by the UI owner. A reveal between virtual ticks uses the saved
// sample, never a newer pose. Look changes clear hub.drawn; density/owner guards
// prevent materializing an invalidated sample before its next hub_frame call.
SDL_Surface *materialize_hub() {
    auto &d=deferred_hub;
    if (!active() || !painter || !hub.surface) return nullptr;
    if (d.pending && d.owner==painter.get() && d.tick==hub.drawn && hub.drawn &&
        d.density==render_density::factor && d.width==hub.img.w && d.height==hub.img.h) {
        raster_sample(hub,d.pose,d.camera,400,660);d.pending=false;
    }
    return hub.surface;
}
SDL_Surface *sample_hub(const avkit::Camera &cam,Uint32 now,bool defer_raster) {
    if (!active() || !painter) return nullptr;
    SDL_Surface *surface=hub.ensure(400,660);
    if (!surface) return nullptr;
    if (!hub.drawn || now-hub.drawn>=66) {
        hub.drawn=now?now:1;
        auto &d=deferred_hub;
        d.pose=painter->pose(clip,clip_time(now));d.camera=cam;
        d.owner=painter.get();d.tick=hub.drawn;d.density=render_density::factor;
        d.width=hub.img.w;d.height=hub.img.h;d.pending=true;
    }
    if (!defer_raster)materialize_hub();
    return surface;
}
// On a proven full-body placement: idle with capability-checked look/shift
// fidgets. Selection and celebration moments are explicit, never random.
SDL_Surface *hub_frame(Uint32 now,bool defer_raster=false) {
    const avkit::Body *body=painter?painter->body():nullptr;
    const std::array<bool,2> available{body&&body->clip("look"),body&&body->clip("shift")};
    if(clip=="idle")if(const auto fidget=behavior_director.idle_fidget(now,true,available);!fidget.empty())play(std::string(fidget),now);
    avkit::Camera cam=avkit::Camera::full();
    cam.yaw=painter && painter->has_front_trace()?0.0f:-0.22f;
    return sample_hub(cam,now,defer_raster);
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
    kit_job.join();prep_job.join();
    preparation.reset(); // Destroy unpublished renderer/helpers before catalog teardown.
    ready=false;director_started=false;
    deferred_hub=DeferredHub{};
    hub.free(); stage.free();
    release_local_guests();
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
