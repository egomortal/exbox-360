// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
// The original media scenes' skin (music/, videos/ and pictures/ in the 9199,
// 13604, 15574 and 17559 dashboards), shared by the Music Library, the Videos
// and Pictures grids and their full-screen bars. Blades (era 0) keeps its own.
// Sizes are the skin's 720p units times 1.5.
namespace media_skin {
bool light() {return model.era>=2;}                           // 13604, 15574, 17559
bool flat() {return model.era==3 && !model.metro_v1();}        // 17559: flat greens
// NXE greenBox / greenBox1 rim colour (0x69a50f), through the theme as the hub.
unsigned nxe_green() {return tint_rgb(0x69a50f,theme_tint(model.theme));}
// The era's own green, for small marks (a selected tab, a playing sign).
unsigned green() {return model.era==1?nxe_green():flat()?0x008a00:0x77ba02;}
constexpr SDL_Color kInk{235,235,235,255};                     // 0xffebebeb, every white label
constexpr SDL_Color kSub{235,235,235,178};                     // NXE Label_Sub / Label_SubRight: 0.7
constexpr SDL_Color kRowInk{0x65,0x6d,0x72,255};               // btn_Music_arrow, btn_LibBrowse 0xff656d72
constexpr SDL_Color kRowSel{0x33,0x3a,0x40,255};               // btn_Music_arrow KillFocus 0xff333a40
// The scene draws over the dashboard's own background, not over the hub.
bool opaque_background() {static x21opaque::Proof proof;return !era_background||proof.opaque(era_background);}
void scene_background() {
    SDL_Rect all{0,0,W,H};
    if (era_background) blit_quality(era_background,nullptr,screen,&all);
    else if (model.era==3) gradient(0,0,W,H,0x4a4a4a,0x9a9a9a);
    else gradient(0,0,W,H,0x358f24,0xc9e713);
    draw_picture_background();
    if (era_aura_floor) blit_quality(era_aura_floor,nullptr,screen,&all,true,era_aura_floor->w==8&&era_aura_floor->h==720);
}
// BackgroundPanel. 9199: the 9-grid common://BackgroundPanel.png (offsets
// 15/20); 13604/15574/17559: a figure, 0xe8ebeb over 0xc9cfd1 (rot -90).
void background_panel(int x,int y,int w,int h) {
    if (light()) {gradient(x,y,w,h,0xe8ebeb,0xc9cfd1);return;}
    if (nxe_panel) nine_slice(nxe_panel,x-22,y-18,w+44,h+42,18,27);
    else rounded(x,y,w,h,10,0x4a5a66,0x323f4b);
}
// RightPanelShader (13604/15574/17559): 0x979da1 over 0x696e73 (rot -90), the
// grey that the white-lettered lists (btn_3rdList, btn_picturesGrid) sit on.
void panel_shader(int x,int y,int w,int h) {gradient(x,y,w,h,0x979da1,0x696e73);}
// GreenHighlight: 0x77ba02 over 0x51860f (13604, 15574; rot 90 puts stop 0,
// 0x51860f, at the bottom); 17559 fills it flat 0x008a00.
void green_highlight(int x,int y,int w,int h) {
    if (flat()) rect(x,y,w,h,0x008a00);
    else gradient(x,y,w,h,0x77ba02,0x51860f);
}
// NXE greenBox1: a soft 0x69a50f rim around the focused row or tile, and
// Highlight14's pale wash over its top half.
void green_box(int x,int y,int w,int h) {
    const unsigned g=nxe_green();
    for (int k=8;k>=1;k--) guide::bg::ring(x-k,y-k,w+2*k,h+2*k,6+k,1,g,Uint8(std::max(0,170-k*20)));
    guide::bg::ring(x,y,w,h,6,3,g,255);
    shade(x+3,y+3,w-6,(h-6)/2,0xebebeb,22);
}
// A row's rules: NXE "top"/"bottom" 2 px 0xebebeb at 10%; 13604 Top/Bottom 1 px 0x9facb2.
void rules(int x,int y,int w,int h) {
    if (light()) {rect(x,y,w,1,0x9facb2);rect(x,y+h-1,w,1,0x9facb2);}
    else {shade(x+5,y+3,w-10,2,0xebebeb,26);shade(x+5,y+h,w-10,2,0xebebeb,26);}
}
// Text with the skin's 0x7f0f0f0f drop shadow (the NXE labels).
int shadowed(const std::string &s,int x,int y,TTF_Font *font,SDL_Color ink) {
    text(s,x+2,y+2,font,{15,15,15,Uint8(ink.a/2)});
    return text(s,x,y,font,ink);
}
// The progress bar. 9199 XuiProgressBar: a 0x0f0f0f body, a 0xa7cd38 to
// 0x6e9b31 fill. 13604/15574 XuiProgressBarGreen: 0xc7c9cb over 0x767d81,
// filled 0x77ba02 over 0x51860f; 17559: 0xc7c9cb over 0xa5a8aa, 0x1cb61c over 0x008a00.
void progress_bar(int x,int y,int w,int h,double fraction) {
    const int fill=int(w*std::clamp(fraction,0.0,1.0));
    if (model.era==1) {
        const Tint t=theme_tint(model.theme);
        shade(x,y,w,h,0x0f0f0f,200);
        shade(x,y,w,1,0xebebeb,36);
        if (fill>0) gradient(x,y,fill,h,tint_rgb(0xa7cd38,t),tint_rgb(0x6e9b31,t));
        return;
    }
    gradient(x,y,w,h,0xc7c9cb,flat()?0xa5a8aa:0x767d81);
    if (fill>0) gradient(x,y,fill,h,flat()?0x1cb61c:0x77ba02,flat()?0x008a00:0x51860f);
}
// The player's band: osui_PhotoViewerBG's 0x23292e, its top edge fading out.
void osd_band(int top) {
    for (int i=0;i<5;i++) shade(0,top-40+i*8,W,8,0x23292e,Uint8(30+i*36));
    shade(0,top,W,H-top,0x23292e,220);
}
// osui_PhotoViewerDisplayBar: a 0x7d0f0f0f box with a 1.5 px 0x19ebebeb edge.
void display_box(int x,int y,int w,int h) {
    shade(x,y,w,h,0x0f0f0f,0x7d);
    shade(x,y,w,2,0xebebeb,0x19);shade(x,y+h-2,w,2,0xebebeb,0x19);
    shade(x,y+2,2,h-4,0xebebeb,0x19);shade(x+w-2,y+2,2,h-4,0xebebeb,0x19);
}
}
// Game Videos' controls show as a video opens, on input and while paused,
// then get out of the picture, as the 360's player did.
constexpr Uint32 kVideoControls=4000;
// Pictures full screen keeps the same habit: its bar and prompts show as it
// opens and on input, and then leave the picture alone (the slideshow too).
Uint32 picture_full_since=0;
bool video_controls_shown(Uint32 now) {
    if (model.viewer==dash::Viewer::Pictures && model.viewer_full && !model.guide && !model.info)
        return model.picture_control>=0 || model.picture_background_confirm || now-last_input_at<kVideoControls || now-picture_full_since<kVideoControls;
    if (model.viewer!=dash::Viewer::Videos || !model.viewer_full || model.guide || model.info) return true;
    return model.video_paused || now-last_input_at<kVideoControls || now-dwell_since<kTrailerAt+kVideoControls;
}
std::string video_size_label(long long bytes) {
    char buf[32];
    if (bytes>=(1ll<<30)) std::snprintf(buf,sizeof buf,"%.1f GB",double(bytes)/double(1ll<<30));
    else std::snprintf(buf,sizeof buf,"%.1f MB",double(bytes)/double(1<<20));
    return buf;
}
// A clip's sidecar subtitles (packed/<folder>.srt), read when it first plays.
const std::vector<srt::Cue> &subtitles_for(const std::string &folder) {
    if (folder==subtitle_folder) return subtitle_cues;
    subtitle_folder=folder;
    subtitle_cues.clear();
    const std::string path=std::string(trailer_root)+"/packed/"+folder+".srt";
    if (FILE *f=fopen(path.c_str(),"rb")) {
        std::string text;
        char buf[4096];
        size_t got;
        while ((got=fread(buf,1,sizeof buf,f))>0 && text.size()<(1u<<20)) text.append(buf,got); // a megabyte is hours of captions
        fclose(f);
        subtitle_cues=srt::parse(text);
    }
    return subtitle_cues;
}
// Each line centred on its own dark box, the bottom line ending at bottom.
void draw_subtitles(const srt::Cue &cue, int bottom) {
    TTF_Font *font=semibold?semibold:regular;
    constexpr int pitch=50;
    int y=bottom-int(cue.lines.size())*pitch;
    for (const auto &line:cue.lines) {
        const std::string shown=fit(line,font,W-240);
        const int w=text_width(shown,font), x=(W-w)/2;
        shade(x-16,y,w+32,pitch-4,0x000000,170);
        text(shown,x,y+6,font,{255,255,255,255});
        y+=pitch;
    }
}
// dvd/XmfInfo.xur (9199/13604/15574/17559): +running time, -time left.
std::string xmf_time(int ms, char sign) {
    const int seconds=std::max(0,ms)/1000;
    char out[32];
    std::snprintf(out,sizeof out,"%c%02d:%02d:%02d",sign,seconds/3600,(seconds/60)%60,seconds%60);
    return out;
}
// XmfPadPlayback and XmfInfo use a 720 x 480 canvas. Keep their top info
// pane and lower transport row, with each build's icons from library_art().
void draw_video_controls(int item, int n, int ms, int length, bool paused) {
    auto sx=[](double x) {return int(x*W/720);};
    auto sy=[](double y) {return int(y*H/480);};
    const bool metro=model.era==3, nxe=model.era<=1;
    const SDL_Color ink{235,235,235,255};
    // bgDrop and prgbr_Playback; local frames need no network buffer meter.
    shade(0,0,W,sy(60),0x000000,128);
    text(xmf_time(ms,'+'),sx(88),sy(nxe?31:29),regular,ink);
    // Ex-Box addition: no 360 original; unknown duration is not a fake countdown.
    text(length>0?xmf_time(std::max(0,length-ms),'-'):"--:--:--",sx(557.5),sy(nxe?31:29),regular,ink);
    const int bx=sx(197), by=sy(35), bw=sx(360), bh=sy(15);
    const int fill=length>0?int(bw*std::clamp(double(ms)/length,0.0,1.0)):0;
    shade(bx-4,by-4,bw+8,bh+8,0xebebeb,90);
    rect(bx,by,bw,bh,0x252b2e);
    if (fill>0) {
        if (metro) rect(bx,by,fill,bh,0x008a00);
        else gradient(bx,by,fill,bh,0xa7cd38,0x6e9b31);
    }
    // XmfInfo grp_segments: Metro changes the width, offsets and opacity.
    constexpr double old_segments[]={34.6,64.2,93.8,123.4,153,182.4,212,241.6,271};
    constexpr double new_segments[]={20,52,84,116,148,180,213,245,276};
    for (int i=0;i<9;i++) shade(sx(208+(metro?new_segments[i]:old_segments[i])),by,
                               sx(metro?1:2),bh,0xebebeb,metro?255:102);
    // Only existing controls: play/pause, previous clip, seek back, stop,
    // seek forward, next clip. No chapter, speed, language or display modes.
    constexpr double nxe_x[]={173.07,219.95,255.73,290.18,324.60,358.85};
    constexpr double later_x[]={117.5,166.25,215,263.75,312.5,361.25};
    const int icons[]={paused?0:1,2,3,4,5,6};
    constexpr xb::Button buttons[]={xb::A,xb::LB,xb::LT,xb::B,xb::RT,xb::RB};
    const double y=nxe?388:381.5;
    media_skin::osd_band(sy(y-2));
    for (int i=0;i<6;i++) {
        if ((i==1 && item==0) || (i==5 && item+1>=n)) continue;
        if ((i==2 && ms<=0) || (i==4 && length>0 && ms>=length)) continue;
        // Original local size includes the authored per-button Scale. Missing
        // widths and unverified updates retain the pre-existing host fallback.
        media_transport::Pixels bounds{sx(nxe?nxe_x[i]:later_x[i]),sy(y),sx(nxe?34:48.75),sy(29)};
        if (const auto* cell=media_transport::cell_for(model.dashboard_build,i,true))
            bounds=media_transport::project(*cell,double(W)/720,double(H)/480,nxe?34:48.75,29);
        const int x=bounds.x,w=bounds.w,h=bounds.h;
        if (metro && !model.metro_v1()) rect(x,bounds.y,w,h,0xc9cfd1);
        else gradient(x,bounds.y,w,h,0xebebeb,0x9facb2);
        if (SDL_Surface *icon=video_controls[icons[i]]) {
            SDL_Rect dst{x+(w-sx(24))/2,bounds.y+(h-sy(24))/2,sx(24),sy(24)};
            blit_quality(icon,nullptr,screen,&dst);
        }
        // Ex-Box addition: no 360 original; show the existing direct pad binding.
        glyph(buttons[i],x+(w-30)/2,bounds.y-sy(18),30);
    }
    // XmfPadPlayback's help-text label (the focused button's action) is left
    // out: at its source position it lands on the controller legend below,
    // which already names the A action (Pause / Play / Restart).
    (void)ink;
}
// videos/DetailsActions.xur scnBookmark: the selected clip and its two actions.
void draw_video_start() {
    media_skin::scene_background();
    media_skin::background_panel(120,150,1680,810);
    text(fit(trailer_name(model.video_pending),large,1480),165,200,large,
         model.era>=2?media_skin::kRowSel:media_skin::kInk);
    if(model.video_theater)
        text("Offline Theater Party",165,262,small,model.era>=2?media_skin::kRowSel:media_skin::kSub);
    for (int i=0;i<2;i++) {
        const int y=330+i*90;
        if (i==model.video_start_item) {
            if (model.era>=2) media_skin::green_highlight(165,y,600,78);
            else media_skin::green_box(165,y,600,78);
        }
        media_skin::rules(165,y,600,78);
        text(dash::kVideoStartActions[i],190,y+19,regular,
             model.era>=2 && i!=model.video_start_item?media_skin::kRowInk:media_skin::kInk);
    }
}

void draw_theater_room(Uint32 now,float video_alpha) {
    static unsigned seen_reaction=0;
    static Uint32 reaction_at=0;
    static int reaction_kind=0;
    if(model.video_theater_reaction_serial!=seen_reaction) {
        seen_reaction=model.video_theater_reaction_serial;
        reaction_kind=std::clamp(model.video_theater_reaction,0,dash::kTheaterReactionCount-1);
        reaction_at=now;
        // Bind only clips the current kit reports.  Popcorn is original
        // Ex-Box particle chrome until a supplied authored clip is proven.
        if(reaction_kind==0)look3d::moment(exbox::dashboard_director::AvatarMoment::Selection,now);
        else if(reaction_kind==1)look3d::moment(exbox::dashboard_director::AvatarMoment::Achievement,now);
    }
    // Reuse the active era's owner-supplied dashboard material so Theater
    // belongs to the selected dashboard instead of looking like unrelated
    // placeholder art.  The neutral gradient remains the clean fallback.
    gradient(0,0,W,H,0x111619,0x30383b);
    if(era_background){SDL_Rect all{0,0,W,H};blit_quality(era_background,nullptr,screen,&all);shade(0,0,W,H,0x050708,145);}
    constexpr SDL_Rect frame{244,54,1432,704};
    rounded(frame.x-18,frame.y-18,frame.w+36,frame.h+36,12,0x11171d,0x020304);
    outline(frame.x-10,frame.y-10,frame.w+20,frame.h+20,4,0x68727a);
    rect(frame.x,frame.y,frame.w,frame.h,0x000000);
    if(video_alpha>0)draw_video(frame.x,frame.y,frame.w,frame.h,video_alpha);

    // A low auditorium floor reuses the era floor material when present.
    gradient(0,758,W,H-758,0x1a2026,0x050708);
    if(era_floor){SDL_SetSurfaceAlphaMod(era_floor,135);SDL_Rect floor{0,758,W,H-758};blit_quality(era_floor,nullptr,screen,&floor);SDL_SetSurfaceAlphaMod(era_floor,255);}
    for(int i=0;i<8;i++) {
        const int x=92+i*244;
        rounded(x,855,190,190,42,0x202830,0x0d1115);
        rounded(x+16,875,158,170,34,0x33404a,0x151b20);
    }
    if(model.local_friend_avatars) {
        constexpr int centers[]{350,610,1310};
        for(int i=0;i<3;++i) {
            SDL_Surface* guest=edited_avatar::friend_still(i);
            if(!guest)guest=look3d::local_guest(i);
            if(!guest)continue;
            const int pulse=0;
            SDL_Rect at=edited_avatar::contain(guest,{centers[i]-105,720-pulse,210,330});
            const bool gpu=gpu_home::avatar(guest,at,1.f);
            if(!gpu||x21hook::audit_cpu()){x21hook::Pause reference(gpu);prepared_avatar::materialize(guest);blit_quality(guest,nullptr,screen,&at,false);}
        }
    }
    SDL_Surface* figure=edited_avatar::live_or_still(now,true,avatar_art);
    if(figure) {
        // The lower body extends behind the chair front below.  This keeps the
        // same live pose/animation but reads as a seated theater silhouette
        // instead of a miniature avatar standing on the chair cushion.
        const SDL_Rect stage{830,690,260,420};
        SDL_Rect at=edited_avatar::contain(figure,stage);
        const bool gpu=gpu_home::avatar(figure,at,1.f);
        if(!gpu||x21hook::audit_cpu()) {x21hook::Pause reference(gpu);
            prepared_avatar::materialize(figure);if(figure==look3d::hub.surface)look3d::materialize_hub();
            blit_quality(figure,nullptr,screen,&at,false);
        }
    }
    // Foreground chair backs occlude hips/legs for the seated composition.
    // The original party's exact sitting skeleton remains an asset-binding
    // task; this visual treatment never invents joint animation data.
    for(int i=0;i<8;i++) {
        const int x=92+i*244;
        // The owner occupies the wider centre chair.  Its foreground back
        // masks the standing clip's hands as well as the hips; no skeleton
        // or authored pose is altered.
        const int back_x=i==3?846:x+13;
        const int back_w=i==3?228:164;
        rounded(back_x,900,back_w,145,30,0x28323a,0x10151a);
        rect(back_x+12,914,back_w-24,12,0x3b4852);
    }
    // Hide feet below the front row; this is a visual seat occlusion, not a
    // fabricated sitting skeleton pose.
    gradient(0,1042,W,H-1042,0x090c0f,0x020303);
    if(reaction_at && now-reaction_at<1600) {
        const Uint32 age=now-reaction_at;
        if(reaction_kind==2) for(int i=0;i<22;i++) {
            const int phase=int(age)/12+i*31;
            const int x=960+((i*83+phase*3)%520)-260;
            const int rise=std::min(250,phase%340);
            const int y=920-rise+((i*37+phase)%90);
            const int s=5+(i%4)*2;
            rounded(x,y,s,s,s/2,i%3?0xf4e7aa:0xffffff,0xb88b36);
        }
        if(reaction_kind==2 && theater_popcorn_art) {
            SDL_Rect prop{1010,820,84,84};
            blit_contained(theater_popcorn_art,prop);
        }
        const std::string reaction=dash::kTheaterReactions[reaction_kind];
        shade(820,676,280,46,0x000000,145);
        text(reaction,960-text_width(reaction,small)/2,686,small,{255,255,255,255});
    }
    text("OFFLINE THEATER",96,78,small,{210,218,222,210});
    text("Local guests",96,111,small,{148,158,164,210});
    const std::string selected=std::string("Reaction: ")+dash::kTheaterReactions[std::clamp(model.video_theater_reaction,0,dash::kTheaterReactionCount-1)];
    text(selected,96,142,small,{148,158,164,210});
}
// ---- Pictures full screen: cross-fades, zoom and pan, the slideshow, info ----
// A screenshot's file size: most are well under a megabyte.
std::string picture_size_label(long long bytes) {
    if (bytes>=(1ll<<20)) return video_size_label(bytes);
    return std::to_string(std::max(1ll,(bytes+512)/1024))+" KB";
}
constexpr Uint32 kPictureFade=350, kSlideFade=900;
unsigned long picture_full_frame=0; // the last frame drawn full screen
Uint32 slideshow_at=0, picture_settled_at=0, picture_drawn_at=0;
int slideshow_item=-1, picture_last_item=-1, picture_step=1;
bool slideshow_running=false;
float shown_zoom=1, shown_pan_x=0, shown_pan_y=0;
// Each frame: the slideshow's clock, and what full screen held goes when it closes.
void picture_tick(Uint32 now) {
    const bool full=model.viewer==dash::Viewer::Pictures && model.viewer_full;
    if (!full) {
        for (SDL_Surface **s:{&picture_before,&picture_ahead,&picture_source,&picture_view}) drop_surface(*s);
        picture_ahead_path.clear(); picture_source_path.clear(); picture_view_key.clear();
    }
    if (!full || !model.picture_slideshow || model.guide || model.info || model.details || model.picture_background_confirm) {slideshow_running=false; return;}
    if (!slideshow_running || slideshow_item!=model.viewer_item) {slideshow_running=true; slideshow_item=model.viewer_item; slideshow_at=now; return;}
    if (now-slideshow_at>=Uint32(dash::kSlideshowMs) && model.slideshow_step()) {
        // Past any it cannot show (HDR without jxrlib), once round at most.
        for (int left=model.viewer_count();left>1;--left) {
            const dash::PictureFile *pic=model.picture();
            if (!pic || can_show(pic->path) || !model.slideshow_step()) break;
        }
        slideshow_item=model.viewer_item; slideshow_at=now;
    }
}
// Original slideshow transport; host metadata remains in the separate Info panel.
void draw_picture_controls(const dash::PictureFile &pic, int item, int n, bool running, float zoom) {
    constexpr int top=H-350;
    const bool skin=model.era>=1;
    if (skin) media_skin::osd_band(top);
    else {
        for (int i=0;i<5;i++) shade(0,top-40+i*8,W,8,0x000000,Uint8(24+i*24)); // a soft edge, not a hard line
        shade(0,top,W,H-top,0x000000,170);
    }
    // Ex-Box metadata stays in the explicit RS Info panel. The source loading
    // progress control is not a picture-index/date/zoom progress strip.
    (void)pic;(void)item;(void)n;(void)zoom;
    const SDL_Color ink=skin?media_skin::kInk:SDL_Color{240,244,236,255};
    // 9199 slideshowControls.xur: osui_btn_PlayerLeft/Middle/Right,
    // btn_Play through btn_RotateLeft; 1280x720 scene, scaled by 1.5.
    constexpr double positions[]={412.671,477.109,530.99,584.658,637.978,691.188,744.597,800.989};
    constexpr double blades_positions[]={313.952,369.415,429.941,491.151,552.361,613.571,674.781,734.991};
    const bool later=model.era>=2; // 13604/15574/17559: 70x48, x=342+75*i, y=568
    const int icons[]={running?1:0,2,3,4,model.picture_shuffle?5:6,model.picture_repeat?7:8,9,10};
    for (int i=0;i<8;++i) {
        media_transport::Pixels bounds{int((later?342+75*i:model.era==0?blades_positions[i]:positions[i])*1.5),
            int((later?568:model.era==0?590.278:591.328)*1.5),int((later?70:model.era==0?60:67.0094)*1.5),int((later?48:44.0094)*1.5)};
        // Keep current host viewport placement explicit. Blades' 1120x770 author
        // canvas still needs its host transform; this fixes local metrics only.
        if (const auto* cell=media_transport::cell_for(model.dashboard_build,i,false))
            bounds=media_transport::project(*cell,1.5,1.5,model.era==0?60:67.0094,44.0094);
        const int px=bounds.x,py=bounds.y,pw=bounds.w,ph=bounds.h;
        const bool selected=model.picture_control==i;
        if (selected) media_skin::green_highlight(px,py,pw,ph);
        else if (media_skin::flat()) rect(px,py,pw,ph,0xc9cfd1);
        else if (!later && (i==0 || i==7)) rounded(px,py,pw,ph,8,0xebebeb,0x9facb2);
        else gradient(px,py,pw,ph,0xebebeb,0x9facb2);
        if (SDL_Surface *icon=picture_controls[icons[i]]) {
            SDL_Rect dst{px+(pw-48)/2,py+(ph-48)/2,48,48};
            blit_quality(icon,nullptr,screen,&dst);
        }
    }
    const int help_x=later?513:model.era==0?556:718, help_y=later?942:model.era==0?964:955;
    if (model.picture_control>=0) {
        if (model.era==1) media_skin::display_box(663,949,650,51);
        text(model.picture_control_help(),help_x,help_y,small,ink);
    } else {
        // Ex-Box addition: no 360 original; discoverable entry to transport focus.
        glyph(xb::Down,663,955,30);text("Controls",703,955,small,ink);
    }
}
// RS: what the PS5 and the file say about the picture, in a panel at the side.
// Ex-Box addition: no 360 original (the panel and its row names). NXE, Kinect
// and Metro dress it as osui_PhotoViewerDisplayBar with metaScene_1line_MediaTop's
// 18 pt 0xebebeb text.
void draw_picture_info(const dash::PictureFile &pic, int item, int n) {
    std::vector<std::pair<std::string,std::string>> rows;
    rows.push_back({"Game",pic.label()});
    if (!pic.title_id.empty() && pic.title_id!=pic.label()) rows.push_back({"Title ID",pic.title_id});
    rows.push_back({"Taken",dash::picture_date(pic.taken)});
    const auto size=picture_sizes.find(pic.path);
    if (size!=picture_sizes.end()) rows.push_back({"Size",std::to_string(size->second.first)+" \xC3\x97 "+std::to_string(size->second.second)});
    if (pic.bytes>0) rows.push_back({"File size",picture_size_label(pic.bytes)});
    const bool png=ps5lib::viewable_picture(pic.path) && pic.path.size()>4 && (pic.path.compare(pic.path.size()-4,4,".png")==0 || pic.path.compare(pic.path.size()-4,4,".PNG")==0);
    rows.push_back({"Format",is_jxr(pic.path)?"HDR (JPEG XR)":png?"PNG":"JPEG"});
    const size_t slash=pic.path.rfind('/');
    rows.push_back({"File",slash==std::string::npos?pic.path:pic.path.substr(slash+1)});
    rows.push_back({"Picture",std::to_string(item+1)+" of "+std::to_string(n)});
    constexpr int pw=640, x=W-95-pw, y=80, pitch=46;
    const int ph=96+int(rows.size())*pitch;
    const bool skin=model.era>=1;
    if (skin) {
        shade(x,y,pw,ph,0x23292e,205);
        media_skin::display_box(x,y,pw,ph);
        rect(x,y,pw,4,media_skin::green());
    } else {
        shade(x,y,pw,ph,0x0c1014,205);
        rect(x,y,pw,4,0x8fd23c);
    }
    TTF_Font *head=skin && era_text?era_text:regular;
    text("Picture Info",x+32,y+24,head,skin?media_skin::kInk:SDL_Color{255,255,255,255});
    int ry=y+84;
    for (const auto &row:rows) {
        text(row.first,x+32,ry,small,skin?SDL_Color{235,235,235,153}:SDL_Color{150,164,172,255}); // metaScene_1line Pane_txt: 0.6
        text(fit(row.second,small,pw-232),x+200,ry,small,skin?media_skin::kInk:SDL_Color{232,238,226,255});
        ry+=pitch;
    }
}
// pictures/Strings.xus, 9199: the confirmation for 905_IndividualDeviceMain's Y action.
void draw_picture_background_confirmation() {
    shade(0,0,W,H,0x000000,150);
    media_skin::background_panel(360,320,1200,420);
    const SDL_Color ink=media_skin::light()?media_skin::kRowSel:media_skin::kInk;
    text("Apply as Background",400,350,large,ink);
    text("This will set the picture as the background.",400,430,regular,ink);
    text("Are you sure you want to use this picture?",400,478,regular,ink);
    for (int i=0;i<2;++i) {
        const int x=400+i*540;
        const bool selected=model.picture_background_yes==(i==0);
        if (selected) media_skin::green_highlight(x,580,500,70);
        text(i==0?"Yes":"No",x+24,598,regular,selected?media_skin::kInk:ink);
    }
}
void draw_picture_full(Uint32 now) {
    rect(0,0,W,H,0x000000);
    const int n=model.viewer_count(), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const dash::PictureFile *pic=model.picture();
    const bool opened=picture_full_frame+1!=frame_count;
    picture_full_frame=frame_count;
    const float dt=opened?0.0f:std::min(0.1f,float(now-picture_drawn_at)/1000.0f);
    picture_drawn_at=now;
    if (opened) {picture_full_since=now; picture_last_item=item; shown_zoom=1; shown_pan_x=shown_pan_y=0;}
    if (!pic) return;
    if (item!=picture_last_item) {
        picture_step=model.picture_slideshow || item>picture_last_item?1:-1;
        picture_last_item=item;
    }
    SDL_Surface *full=picture_screen(pic->path,now);
    if (opened) {drop_surface(picture_before); picture_shown_at=now;} // fade in from black
    // The cross-fade from the picture before (longer in the slideshow).
    const float fade=std::clamp(float(now-picture_shown_at)/float(model.picture_slideshow?kSlideFade:kPictureFade),0.0f,1.0f);
    const float a=fade*fade*(3-2*fade);
    if (picture_before && a<1) SDL_BlitSurface(picture_before,nullptr,screen,nullptr);
    else drop_surface(picture_before);
    // Zoom and pan glide to where the model says.
    const float zoom=dash::kPictureZooms[std::clamp(model.picture_zoom,0,dash::kPictureZoomCount-1)];
    const float pan_x=float(model.picture_pan_x)/dash::kPicturePan, pan_y=float(model.picture_pan_y)/dash::kPicturePan;
    const float follow=std::min(1.0f,dt*14.0f);
    auto glide=[follow](float &shown, float target, float close) {shown+=(target-shown)*follow; if (std::fabs(target-shown)<close) shown=target;};
    glide(shown_zoom,zoom,0.002f); glide(shown_pan_x,pan_x,0.001f); glide(shown_pan_y,pan_y,0.001f);
    const bool moving=shown_zoom!=zoom || shown_pan_x!=pan_x || shown_pan_y!=pan_y;
    if (moving) picture_settled_at=now;
    SDL_Surface *original=shown_zoom>1.0f?picture_original(pic->path):nullptr;
    if (original) {
        // The part of the original on screen: centred, then panned across what overflows.
        const float k=std::min(float(W)/original->w,float(H)/original->h)*shown_zoom;
        const float dw=original->w*k, dh=original->h*k;
        const float ox=(W-dw)/2-(dw>W?(dw-W)/2*shown_pan_x:0.0f), oy=(H-dh)/2-(dh>H?(dh-H)/2*shown_pan_y:0.0f);
        const float x0=std::max(0.0f,ox), x1=std::min(float(W),ox+dw), y0=std::max(0.0f,oy), y1=std::min(float(H),oy+dh);
        SDL_Rect from{int((x0-ox)/k),int((y0-oy)/k),std::max(1,int((x1-x0)/k)),std::max(1,int((y1-y0)/k))};
        SDL_Rect to{int(x0),int(y0),int(x1-x0),int(y1-y0)};
        from.w=std::min(from.w,original->w-from.x); from.h=std::min(from.h,original->h-from.y);
        if (moving || now-picture_settled_at<200) {
            // While it moves: a quick scale. Once it rests: a sharp one, kept until it moves again.
            SDL_SetSurfaceBlendMode(original,SDL_BLENDMODE_NONE);
            SDL_BlitScaled(original,&from,screen,&to);
        } else {
            const int density=render_density::density(screen);
            const std::string key=std::to_string(density)+":"+std::to_string(to.w)+","+std::to_string(to.h)+":"+pic->path+":"+std::to_string(model.picture_rotation)+":"+std::to_string(from.x)+","+std::to_string(from.y)+","+std::to_string(from.w)+","+std::to_string(from.h);
            const SDL_Rect physical=render_density::pixel_rect(to,density);
            const bool gpu_quality=x21hook::recording(screen);
            if (!x21hook::quality(original,from,screen,physical)) {
                x21hook::Pause reference_only(gpu_quality);
            if (key!=picture_view_key) {if(gpu_quality)++x21hook::audit_picture_zoom_resamples;else ++x21hook::cpu_picture_zoom_resamples;drop_surface(picture_view); picture_view=render_density::tag(resample_art(original,from,to.w*density,to.h*density),density); picture_view_key=key;}
            if (picture_view) SDL_BlitSurface(picture_view,nullptr,screen,&to);
            else SDL_BlitScaled(original,&from,screen,&to);
            }
        }
        if (model.picture_zoom>0) {
            // Where the view is in the whole picture, at the top left (the info panel has the right).
            constexpr int mw=192, mh=108, mx=95, my=60;
            shade(mx,my,mw,mh,0x000000,150);
            const int vx=mx+int(mw*(x0-ox)/dw), vy=my+int(mh*(y0-oy)/dh), vw=std::max(4,int(mw*(x1-x0)/dw)), vh=std::max(4,int(mh*(y1-y0)/dh));
            const unsigned g=model.era>=1?media_skin::green():0x8fd23c;
            rect(vx,vy,vw,2,g); rect(vx,vy+vh-2,vw,2,g); rect(vx,vy,2,vh,g); rect(vx+vw-2,vy,2,vh,g);
        }
    } else if (full) {
        SDL_SetSurfaceBlendMode(full,SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(full,Uint8(255*a));
        SDL_BlitSurface(full,nullptr,screen,nullptr);
        SDL_SetSurfaceAlphaMod(full,255);
    } else if (can_show(pic->path)) {
        const std::string m="This picture could not be opened."; // Ex-Box addition: no 360 original
        text(m,(W-text_width(m,regular))/2,H/2-20,regular,{230,236,226,255});
    } else {
        // Ex-Box addition: no 360 original
        const std::string m="This screenshot is HDR (JPEG XR), which Ex-Box cannot show.";
        const std::string b="Set Settings > Captures and Broadcasts > Screenshot Format to JPG or PNG for new ones.";
        text(m,(W-text_width(m,regular))/2,H/2-40,regular,{230,236,226,255});
        text(b,(W-text_width(b,small))/2,H/2+14,small,{170,180,166,255});
    }
    if (model.picture_info) draw_picture_info(*pic,item,n);
    if (video_controls_shown(now)) draw_picture_controls(*pic,item,n,model.picture_slideshow,shown_zoom);
    if (model.picture_background_confirm) draw_picture_background_confirmation();
    // Once this one has been on screen a moment, decode the next one the owner
    // (or the slideshow) is heading for, so the step is a fade, not a wait.
    if (!moving && a>=1 && now-picture_shown_at>600 && n>1 && !model.picture_shuffle && !model.picture_background_confirm) {
        const int next=model.picture_slideshow?(item+1)%n:std::clamp(item+picture_step,0,n-1);
        const std::vector<int> list=model.picture_list();
        if (next!=item && next<int(list.size())) picture_decode_ahead(model.picture_files[size_t(list[size_t(next)])].path);
    }
}
void draw_viewer_full() {
    rect(0,0,W,H,0x000000);
    const int n=model.viewer_count(), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    std::string caption;
    if (model.viewer==dash::Viewer::Pictures) {
        draw_picture_full(SDL_GetTicks());
        return;
    } else if (model.viewer==dash::Viewer::Videos && n>0) {
        const std::string key=model.video_key();
        const int index=trailer_index(key);
        const Uint32 now=SDL_GetTicks();
        const MediaFade fade=media_fade(now);
        if(model.video_theater)draw_theater_room(now,fade.video);
        else if (fade.video>0) draw_video(0,0,W,H,fade.video);
        if (index>=0 && active_trailer==index && !trailer_preparing && !trailer_stream_ready()) {
            // start_trailer found no usable frames: say so rather than spin forever.
            // Ex-Box addition: no 360 original
            const std::string a="This video could not be played.";
            const std::string b="Its packed frames are damaged. Run prepare_trailers.py and deploy.py again.";
            text(a,(W-text_width(a,regular))/2,H/2-40,regular,{230,236,226,255});
            text(b,(W-text_width(b,small))/2,H/2+14,small,{170,180,166,255});
        } else if (active_trailer!=index || !trailer_stream_ready() || fade.video<1) {
            draw_spin(W/2,H/2,300,now,1);
            const std::string loading="Buffering Video..."; // dvd/XmfPrebufferingScene.xur
            text(loading,(W-text_width(loading,regular))/2,H/2+180,regular,{235,235,235,255});
        }
        const bool controls=video_controls_shown(now);
        const auto info=model.video_info.find(key);
        if (index>=0 && model.video_subtitles && info!=model.video_info.end() && info->second.subtitles)
            if (const srt::Cue *cue=srt::at(subtitles_for(trailers[size_t(index)].folder),model.video_elapsed))
                draw_subtitles(*cue,model.video_theater?735:(controls?H-300:H-70));
        if (controls) draw_video_controls(item,n,model.video_elapsed,model.video_length(),model.video_paused);
        return;
    }
    shade(0,H-120,W,120,0x000000,150);
    text(fit(caption,regular,1200),95,H-150,regular,{240,244,236,255});
    const std::string where=std::to_string(item+1)+" of "+std::to_string(n);
    text(where,W-95-text_width(where,small),H-140,small,{200,210,196,255});
}
