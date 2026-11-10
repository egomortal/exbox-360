// Included inside main.cpp's private namespace; see docs/XUI_RUNTIME.md.
// Source-scene rendering: Microsoft's own Blades scene drawn by the XUI painter,
// with the native menus placed into its tabs. On the PS5 it is the default (it
// is what makes Blades one-to-one); /data/nxe/blades_source_off turns it off,
// and it steps back to the converted frames by itself if it cannot keep up.
// On the desktop it stays opt-in (NXE_BLADES_SOURCE=1) for the audit tools.
namespace source_blades {
struct Family {
    xui::Scene scene;
    xui::Renderer painter;
    xui::GpuResources gpu_resources;
    // Immutable commands/publications for the same sample retained by CPU canvas.
    std::shared_ptr<const x21hook::OwnedPacket> gpu_sample;
    xui::Navigation navigation;
    int build=0;
    std::map<std::string,std::string> resources;
    std::map<std::string,sfx::Clip> sounds;
    int tabs[5]{-1,-1,-1,-1,-1};
    // Each tab's pose when its own blade is the settled one. The native menus are
    // already laid out for that resting place, so only the motion away from it
    // (the original transitions) is applied to them.
    xui::Matrix rest[5];
};
// Inverse of an affine 4x4 (bottom row 0 0 0 1).
xui::Matrix affine_inverse(const xui::Matrix &m) {
    const auto &a=m.a;
    const float det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if (std::abs(det)<1e-12f) return xui::Matrix::identity();
    const float k=1/det;
    xui::Matrix r=xui::Matrix::identity();
    r.a[0][0]=(a[1][1]*a[2][2]-a[1][2]*a[2][1])*k;r.a[0][1]=(a[0][2]*a[2][1]-a[0][1]*a[2][2])*k;r.a[0][2]=(a[0][1]*a[1][2]-a[0][2]*a[1][1])*k;
    r.a[1][0]=(a[1][2]*a[2][0]-a[1][0]*a[2][2])*k;r.a[1][1]=(a[0][0]*a[2][2]-a[0][2]*a[2][0])*k;r.a[1][2]=(a[0][2]*a[1][0]-a[0][0]*a[1][2])*k;
    r.a[2][0]=(a[1][0]*a[2][1]-a[1][1]*a[2][0])*k;r.a[2][1]=(a[0][1]*a[2][0]-a[0][0]*a[2][1])*k;r.a[2][2]=(a[0][0]*a[1][1]-a[0][1]*a[1][0])*k;
    for (int row=0;row<3;row++) r.a[row][3]=-(r.a[row][0]*a[0][3]+r.a[row][1]*a[1][3]+r.a[row][2]*a[2][3]);
    return r;
}
Family families[2];
SDL_Surface *content=nullptr;
bool enabled=false,frozen=false,manual=false;
int family=-1,previous=-1,failed_build=0;
std::string scene_directory,scene_font;
Uint32 last_tick=0;
uint64_t last_render_us=0;
int active_blade() {
    int active=0;while (active<4 && dash::kBladesChannels[active]!=model.channel) active++;
    return active;
}
void stop_source_voices(Family &f);
void clear() {
    enabled=false;
    for (auto &f:families) {stop_source_voices(f);render_density::forget_surface(f.painter.canvas);f.painter.clear();f.gpu_sample.reset();f.gpu_resources.clear();f.sounds.clear();f.build=0;}
    SDL_FreeSurface(content);content=nullptr;
}
void stop_source_voices(Family &f) {
    if (sfx::device) SDL_LockAudioDevice(sfx::device);
    for (auto &voice:sfx::voices) for (const auto &clip:f.sounds) if (voice.clip==&clip.second) voice.clip=nullptr;
    if (sfx::device) SDL_UnlockAudioDevice(sfx::device);
}
// Painter threads: the PS5 gives a big-app about seven cores; the avatar and the
// main loop keep one. The desktop uses what it has.
#ifdef __PROSPERO__
unsigned kWorkers=8;   // measured: 6 threads 20.8 ms, 8 threads 19.0 ms; /data/nxe/blades_workers overrides
#else
unsigned kWorkers=0;
#endif
// The slow-console guard: frame times of the last stretch, and the limit.
std::vector<float> recent_ms;
constexpr float kTooSlowMs=45.f;
void prepare(const char *font) {
#ifndef __PROSPERO__
    const char *option=getenv("NXE_BLADES_SOURCE");
    if (!option || std::strcmp(option,"1")) return;
#else
    if (FILE *off=fopen("/data/nxe/blades_source_off","r")) {fclose(off);log("Blades source scene: off (blades_source_off)");return;}
    if (FILE *count=fopen("/data/nxe/blades_workers","r")) {
        int n=0;if (fscanf(count,"%d",&n)==1 && n>=1 && n<=8) kWorkers=unsigned(n);
        fclose(count);
    }
    {char line[64];std::snprintf(line,sizeof line,"Blades source scene: %u painter threads",kWorkers);log(line);}
#endif
    scene_directory=std::string(root)+"era-skins/blades/scene";scene_font=font;
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) scene_directory=std::string(override)+"/blades/scene";
#endif
    content=render_density::create(W,H);
    if (!content) return;
    SDL_SetSurfaceBlendMode(content,SDL_BLENDMODE_BLEND);enabled=true;
    log("Blades source scene: on");
}
bool load_revision(Family &f,int build,int active) {
    // One revision's painter at a time: free the other family's canvas, scratch and art.
    for (auto &other:families) if (&other!=&f && other.build) {stop_source_voices(other);render_density::forget_surface(other.painter.canvas);other.painter.clear();other.gpu_sample.reset();other.gpu_resources.clear();other.sounds.clear();other.build=0;}
    stop_source_voices(f);f.gpu_sample.reset();f.sounds.clear();f.resources.clear();f.build=0;
    render_density::forget_surface(f.painter.canvas);
    if (!f.scene.load((scene_directory+"/"+std::to_string(build)+".nxui").c_str()) ||
        !f.painter.prepare(f.scene,W*render_density::factor,H*render_density::factor,scene_font.c_str(),kWorkers) || !f.navigation.bind(f.scene,build<5759,active)) return false;
    render_density::tag(f.painter.canvas,render_density::factor);
    const std::string base=scene_directory+"/resources/";
    std::ifstream manifest(base+std::to_string(build)+".tsv");std::string line;
    while (std::getline(manifest,line)) {
        const auto tab=line.find('\t');if (tab==std::string::npos) continue;
        const auto key=line.substr(0,tab),file=line.substr(tab+1);
        if (file.find("..")!=std::string::npos || file.find(':')!=std::string::npos || file.empty() || file[0]=='/') continue;
        f.resources[key]=base+file;
        if (key.size()>4 && (key.substr(key.size()-4)==".xma" || key.substr(key.size()-4)==".wav")) {
            sfx::Clip clip;if (sfx::load_path(base+file,clip)) f.sounds.emplace(key,std::move(clip));
        }
    }
    f.painter.bind_font("ConvectionUI",scene_font);f.painter.bind_font("Convection",scene_font);
    f.painter.bind_bitmaps([&f](const std::string &key)->SDL_Surface * {
        const auto found=f.resources.find(key);if (found==f.resources.end()) return nullptr;
        auto *raw=IMG_Load(found->second.c_str());if (!raw) return nullptr;
        auto *surface=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);return surface;
    });
    f.gpu_resources.prepare(f.scene,scene_font.c_str());f.gpu_resources.bind_font("ConvectionUI",scene_font);f.gpu_resources.bind_font("Convection",scene_font);
    f.gpu_resources.bind_bitmaps([&f](const std::string&key)->SDL_Surface*{auto found=f.resources.find(key);if(found==f.resources.end())return nullptr;auto*raw=IMG_Load(found->second.c_str());if(!raw)return nullptr;auto*image=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);return image;});
    for (int i=0;i<(build<5759?4:5);i++) {char name[12];snprintf(name,sizeof name,"Tab%d",i+1);f.tabs[i]=f.scene.find(name);}
    // Record where each tab rests when its blade is settled, then go back.
    const int offset=build<5759?1:0;
    for (int i=0;i<(build<5759?4:5);i++) {
        f.rest[i]=xui::Matrix::identity();
        if (f.tabs[i]<0) continue;
        // The first blade's resting place is the end of the transition into it
        // (frame 0 is the scene's entry pose, a little further right).
        const bool placed=i==0?f.scene.seek_name(0,"2To1End",false):f.navigation.reset(i+offset);
        if (placed) {f.scene.evaluate();f.rest[i]=f.scene.world[f.tabs[i]].matrix;}
    }
    if (!f.navigation.reset(active)) return false;
    f.build=build;return true;
}
void dispatch_sounds(Family &f) {
    for (const auto &event:f.scene.sound_events) {
        bool native_content=false;
        for (int node=event.node;node>=0;node=f.scene.nodes[node].parent) {
            const char *id=f.scene.str(f.scene.nodes[node].id);
            if (!std::strncmp(id,"Tab",3) && id[3]>='1' && id[3]<='6' && !id[4]) {native_content=true;break;}
        }
        if (native_content || !f.scene.world[event.node].visible) continue;
        const auto found=f.sounds.find(f.scene.str(event.file));if (found!=f.sounds.end()) sfx::play(&found->second);
    }
    f.scene.sound_events.clear();
}
bool owns_switch_sound() {return enabled && family>=0 && !families[family].sounds.empty();}
void update(Uint32 now) {
    if (model.dashboard_build==failed_build) return;
    const int which=model.dashboard_build<5759?1:0,active=active_blade();
    if (family!=which || families[which].build!=model.dashboard_build) {
        family=which;
        if (!load_revision(families[family],model.dashboard_build,active)) {log("Source revision unavailable");failed_build=model.dashboard_build;return;}
        failed_build=0;
        previous=active;last_tick=now;frozen=manual=false;
    }
    auto &f=families[family];
    if (active!=previous) {frozen=manual=false;f.navigation.request(active);previous=active;}
    const float frames=float(now-last_tick)*60/1000;last_tick=now;
    if (!frozen) {
        if (manual) {f.scene.advance(frames);f.scene.evaluate();}
        else f.navigation.advance(frames);
        dispatch_sounds(f);
    }
}
bool owns_hints() {
    return enabled && model.era==0 && model.viewer==dash::Viewer::None && !model.info && !model.details && !model.controller_map;
}
bool draw(Uint32 now) {
    if (!enabled) return false;
    update(now);if (!enabled || family<0 || families[family].build!=model.dashboard_build) return false;auto &f=families[family];
    const auto start=std::chrono::steady_clock::now();
    const auto view=xui::viewport(W,H);
    // At rest only the slow background swirl moves: repaint the scene every
    // other frame then (each frame while a blade moves), so the console holds
    // 60 Hz; the hub content above it still draws every frame.
    static unsigned rest_frames=0;
    const bool moving=f.navigation.transitioning() || manual;
    rest_frames=moving?0:rest_frames+1;
    const bool painted=moving || (rest_frames&1) || rest_frames<2;
    const float density=float(render_density::factor);
    bool gpu=x21hook::allow_frame;std::unique_ptr<TabSink> gpu_sink;
    const int pixel_width=W*render_density::factor,pixel_height=H*render_density::factor;
    if(f.gpu_sample&&(f.gpu_sample->width!=pixel_width||f.gpu_sample->height!=pixel_height))f.gpu_sample.reset();
    if(painted){
        // Timeline/navigation/events still advance above on every original tick.
        // Only the background sample follows the original painter cadence.
        f.gpu_sample.reset();
        if(gpu)try{
            TabSink sample(pixel_width,pixel_height);
            sample.packet.extract(f.scene,f.gpu_resources,xui::Matrix::scale(density,density)*view,pixel_width,pixel_height);
            f.gpu_sample=x21hook::own(sample,unsigned(f.build));
        }catch(const std::exception&){f.gpu_sample.reset();}
    }
    // No stale sample after CPU-only paint, revision/density change or failure.
    // Use the original CPU path until an original paint tick publishes a sample.
    if(gpu&&!f.gpu_sample){gpu=false;x21hook::allow_frame=false;}
    if(gpu){
        gpu_sink=std::make_unique<TabSink>(pixel_width,pixel_height);
        gpu_sink->packet.commands=f.gpu_sample->commands;
        gpu_sink->publications=f.gpu_sample->publications;
    }
    if (painted && (!gpu||x21hook::audit_cpu())) f.painter.draw(f.scene,xui::Matrix::scale(density,density)*view);
    if(!gpu||x21hook::audit_cpu())SDL_BlitSurface(f.painter.canvas,nullptr,screen,nullptr);
    SDL_Surface *target=screen;const int saved_channel=model.channel;
    const auto inverse_view=xui::inverse_viewport(W,H);
    for (int tab=0;tab<(family?4:5);tab++) {
        const int index=f.tabs[tab];if (index<0) continue;
        const auto &pose=f.scene.world[index];if (!pose.visible || pose.opacity<=0) continue;
        if(!gpu||x21hook::audit_cpu())SDL_FillRect(content,nullptr,0);screen=content;
        model.channel=dash::kBladesChannels[tab+(family?1:0)];
        if(gpu){gpu_sink->begin_tab();x21hook::active=gpu_sink.get();x21hook::target=content;}
        draw_blades_hub(false,model.channel==saved_channel);
        draw_hints(1001,{232,238,226,255});
        x21hook::active=nullptr;x21hook::target=nullptr;
        screen=target;model.channel=saved_channel;
        const auto transform=view*pose.matrix*affine_inverse(f.rest[tab])*inverse_view;
        if(gpu)gpu_sink->end_tab(xui::Matrix::scale(density,density)*transform*xui::Matrix::scale(1/density,1/density),pose.opacity);
        if(gpu&&!x21hook::audit_cpu())continue;
        const auto origin=transform.point(0,0),x=transform.point(1,0),y=transform.point(0,1);
        if (std::abs(origin.x)<.001f && std::abs(origin.y)<.001f && std::abs(x.x-1)<.001f && std::abs(x.y)<.001f && std::abs(y.x)<.001f && std::abs(y.y-1)<.001f) {
            SDL_SetSurfaceAlphaMod(content,Uint8(std::lround(pose.opacity*255)));SDL_BlitSurface(content,nullptr,target,nullptr);SDL_SetSurfaceAlphaMod(content,255);
        } else f.painter.texture(target,content,xui::Matrix::scale(density,density)*transform*xui::Matrix::scale(1/density,1/density),pose.opacity,{0,0,target->w,target->h});
    }
    if(gpu){x21hook::ready=x21hook::own(*gpu_sink,unsigned(f.build));if(x21hook::ready)x21hook::ready->channel=saved_channel;if(!x21hook::ready){for(const auto&why:gpu_sink->packet.unsupported)log(("GPU unsupported: "+why).c_str());log("GPU packet refused: CPU fallback for this frame");x21hook::allow_frame=false;return draw(now);}}
    last_render_us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
    // Timing: logged now and then; if the median of a stretch is too slow for the
    // console, step back to the converted frames rather than stutter.
    if (painted && !gpu) recent_ms.push_back(float(last_render_us)/1000.f);
    if (recent_ms.size()>=180) {
        std::vector<float> sorted=recent_ms;std::sort(sorted.begin(),sorted.end());
        const float median=sorted[sorted.size()/2],p95=sorted[sorted.size()*95/100];
        char line[160];std::snprintf(line,sizeof line,"Blades source scene: build %d, median %.1f ms, p95 %.1f ms",f.build,median,p95);log(line);
        recent_ms.clear();
        if (median>kTooSlowMs) {log("Blades source scene: too slow here; using the converted frames");clear();}
    }
    return true;
}
[[maybe_unused]] void seek(float frame) {
    if (!enabled) return;
    update(SDL_GetTicks());families[family].scene.seek(0,std::max(0.f,frame));families[family].scene.evaluate();frozen=true;manual=false;
}
[[maybe_unused]] void loop(float frame) {
    if (!enabled) return;
    update(SDL_GetTicks());auto &s=families[family].scene;
    for (size_t i=0;i<s.animations.size();i++) if (s.animations[i].automatic) s.playback[i].frame=s.animations[i].last>0?std::fmod(std::max(0.f,frame),s.animations[i].last):0;
    s.evaluate();
}
[[maybe_unused]] void play(const std::string &name) {
    if (!enabled) return;
    update(SDL_GetTicks());auto &s=families[family].scene;
    if (s.seek_name(0,name,true)) {frozen=false;manual=true;last_tick=SDL_GetTicks();}
}
[[maybe_unused]] void report(const char *path) {
    if (!enabled || family<0) return;
    auto &f=families[family];const auto &s=f.scene;
    FILE *file=std::fopen(path,"w");if (!file) return;
    std::fprintf(file,"{\"family\":%d,\"current\":%d,\"target\":%d,\"frame\":%.5f,\"transitioning\":%s,\"render_us\":%llu,\"nodes\":%zu,\"tracks\":%zu,\"poses\":[",f.build,f.navigation.current(),f.navigation.target(),f.navigation.frame(),f.navigation.transitioning()?"true":"false",(unsigned long long)last_render_us,s.nodes.size(),s.tracks.size());
    bool comma=false;
    for (size_t i=0;i<s.nodes.size();i++) {
        const char *id=s.str(s.nodes[i].id);
        if (std::strncmp(id,"blade_",6) && std::strncmp(id,"Tab",3)) continue;
        const auto &w=s.world[i];
        if (comma) std::fputs(",",file);
        comma=true;std::fprintf(file,"{\"node\":%zu,\"visible\":%s,\"opacity\":%.6f,\"matrix\":[",i,w.visible?"true":"false",w.opacity);
        for (int r=0;r<4;r++) for (int c=0;c<4;c++) std::fprintf(file,"%s%.7f",r||c?",":"",w.matrix.a[r][c]);
        std::fputs("]}",file);
    }
    std::fputs("]}\n",file);std::fclose(file);
}
}

