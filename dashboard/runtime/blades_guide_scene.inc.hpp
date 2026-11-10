// Included inside main.cpp's private namespace; see docs/XUI_RUNTIME.md.
// The launch (1888) Xbox Guide drawn from its own scenes: xam's hudbkgnd (the
// metal base, the blade, clock, gamertag spine, legends), hud's MainMenuSignedIn
// with MiniMediaPlayer and CommunityBar in its placeholder scenes, xam's
// GamerCard in "Mini" mode, all skinned by huduiskin's skin.xur. The pack and
// the art are the owner's (private/era-skins/blades/guide-1888); without them
// the hand-measured panel in ui/guide.inc.hpp is used instead.
//
// The painter blends opaquely, so a state is painted twice, over black and over
// white, and the true colour and coverage are recovered from the pair; the
// result is cached and only repainted when what it shows changes.
namespace guide_scene {
std::uint64_t cpu_paints=0;
std::uint64_t render_epoch=0; // Advances once per dashboard frame, even while the Guide is closed.
std::string font_path;
// One era's Guide: its pack, painter and the cached, colour-recovered state.
struct Core {
    xui::Scene scene;
    xui::Renderer painter;
    xui::GpuResources gpu_resources;Extractor gpu_packet;
    bool gpu_ready=false,gpu_dirty=true,cpu_valid=false;
    bool ready=false,tried=false;
    int raster_density=0;
    std::string art_directory,last_key;
    SDL_Surface *cached=nullptr,*black=nullptr,*white=nullptr,*first=nullptr;
    Uint32 last_draw=0,last_tick=0;
    std::uint64_t last_epoch=0;
    int focused=-1;
    SDL_Rect area{0,0,0,0};     // the screen rectangle the Guide can touch
    xui::Matrix to_screen;     // HUD canvas to 1080p
    int node(const char *id,int ancestor=-1) const {return scene.find(id,ancestor);}
    // The skin visual the compiler hung under a control ("@visual:<name>").
    int visual_of(int control) const {
        if (control<0) return -1;
        for (size_t i=size_t(control)+1;i<scene.nodes.size();i++)
            if (scene.nodes[i].parent==control && !std::strncmp(scene.str(scene.nodes[i].id),"@visual:",8)) return int(i);
        return -1;
    }
    void show(const char *id,bool on,int ancestor=-1) {scene.bind_show(node(id,ancestor),on);}
    void text_of(int n,const std::string &value) {if (n>=0) scene.bind_string(n,xui::Text,value);}
    void clear() {
        gpu_resources.clear();gpu_packet=Extractor{};gpu_ready=false;gpu_dirty=true;cpu_valid=false;
        render_density::forget_surface(painter.canvas);painter.clear();orbs.clear();
        for (auto **s:{&cached,&black,&white,&first}) {SDL_FreeSurface(*s);*s=nullptr;}
        ready=false;last_epoch=0;last_key.clear();
    }
    bool load(const char *folder,const char *pack,SDL_Rect where,xui::Matrix view,const char *label) {
        if (tried && raster_density==render_density::factor) return ready;
        if (tried) clear();
        raster_density=render_density::factor;tried=true;area=where;to_screen=view;
        std::string base=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
        if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) base=std::string(override)+"/";
#endif
        art_directory=base+folder+"/";
        char line[160];
        if (!scene.load((art_directory+pack).c_str())) {std::snprintf(line,sizeof line,"%s Guide scene: not installed",label);log(line);return false;}
        if (!painter.prepare(scene,area.w*raster_density,area.h*raster_density,font_path.c_str(),source_blades::kWorkers)) {std::snprintf(line,sizeof line,"%s Guide scene: painter failed",label);log(line);return false;}
        render_density::tag(painter.canvas,raster_density);
        painter.bind_font("Convection",font_path);
        gpu_ready=gpu_resources.prepare(scene,font_path.c_str());
        if(gpu_ready)gpu_resources.bind_font("Convection",font_path);
        const std::string dir=art_directory;
        auto bitmap_loader=[dir](const std::string &key)->SDL_Surface * {
            std::string name=key;
            for (const char *prefix:{"sharedres://","file://","common://","xam://"}) if (!name.rfind(prefix,0)) name=name.substr(std::strlen(prefix));
            if (name.empty() || name.find("..")!=std::string::npos || name.find('/')!=std::string::npos) return nullptr;
            SDL_Surface *raw=IMG_Load((dir+name).c_str());
            if (!raw) return nullptr;
            SDL_Surface *out=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);return out;
        };
        painter.bind_bitmaps(bitmap_loader);if(gpu_ready)gpu_resources.bind_bitmaps(bitmap_loader);
        for (auto **s:{&black,&white,&cached,&first}) *s=render_density::create(area.w,area.h);
        if (!black || !white || !cached || !first) {clear();return false;}
        SDL_SetSurfaceBlendMode(first,SDL_BLENDMODE_NONE);
        SDL_FillRect(black,nullptr,0xff000000u);SDL_FillRect(white,nullptr,0xffffffffu);
        SDL_SetSurfaceBlendMode(black,SDL_BLENDMODE_NONE);SDL_SetSurfaceBlendMode(white,SDL_BLENDMODE_NONE);
        SDL_SetSurfaceBlendMode(cached,SDL_BLENDMODE_BLEND);
        // Every skin visual at rest; looping timelines held still so a settled
        // Guide costs nothing to draw.
        for (size_t i=0;i<scene.nodes.size();i++) if (!std::strncmp(scene.str(scene.nodes[i].id),"@visual:",8)) scene.seek_name(int(i),"EndNormal");
        for (size_t i=0;i<scene.animations.size();i++) if (scene.animations[i].automatic) scene.seek(scene.animations[i].owner,20);
        ready=true;std::snprintf(line,sizeof line,"%s Guide scene: on",label);log(line);
        return true;
    }
    xui::Matrix view() const {auto m=to_screen;m.a[0][3]-=float(area.x);m.a[1][3]-=float(area.y);return xui::Matrix::scale(float(raster_density),float(raster_density))*m;}
    // The painter blends opaquely, so a state is painted twice, over black and
    // over white, and the true colour and coverage are recovered from the pair.
    void paint() {
        ++cpu_paints;
        painter.draw(scene,view(),true,black);
        SDL_UpperBlit(painter.canvas,nullptr,first,nullptr);
        painter.draw(scene,view(),true,white);
        SDL_LockSurface(cached);
        for (int y=0;y<cached->h;y++) {
            const auto *b=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(first->pixels)+y*first->pitch);
            const auto *w=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(painter.canvas->pixels)+y*painter.canvas->pitch);
            auto *out=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(cached->pixels)+y*cached->pitch);
            for (int x=0;x<cached->w;x++) {
                int a=0;
                for (int s:{0,8,16}) a=std::max(a,255-(int((w[x]>>s)&255)-int((b[x]>>s)&255)));
                a=std::clamp(a,0,255);
                if (!a) {out[x]=0;continue;}
                Uint32 c=Uint32(a)<<24;
                for (int s:{0,8,16}) c|=Uint32(std::min(255,(int((b[x]>>s)&255)*255+a/2)/a))<<s;
                out[x]=c;
            }
        }
        SDL_UnlockSurface(cached);
    }
    // Frames since the last draw (60 a second), and whether this is a fresh open.
    bool begin(float &frames) {
        const Uint32 now=SDL_GetTicks();
        // Slow rendering is not a new open. Only a skipped dashboard frame
        // (Guide absent) or the first render restarts the authored entrance.
        const bool fresh=last_epoch==0 || render_epoch>last_epoch+1;last_epoch=render_epoch;last_draw=now;
        frames=fresh?0.f:float(now-last_tick)*60/1000;last_tick=now;
        if (fresh) {last_key.clear();focused=-2;}
        return fresh;
    }
    // Move the focus look to `target` (a control), playing the skin's transitions.
    void focus(int target) {
        if (target==focused) return;
        if (focused>=0) scene.seek_name(visual_of(focused),"EndNormal");
        if (target>=0) scene.seek_name(visual_of(target),focused==-2?"InitFocus":"Focus",true);
        focused=target;
    }
    void finish(bool changed,float frames) {
        bool animating=false;for(const auto&p:scene.playback)animating=animating||p.playing;
        if(animating)scene.advance(frames);
        if(changed||animating){scene.evaluate();gpu_dirty=true;cpu_valid=false;}
        const bool gpu=x21hook::recording(screen);
        if(gpu){
            if(!gpu_ready)x21hook::active->packet.unsupported.push_back("guide_resources_unavailable");
            else {
                if(gpu_dirty){gpu_packet=Extractor{};gpu_packet.extract(scene,gpu_resources,view(),area.w*raster_density,area.h*raster_density,true,false);gpu_dirty=false;}
                for(const auto&why:gpu_packet.unsupported)x21hook::active->packet.unsupported.push_back("guide:"+why);
                auto at=render_density::pixel_rect(area,raster_density);size_t first=x21hook::active->packet.commands.size();
                if(x21hook::active->target!=0||!exbox_guide_commands(x21hook::active->packet.commands,gpu_packet.commands,screen->w,screen->h,{at.x,at.y,at.w,at.h}))x21hook::active->packet.unsupported.push_back("guide_group_unavailable");
                else {x21hook::active->packet.publications.insert(x21hook::active->packet.publications.end(),gpu_packet.publications.begin(),gpu_packet.publications.end());const auto clip=screen->clip_rect;x21hook::active->packet.commands.back().clip={clip.x,clip.y,clip.w,clip.h};x21hook::snapshot_commands(first);}
            }
            if(!x21hook::audit_cpu())return;
        }
        x21hook::Pause audit(gpu);
        if(!cpu_valid){paint();cpu_valid=true;}
        SDL_Rect to=area;SDL_BlitSurface(cached,nullptr,screen,&to);
    }

    // The legend orbs are the same glyphs the rest of Ex-Box draws for the
    // Button Style setting, so opening the Guide never changes the buttons;
    // the glyph carries its own letter, so the scene's letter is hidden.
    void bind_orb(int image,int letter,xb::Button b) {
        if (image<0) return;
        SDL_Surface *art=button_image(b);
        if (letter>=0) {if (art) scene.bind_show(letter,false); else scene.unbind(letter,xui::Show);}
        if (!art) {scene.unbind(image,xui::Texture);scene.unbind(image,xui::ImageMode);return;}
        scene.bind_image_mode(image,16);   // fitted, centred
        const std::string name="host:button-"+std::to_string(model.button_style)+"-"+std::to_string(int(b));
        if (!orbs.count(name)) if (SDL_Surface *copy=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0)) {if(gpu_ready)gpu_resources.bind_bitmap(name,SDL_ConvertSurfaceFormat(copy,SDL_PIXELFORMAT_ARGB8888,0));painter.bind_bitmap(name,copy);orbs.insert(name);gpu_dirty=true;cpu_valid=false;}
        scene.bind_string(image,xui::Texture,name);
    }
    std::set<std::string> orbs;
    // The root's own timeline (open / close) still running.
    bool root_playing() const {
        for (size_t i=0;i<scene.animations.size();i++) if (scene.animations[i].owner==0 && scene.playback[i].playing) return true;
        return false;
    }
    void bind_gamerpic(int image) {
        if (image<0 || (!edited_avatar::gamerpic && gamerpics.empty())) return;
        SDL_Surface *pic=SDL_ConvertSurfaceFormat(edited_avatar::gamerpic?edited_avatar::gamerpic:gamerpics[size_t(gamerpic)%gamerpics.size()],SDL_PIXELFORMAT_ARGB8888,0);
        const std::string name="host:gamerpic"; // replace the same binding when the editor save changes
        if (pic) {if(gpu_ready)gpu_resources.bind_bitmap(name,SDL_ConvertSurfaceFormat(pic,SDL_PIXELFORMAT_ARGB8888,0));painter.bind_bitmap(name,pic);scene.bind_string(image,xui::Texture,name);gpu_dirty=true;cpu_valid=false;}
    }
};
Core blades_launch,blades_late;
std::map<std::pair<std::string,int>,TTF_Font *> legend_fonts;
void clear_tabbed();
void clear() {blades_launch.clear();blades_late.clear();clear_tabbed();for(auto& entry:legend_fonts)if(entry.second)TTF_CloseFont(entry.second);legend_fonts.clear();}
// ---- The Blades Guides: launch (1888) and late (6770, from the fall 2007 update)
// Both are hudbkgnd + MainMenuSignedIn with the Mini Media Player, the
// Community bar and the GamerCard; 6770 adds Chat and IM, a stop button and
// battery gauges drawn as scenes. Dashboards from 5759 on take the late one.
int main_menu=-1;
Core *blades_core=nullptr;
bool prepare_blades() {
    const bool late=model.dashboard_build>=5759;
    Core &c=late?blades_late:blades_launch;
    if (blades_core!=&c && blades_core) blades_core->clear(),blades_core->tried=false;
    blades_core=&c;
    if (c.tried) {main_menu=c.node("sceneMainMenu");return c.ready;}
    // The HUD canvas (1120 x 770) to 1080p, as measured against a retail capture:
    // 1080/770 in both axes, the canvas shifted 61.7 HUD pixels left.
    auto view=xui::Matrix::scale(1080.f/770,1080.f/770);view.a[0][3]=-86.5f;
    if (!(late?c.load("blades/guide-6770","guide-6770.nxui",{0,0,900,H},view,"Blades (6770)")
              :c.load("blades/guide-1888","guide-1888.nxui",{0,0,900,H},view,"Blades (1888)"))) return false;
    main_menu=c.node("sceneMainMenu");
    // The Guide open ("Half"), the battery full, the gamercard in its Mini layout,
    // offline (a PS5 profile is a local one), and the rows a PS5 cannot use hidden.
    c.scene.seek_name(0,"EndClosedToHalf");
    // No verified battery reading is available; do not claim a full charge.
    c.show("imgBatteryIcon",false);
    c.scene.seek_name(c.node("gamerCardScene"),"Mini");
    c.scene.seek_name(c.node("offlineBaseScene"),"Mini");
    c.show("onlineBaseScene",false);c.show("offlineBaseScene",true);c.show("extendedScene",false);
    c.show("sliderVolume",false);c.show("imgVolume",false);
    c.show("btnSignInLive",false);c.show("txtSignInLive",false);c.show("btnBlank1",false);c.show("btnBlank2",false);
    c.show("scnChatIcon",false);c.show("scnChatAlert",false);c.show("CustomBanner",false);c.show("imgHeadsetBatteryIcon",false);
    c.text_of(c.node("txtCommunityHeading"),"Community");
    // Player one's quadrant of the ring of light (light1, the upper left), held
    // at its brightest; the other controllers' quadrants are off.
    c.show("light2",false);c.show("light3",false);c.show("light4",false);
    return true;
}
// Draw the 1888 Guide's home page or one of its list pages; false when the
// scene is not installed (the measured panel is drawn instead).
bool draw(bool home) {
    if (!prepare_blades()) return false;
    auto &c=*blades_core;
    // Focus: Community 0-2, then the row under it, Personal Settings, the music
    // bar and the XBOX 360 banner (model items 3-6).
    const int item=home?std::clamp(model.guide_item[dash::BladeHome],0,6):-1;
    // Other pages: a heading and up to ten rows, scrolled to keep the focus in view.
    const auto entries=home?std::vector<dash::GuideEntry>{}:model.blade_entries(model.guide_blade);
    const int count=int(entries.size());
    const int row_item=home?-1:std::clamp(model.guide_item[model.guide_blade],0,std::max(0,count-1));
    const int top=home?0:std::clamp(row_item-9,0,std::max(0,count-10));
    const std::string heading=home?"":model.guide_blade==dash::BladeSettings?std::string("Personal Settings"):model.blade_title(model.guide_blade);
    const bool playing=model.now_playing>=0;
    const std::string title=playing?model.now_playing_title:"Select Music";
    std::string key=std::to_string(home)+"|"+std::to_string(item)+"|"+clock_text()+"|"+model.player+"|"+title+"|"+
        std::to_string(playing && !model.music_paused)+std::to_string(model.music_shuffle)+"|"+std::to_string(trophies.known?trophies.points():0)+"|"+
        std::to_string(trophies.known?trophies.total():0)+"|"+std::to_string(trophies.known)+"|"+std::to_string(gamerpic)+"|"+edited_avatar::signature+"|"+std::to_string(model.button_style)+heading+"|"+std::to_string(row_item)+"|"+std::to_string(top);
    for (const auto &entry:entries) key+="|"+entry.label;
    for (const auto &hint:model.hints()) key+="|"+hint.label;
    // Opening plays the scene's own slide-in (ClosedToHalf); focus moves play
    // the skin's Focus / Normal transitions. Timelines run at 60 frames a second.
    float frames=0;
    if (c.begin(frames)) c.scene.seek_name(0,"ClosedToHalf",true);
    const bool changed=key!=c.last_key;
    if (changed) {
        c.last_key=key;
        c.scene.bind_show(main_menu,home);
        const int list=c.node("scnList");
        c.scene.bind_show(list,!home);
        if (!home) {
            c.text_of(c.node("labelHeading",list),heading);
            for (int r=0;r<10;r++) {
                char id[8];std::snprintf(id,sizeof id,"row%d",r);
                const int n=c.node(id,list);
                const bool used=top+r<count;
                c.scene.bind_show(n,used);
                c.text_of(n,used?entries[size_t(top+r)].label:"");
            }
            c.scene.bind_show(c.node("scroll0",list),top>0);
            c.scene.bind_show(c.node("scroll1",list),top+10<count);
        }
        // Header and spine.
        c.text_of(c.node("DateTimeTextId"),clock_text());
        c.text_of(c.node("GamerTag"),model.player);
        // Legends: the scene's own labels, or the page's when it differs.
        std::string legend[4]={"","","",""};
        for (const auto &hint:model.hints()) {
            const int at=hint.button==xb::A?0:hint.button==xb::B?1:hint.button==xb::X?2:hint.button==xb::Y?3:-1;
            // Options and the pages under it carry only Select and Back.
            if (at>=0 && (home || at<2)) legend[at]=hint.label;
        }
        const char *legend_ids[4]={"Legend_A","Legend_B","Legend_X","Legend_Y"};
        for (int i=0;i<4;i++) {
            const int n=c.node(legend_ids[i]);c.text_of(n,legend[i]);c.scene.bind_show(n,!legend[i].empty());
            c.bind_orb(c.node("Button1",c.visual_of(n)),c.node("XuiText1",c.visual_of(n)),i==0?xb::A:i==1?xb::B:i==2?xb::X:xb::Y);
        }
        // The gamercard (offline Mini): gamerpic, gamerscore and achievements.
        const int card=c.node("offlineBaseScene");
        c.text_of(c.node("credText",card),trophies.known?std::to_string(trophies.points()):std::string{});
        c.text_of(c.node("achievementsText",card),trophies.known?std::to_string(trophies.total()):std::string{});
        c.bind_gamerpic(c.node("gamerTileImage",card));
        // Community counts stay empty: the 1888 Guide shows a count only when there is one.
        for (const char *id:{"btnMessages","btnFriends","btnRecentPlayers"}) c.text_of(c.node(id),"");
        // Preserve the source text: 1888 Private Chat; 6770 Chat and IM.
        // PS5 Home remains in the explicitly separate R3 utilities menu.
        c.text_of(c.node("btnPlaylist"),title);
        if (const int play=c.node("btnPlay");play>=0) c.scene.bind_string(play,xui::Texture,playing && !model.music_paused?"mediaPause.png":"mediaPlay.png");
        if (const int mode=c.node("btnPlaybackMode");mode>=0) c.scene.bind_string(mode,xui::Texture,model.music_shuffle?"mediaShufflePlayback.png":"mediaInOrderPlayback.png");
        // Focus states.
        const char *focusable[7]={"btnMessages","btnFriends","btnRecentPlayers","btnQuickChat","btnOptions","btnPlaylist","btnGameBanner"};
        int target=-1;
        if (home) target=c.node(focusable[item]);
        else if (count>0) {char id[8];std::snprintf(id,sizeof id,"row%d",row_item-top);target=c.node(id,c.node("scnList"));}
        c.focus(target);
    }
    c.finish(changed,frames);
    return true;
}
// ---- The tabbed Guides: NXE (9199), Kinect (13604), Metro V1 (15574), V2 (17559)
// xam's hudbkgnd (the open blade grows from the centre, clock, gamerpic,
// legend) with hud's GuideMain: blades on an XuiTabScene, the open one dark
// (NXE) or light (Kinect on). Five blades through Metro V1 (Marketplace,
// Games, Home, Media, Settings); Metro V2 has four (Games & Apps, Home, Media,
// Settings). Each blade retains its authored controls and geometry.
struct Tabbed {
    Core core;
    const char *folder,*pack,*label;
    bool v2;            // four blades
    int shown=-1;
    std::string font;   // the era's Guide face (Convection, then Segoe Xbox)
    int player=0;       // the Media blade's mini player: 1 controls above the song, 2 below
    int media_tab=0;    // the tab that holds it
};
Tabbed nxe_guide{{},"nxe/guide-9199","guide-9199.nxui","NXE (9199)",false,-1,{},0,0};
Tabbed kinect_guide{{},"kinect/guide-13604","guide-13604.nxui","Kinect (13604)",false,-1,{},0,0};
Tabbed metro1_guide{{},"metro1/guide-15574","guide-15574.nxui","Metro V1 (15574)",false,-1,{},0,0};
Tabbed metro2_guide{{},"metro/guide-17559","guide-17559.nxui","Metro V2 (17559)",true,-1,{},0,0};
std::string segoe_path;
Tabbed *tabbed_for(int era) {
    if (era==1) return &nxe_guide;
    if (era==2) return &kinect_guide;
    if (era==3) return model.metro_v1()?&metro1_guide:&metro2_guide;
    return nullptr;
}
bool prepare_tabbed(Tabbed &g) {
    auto &c=g.core;
    if (c.tried) return c.ready;
    // The 852 x 480 HUD canvas fills 1080 lines (x 2.25), centred.
    auto view=xui::Matrix::scale(2.25f,2.25f);view.a[0][3]=1.5f;
    const bool segoe=&g==&metro1_guide || &g==&metro2_guide;
    const std::string saved=font_path;
    if (segoe && !segoe_path.empty()) font_path=segoe_path;
    g.font=font_path;
    const bool ok=c.load(g.folder,g.pack,{300,160,1320,720},view,g.label);
    font_path=saved;
    if (!ok) return false;
    c.scene.seek_name(0,"EndClosedToHalf");
    c.show("imgControllerBattery",false); // No verified battery source.
    c.show("imgHeadsetBattery",false);c.show("imgPlayTimerIcon",false);c.show("GamerPic",true);
    c.show("Gamertag",false);c.show("Spinner",false);c.show("Nui_Center",false);c.show("ErrorShader",false);
    // The Media blade's Mini Media Player (hud's MiniMediaPlayer in scnMusic):
    // its tab, and whether the buttons sit above the song (NXE) or below.
    g.player=0;g.media_tab=0;
    for (int t=1;t<=(g.v2?4:5) && !g.player;t++) {
        char id[12];std::snprintf(id,sizeof id,"GuideTab%d",t);
        const int music=c.node("scnMusic",c.node(id));
        if (music<0) continue;
        c.scene.advance(0);c.scene.evaluate();
        const int song=c.node("btnPlaylist",music),controls=c.node("sceneControlsContainer",music);
        if (song<0 || controls<0) continue;
        g.media_tab=t;
        g.player=c.scene.world[size_t(controls)].matrix.point(0,0).y<c.scene.world[size_t(song)].matrix.point(0,0).y?1:2;
        // Ex-Box reuses the authored volume button/slider for its music bus.
        // It never changes game audio or the PS5 system mixer.
        for (const char *hidden:{"btnDismissVolumeSliderA","btnDismissVolumeSliderB"}) c.show(hidden,false,music);
        c.show("sliderVolume",false,music);c.show("imgVolume",false,music);
    }
    return true;
}
const char *tab_rest(int tab) {
    static const char *names[6]={"","2To1End","1To2End","2To3End","3To4End","4To5End"};
    return names[std::clamp(tab,1,5)];
}
// A label's width in HUD units at its point size (the painter's own measure).
float legend_width(const std::string &face,const std::string &label,float points) {
    const int size=std::max(1,int(std::lround(points*4/3*8)));
    auto found=legend_fonts.find({face,size});
    if (found==legend_fonts.end()) found=legend_fonts.emplace(std::make_pair(face,size),TTF_OpenFont(face.c_str(),size)).first;
    int w=0;if (found->second) TTF_SizeUTF8(found->second,label.c_str(),&w,nullptr);
    return float(w)/8;
}
bool draw_tabbed(Tabbed &g) {
    if (!prepare_tabbed(g)) return false;
    auto &c=g.core;
    const int first=g.v2?int(dash::BladeGames):0;     // Metro V2 has no Marketplace blade
    const int blade=std::clamp(model.guide_blade,first,int(dash::BladeCount)-1);
    const int tab=blade-first+1;
    const auto entries=model.blade_entries(blade);
    const int count=int(entries.size());
    const int item=std::clamp(model.guide_item[blade],0,std::max(0,count-1));
    // On the Media blade the mini player takes the entries after the rows, and
    // the rows keep the four slots above it (the original four buttons).
    const bool player=g.player && tab==g.media_tab;
    int rows_n=0;
    while (rows_n<count && entries[size_t(rows_n)].node[0]) ++rows_n;
    std::string key=std::to_string(model.button_style)+"/"+std::to_string(blade)+"|"+std::to_string(item)+"|"+clock_text()+"|"+model.player+"|"+std::to_string(gamerpic)+"|"+edited_avatar::signature+
        "|volume="+std::to_string(model.music_volume)+"/"+std::to_string(model.guide_volume_open);
    for (const auto &entry:entries) key+="|"+entry.label;
    for (const auto &hint:model.hints()) key+="|"+hint.label;
    float frames=0;
    if (c.begin(frames)) {
        c.scene.seek_name(0,"ClosedToHalf",true);
        c.scene.seek_name(c.node("Tabscene"),tab_rest(tab));g.shown=tab;
    }
    const bool changed=key!=c.last_key;
    if (changed) {
        c.last_key=key;
        // A blade change plays the tab scene's own slide (1To2 ... 5To4).
        if (tab!=g.shown) {
            char name[8];std::snprintf(name,sizeof name,"%dTo%d",g.shown,tab);
            if (std::abs(tab-g.shown)!=1 || !c.scene.seek_name(c.node("Tabscene"),name,true)) c.scene.seek_name(c.node("Tabscene"),tab_rest(tab));
            g.shown=tab;
        }
        c.text_of(c.node("DateTimeTextId"),clock_text());
        // The profile's blade carries the gamertag, as on the console.
        c.text_of(c.node("txt_home"),model.player);c.text_of(c.node("txt_homeSel"),model.player);
        c.bind_gamerpic(c.node("GamerPic"));
        // Legend: the hudbkgnd's A / B / X / Y groups with the page's labels,
        // packed left to right by their widths.
        const char *buttons[4]={"AButton","BButton","XButton","YButton"},*texts[4]={"AText","BText","XText","YText"};
        bool any=false;float x=0;
        for (int i=0;i<4;i++) {
            std::string label;
            for (const auto &hint:model.hints()) if (hint.button==(i==0?xb::A:i==1?xb::B:i==2?xb::X:xb::Y)) label=hint.label;
            const int orb=c.node(buttons[i]),txt=c.node(texts[i]);
            c.bind_orb(c.node("Image",orb),c.node("Text",orb),i==0?xb::A:i==1?xb::B:i==2?xb::X:xb::Y);
            c.scene.bind_show(orb,!label.empty());c.scene.bind_show(txt,!label.empty());c.text_of(txt,label);
            if (label.empty()) continue;
            any=true;
            const float w=legend_width(g.font,label,txt>=0?c.scene.nodes[size_t(txt)].p[xui::PointSize].v[0]:12)+2;
            c.scene.bind_position(orb,x,0);c.scene.bind_position(txt,x+24,0);c.scene.bind_width(txt,w);
            x+=24+w+14;
        }
        c.show("Legend",any);
        // Bind focus by source control id; text, geometry, visibility, icons
        // and tab transitions stay authored. No scrolling replacement rows.
        char tab_id[12];std::snprintf(tab_id,sizeof tab_id,"GuideTab%d",tab);
        int target=item<rows_n?c.node(entries[size_t(item)].node,c.node(tab_id)):-1;
        // The mini player: the song (or Select Music) and its buttons, the
        // focus on whichever the Guide's item is.
        if (g.player) {
            char id[12];std::snprintf(id,sizeof id,"GuideTab%d",g.media_tab);
            const int music=c.node("scnMusic",c.node(id));
            c.scene.bind_show(music,player);
            if (player) {
                using A=dash::GuideAction;
                c.text_of(c.node("btnPlaylist",music),model.now_playing>=0?model.now_playing_title:std::string("Select Music"));
                if (const int play=c.node("btnPlay",music);play>=0)
                    c.scene.bind_string(play,xui::Texture,model.now_playing>=0 && !model.music_paused?"mediaPause.png":"mediaPlay.png");
                // Metro has only the shuffle glyph; it keeps the scene's own.
                if (const int mode=c.node("btnPlaybackMode",music);mode>=0) {
                    const char *look=model.music_shuffle?"mediaShufflePlayback.png":"mediaInOrderPlayback.png";
                    if (FILE *f=fopen((c.art_directory+look).c_str(),"rb")) {fclose(f);c.scene.bind_string(mode,xui::Texture,look);}
                    else c.scene.unbind(mode,xui::Texture);
                }
                const int volume=c.node("imgVolume",music),slider=c.node("sliderVolume",music);
                c.scene.bind_show(volume,model.now_playing>=0);
                c.scene.bind_show(slider,model.guide_volume_open);
                if (const int bar=c.node("Bar_mid",slider);bar>=0) {
                    const float full=c.scene.nodes[size_t(bar)].p[xui::Width].v[0];
                    c.scene.bind_width(bar,full*float(model.music_volume)/10.f);
                }
                if (item>=rows_n && item<count) {
                    const A a=entries[size_t(item)].action;
                    const char *node_id=a==A::Music?"btnPlaylist":a==A::MusicPause?"btnPlay":a==A::MusicPrevious?"btnPrev":
                                        a==A::MusicStop?"btnStop":a==A::MusicNext?"btnNext":a==A::MusicVolume?"imgVolume":"btnPlaybackMode";
                    target=c.node(node_id,music);
                }
            }
        }
        c.focus(target);
    }
    c.finish(changed,frames);
    return true;
}
bool draw_era(int era) {Tabbed *g=tabbed_for(era);return g && draw_tabbed(*g);}
bool available(int era) {
    model.media_player=0;
    if (era==0) return prepare_blades();
    Tabbed *g=tabbed_for(era);
    if (!g || !prepare_tabbed(*g)) return false;
    model.media_player=g->player;
    return true;
}
Core *core_for(int era) {
    if (era==0) return prepare_blades()?blades_core:nullptr;
    Tabbed *g=tabbed_for(era);
    return g && prepare_tabbed(*g)?&g->core:nullptr;
}
// Closing plays the scene's own HalfToClosed over the last open frame; false
// once it has stopped (or the scene has no such frame), so the hub takes over.
bool draw_closing(int era,bool fresh) {
    Core *c=core_for(era);
    if (!c) return false;
    const Uint32 now=SDL_GetTicks();
    if (fresh) {
        if (!c->scene.seek_name(0,"HalfToClosed",true)) return false;
        // The tabbed eras hide the legend by its Show key at the first close
        // frame; the open Guide's binding would hold it up.
        c->scene.unbind(c->node("Legend"),xui::Show);
        c->last_key.clear();
        c->last_tick=now;
    } else if (!c->root_playing()) {c->last_draw=0;return false;}
    // At most four frames a step, so a slow paint cannot skip the slide.
    const float frames=std::min(4.f,float(now-c->last_tick)*60/1000);
    c->last_tick=now;c->last_draw=0;   // reopening is always a fresh open
    // Use the same authored playback and dual-pass GPU route as an open Guide.
    c->finish(true,frames);
    return true;
}
void clear_tabbed() {for (Tabbed *g:{&nxe_guide,&kinect_guide,&metro1_guide,&metro2_guide}) g->core.clear();}
}

