// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_viewer() {
    if (model.viewer==dash::Viewer::RomGameGuidePreview) {draw_rom_game_guide_preview();return;}
    if (model.viewer==dash::Viewer::WelcomePages || model.viewer==dash::Viewer::InitialSetup) {draw_welcome_setup();return;}
    if (model.viewer==dash::Viewer::Profile) {draw_profile();return;}
    if (model.viewer==dash::Viewer::Achievements) {draw_achievements();return;}
    if (model.viewer==dash::Viewer::Themes) {draw_theme_picker();return;}
    if (model.viewer==dash::Viewer::AdsSettings) {draw_ads_settings();return;}
    if (model.viewer==dash::Viewer::DashboardEras || model.viewer==dash::Viewer::DashboardUpdates) {draw_dashboard_picker();return;}
    if (model.viewer==dash::Viewer::Library) {draw_library();return;}
    if (model.viewer==dash::Viewer::AvatarEdit) {draw_avatar_editor(); return;}
    if (model.viewer==dash::Viewer::Avatar) {draw_avatar_stage(); return;}
    if (model.viewer==dash::Viewer::Music) {draw_music(); return;}
    if (model.viewer==dash::Viewer::SystemSettings) {draw_system_settings(); return;}
    if (model.viewer==dash::Viewer::Videos && !model.video_pending.empty()) {draw_video_start();return;}
    if (model.viewer_full) {draw_viewer_full(); return;}
    const bool pictures=model.viewer==dash::Viewer::Pictures;
    // NXE, Kinect and Metro: videos/VideoContent and pictures/905_IndividualDeviceMain
    // in the era's skin. Label_Head over the dashboard's background, the grid
    // on the BackgroundPanel (Pictures, whose btn_picturesGrid captions are
    // white, on 13604's grey RightPanelShader), the focus btn_picturesGrid's
    // (9199 greenBox, 13604 GreenHighlight behind tile and caption). Blades
    // keeps the dark blade panel.
    namespace sk=media_skin;
    const bool skin=model.era>=1, light=sk::light();
    const int px=120, py=skin?150:90, pw=1680, ph=skin?810:895;
    if (skin) {
        sk::scene_background();
        sk::background_panel(px,py,pw,ph);
        if (light && pictures) sk::panel_shader(px+20,py+16,pw-40,ph-32);
    } else {
        shade(0,0,W,H,0x000000,185);
        if (blade_dark) nine_slice(blade_dark,px-18,py-18,pw+36,ph+36,24,36);
        else rounded(px,py,pw,ph,12,0x42505e,0x2c3742);
    }
    // Where the heading row sits: inside the blade panel, else over the background.
    const int hy=skin?py-(light?66:80):py+40;
    TTF_Font *head=skin && light && era_text?era_text:large;
    TTF_Font *tab_font=skin && era_text?era_text:regular;
    const SDL_Color white{255,255,255,255};
    const SDL_Color idle=skin?SDL_Color{235,235,235,150}:SDL_Color{140,154,164,255};
    const SDL_Color counter=skin?(light?sk::kInk:sk::kSub):SDL_Color{196,208,216,255}; // Label_SubRight / Label_Sub12_white
    const SDL_Color meta_ink=skin?(light?sk::kInk:sk::kSub):SDL_Color{170,182,190,255};
    // Pictures: the Games tab shows one tile per game until one is opened.
    const bool groups=pictures && model.picture_groups();
    const std::vector<std::string> games=pictures?model.picture_games():std::vector<std::string>{};
    std::string title=pictures?dash::media_text::pictures_title(model):dash::media_text::videos_title(model);
    if (pictures && model.picture_group>=0 && model.picture_group<int(games.size())) title=fit(games[size_t(model.picture_group)],large,640);
    const int heading=text(title,px+(skin?30:70),hy,head,skin?sk::kInk:white);
    const int n=model.viewer_count();
    const int item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const std::vector<std::string> videos=pictures?std::vector<std::string>{}:model.video_list();
    const std::vector<int> shots=pictures && !groups?model.picture_list():std::vector<int>{};
    // The picture a tile stands for: its own, or (on the Games tab) the game's
    // newest; as a cover, the newest that can be shown.
    auto tile_picture=[&](int i, bool cover=false) -> const dash::PictureFile * {
        if (!groups) return i<int(shots.size())?&model.picture_files[size_t(shots[size_t(i)])]:nullptr;
        const std::vector<int> list=model.picture_group_list(i);
        if (cover) for (int at:list) if (can_show(model.picture_files[size_t(at)].path)) return &model.picture_files[size_t(at)];
        return list.empty()?nullptr:&model.picture_files[size_t(list.front())];
    };
    {
        // VideoContent uses the next sort in the X legend; only show the active sort here.
        const char *const *orders=pictures?dash::kPictureOrders:dash::kVideoOrders;
        const int count=pictures?dash::kPictureOrderCount:dash::kVideoOrderCount, chosen=pictures?model.picture_order:model.video_order;
        int at=px+(skin?30:70)+heading+44;
        const int ty=skin?hy+TTF_FontHeight(head)-TTF_FontHeight(tab_font)-4:py+56;
        for (int v=0;v<count;v++) {
            if (!pictures && v!=chosen) continue;
            const bool here=v==chosen;
            const int w=text_width(orders[v],tab_font);
            text(orders[v],at,ty,tab_font,here?(skin?sk::kInk:white):idle);
            if (here) rect(at,skin?ty+TTF_FontHeight(tab_font)+2:py+100,w,4,skin?sk::green():0x8fd23c);
            at+=w+36;
        }
    }
    if (!pictures) {
        if (const int hidden=model.video_hidden()) {
            // Clips in manifest.tsv whose frames are not on the console: said, not shown.
            const std::string note=std::to_string(hidden)+(hidden==1?" video in the manifest is":" videos in the manifest are")+" not on the console"; // Ex-Box addition: no 360 original
            // Label_SubRight: 9199 0xebebeb at 0.7, 13604 on 0x333a40 (over the background's pale foot).
            text(note,px+pw-text_width(note,small),1008,small,skin?(light?sk::kRowSel:sk::kSub):SDL_Color{150,162,170,255});
        }
    }
    if (n>0) {
        const std::string where=std::to_string(item+1)+" of "+std::to_string(n);
        if (skin) text(where,px+pw-text_width(where,regular),hy-34,regular,counter);
        else text(where,px+pw-70-text_width(where,regular),py+36,regular,counter);
    }
    if (pictures && n>0) if (const dash::PictureFile *pic=tile_picture(item)) {
        // The selected picture's date, size and game, as Game Videos gives a clip's.
        std::string meta;
        auto add=[&meta](const std::string &part) {meta+=(meta.empty()?"":"  \xC2\xB7  ")+part;};
        if (groups) {
            const size_t count=model.picture_group_list(item).size();
            add(count==1?std::string("1 picture"):std::to_string(count)+" pictures"); // Ex-Box addition: no 360 original
            add("Newest "+dash::picture_date(pic->taken)); // Ex-Box addition: no 360 original
        } else {
            add(dash::picture_date(pic->taken));
            const auto size=picture_sizes.find(pic->path);
            if (size!=picture_sizes.end()) add(std::to_string(size->second.first)+" \xC3\x97 "+std::to_string(size->second.second));
            if (pic->bytes>0) add(picture_size_label(pic->bytes));
            if (is_jxr(pic->path)) add("HDR"); // Ex-Box addition: no 360 original
            if (model.picture_group<0 && pic->label()!="Other") add(fit(pic->label(),small,420)); // an opened game is the heading
        }
        if (skin) text(meta,px+pw-text_width(meta,small),hy+20,small,meta_ink);
        else text(meta,px+pw-70-text_width(meta,small),py+84,small,meta_ink);
    }
    if (!pictures && n>0) {
        // The selected clip's length, size, picture and what is left of it.
        const std::string &key=videos[size_t(item)];
        const auto info=model.video_info.find(key);
        std::string meta;
        auto add=[&meta](const std::string &part) {meta+=(meta.empty()?"":"  \xC2\xB7  ")+part;};
        if (info!=model.video_info.end()) {
            if (info->second.ms>0) add(music::clock(info->second.ms/1000.0));
            if (info->second.bytes>0) add(video_size_label(info->second.bytes));
        }
        const auto size=video_sizes.find(key);
        if (size!=video_sizes.end()) add(std::to_string(size->second.first)+" \xC3\x97 "+std::to_string(size->second.second));
        if (info!=model.video_info.end() && info->second.subtitles) add("Subtitles"); // dvd/DvdPadDisplay.xur "Subtitles"
        const auto resume=model.video_resume.find(key);
        if (resume!=model.video_resume.end()) add("Resume at "+music::clock(resume->second/1000.0)); // Ex-Box addition: no 360 original
        if (skin) text(meta,px+pw-text_width(meta,small),hy+20,small,meta_ink);
        else text(meta,px+pw-70-text_width(meta,small),py+84,small,meta_ink);
    }
    if (n==0) {
        const int hidden=pictures?0:model.video_hidden();
        // Videos: videos/VideoContent.xur "No videos found." (9199 on; 6717 opens
        // its longer text with it). Pictures had no fixed text: an Ex-Box addition.
        const std::string a=pictures?"No screenshots yet.":"No videos found.";
        // Ex-Box addition: no 360 original (where the PS5 keeps them)
        const std::string b=pictures?"Screenshots you take on the PS5 (JPG or PNG) show up here. Press Y to look again.":
                            hidden?std::to_string(hidden)+" are listed in manifest.tsv, but their packed frames are missing. Run deploy.py, then press Y.":
                                     "Run prepare_trailers.py and deploy.py to add trailers.";
        const bool dark_ink=skin && light && !pictures; // on the light panel: Label_Body_1line / Label_Sub
        text(a,px+70,skin?py+50:py+170,regular,dark_ink?SDL_Color{0x0f,0x12,0x14,255}:skin?sk::kInk:SDL_Color{230,236,226,255});
        text(b,px+70,skin?py+100:py+220,small,dark_ink?SDL_Color{0x33,0x3a,0x40,255}:skin?sk::kSub:SDL_Color{170,180,166,255});
        return;
    }
    constexpr int cols=dash::kViewerColumns, rows=dash::kViewerRows, gx=40, gy=44;
    const int x0=px+(pw-cols*kTileW-(cols-1)*gx)/2, y0=py+(skin?40:112); // third-row captions clear the button prompts at 1001
    const int total_rows=(n+cols-1)/cols;
    const int top=std::clamp(item/cols-1,0,std::max(0,total_rows-rows));
    const Uint32 now=SDL_GetTicks();
    int thumb_budget=1; // one new thumbnail decode per frame keeps the grid moving
    if (pictures) if (const dash::PictureFile *pic=tile_picture(item,true)) picture_thumb(pic->path,&thumb_budget); // the selected one first
    for (int r=0;r<rows;r++) for (int c=0;c<cols;c++) {
        const int i=(top+r)*cols+c;
        if (i>=n) break;
        const int x=x0+c*(kTileW+gx), y=y0+r*(kTileH+gy);
        const bool on=i==item;
        const dash::PictureFile *pic=pictures?tile_picture(i,true):nullptr;
        if (on && skin) {
            // btn_picturesGrid / btn_LibBrowse focus, around the tile and its caption.
            if (light) sk::green_highlight(x-8,y-8,kTileW+16,kTileH+gy+4);
            else sk::green_box(x-8,y-8,kTileW+16,kTileH+gy+4);
        }
        if (groups) {rect(x+10,y+8,kTileW,kTileH,0x232c35); rect(x+5,y+4,kTileW,kTileH,0x323d48);} // a stack: a game's album
        if (on && !skin) rect(x-5,y-5,kTileW+10,kTileH+10,0x8fd23c);
        rect(x,y,kTileW,kTileH,0x0e1216);
        std::string caption;
        if (pictures && pic) {
            const std::string &path=pic->path;
            caption=groups?games[size_t(i)]:dash::picture_date(pic->taken)+
                (model.picture_group<0 && pic->label()!="Other"?"  \xC2\xB7  "+pic->label():std::string());
            if (SDL_Surface *t=picture_thumb(path,&thumb_budget)) {SDL_Rect d{x,y,kTileW,kTileH}; SDL_BlitSurface(t,nullptr,screen,&d);}
            else if (can_show(path)) {
                const bool waiting=picture_thumbs.find(path)==picture_thumbs.end();
                text(waiting?dash::media_text::loading_picture(model):"Could not open",x+20,y+kTileH/2-14,small,{170,180,166,255}); // "Could not open": Ex-Box addition: no 360 original
            } else {
                // Ex-Box addition: no 360 original
                text("HDR screenshot",x+20,y+kTileH/2-34,regular,{220,228,216,255});
                text("JPEG XR: not shown",x+20,y+kTileH/2+6,small,{160,170,158,255});
            }
            // A corner badge, as Game Videos gives a clip's length: a game's count, or HDR.
            const std::string badge=groups?std::to_string(model.picture_group_list(i).size()):is_jxr(path)?"HDR":"";
            if (!badge.empty()) {
                const int w=text_width(badge,small);
                shade(x+kTileW-w-20,y+kTileH-42,w+14,34,0x000000,170);
                text(badge,x+kTileW-w-13,y+kTileH-39,small,{240,244,236,255});
            }
        } else if (!pictures) {
            const std::string &key=videos[size_t(i)];
            caption=trailer_name(key);
            const MediaFade fade=on?media_fade(now):MediaFade{};
            if (fade.video<1) {
                if (SDL_Surface *t=video_thumb(key,&thumb_budget)) {SDL_Rect d{x,y,kTileW,kTileH}; SDL_BlitSurface(t,nullptr,screen,&d);}
                else if (disc_art && !(on && fade.spin>0)) {SDL_Rect d{x+kTileW/2-60,y+kTileH/2-60,120,120}; blit_quality(disc_art,nullptr,screen,&d);}
            }
            if (on && fade.video<1 && fade.spin>0) draw_spin(x+kTileW/2,y+kTileH/2,150,now,fade.spin);
            if (on && fade.video>0) draw_video(x,y,kTileW,kTileH,fade.video);
            // The length in a corner, and a green bar for a video left part way.
            const auto info=model.video_info.find(key);
            const auto resume=model.video_resume.find(key);
            const bool part=resume!=model.video_resume.end() && info!=model.video_info.end() && info->second.ms>0;
            if (info!=model.video_info.end() && info->second.ms>0) {
                const std::string length=music::clock(info->second.ms/1000.0);
                const int w=text_width(length,small);
                shade(x+kTileW-w-20,y+kTileH-(part?50:42),w+14,34,0x000000,170);
                text(length,x+kTileW-w-13,y+kTileH-(part?47:39),small,{240,244,236,255});
            }
            if (part) {
                shade(x,y+kTileH-8,kTileW,8,0x000000,190);
                rect(x,y+kTileH-8,int(kTileW*std::clamp(double(resume->second)/info->second.ms,0.0,1.0)),8,skin?sk::green():0x8fd23c);
            }
        }
        if (!skin) text(fit(caption,small,kTileW),x,y+kTileH+10,small,on?SDL_Color{255,255,255,255}:SDL_Color{178,190,198,255});
        else if (!light) sk::shadowed(fit(caption,small,kTileW),x,y+kTileH+8,small,on?sk::kInk:sk::kSub); // Label_MovieDetailsWhite / Label_SubRight
        else if (pictures || on) text(fit(caption,small,kTileW),x,y+kTileH+8,small,sk::kInk);      // btn_picturesGrid, 12 pt white
        else text(fit(caption,small,kTileW),x,y+kTileH+8,small,{0x65,0x6d,0x72,178});             // btn_LibBrowse text_Title: 0x656d72 at 0.7
    }
    if (pictures && !groups) {
        // Ahead of the owner, while nothing on screen waits: the row below, then the one above.
        for (int i=(top+rows)*cols;thumb_budget>0 && i<std::min(n,(top+rows+1)*cols);i++)
            if (const dash::PictureFile *pic=tile_picture(i)) picture_thumb(pic->path,&thumb_budget);
        for (int i=std::max(0,(top-1)*cols);thumb_budget>0 && i<top*cols;i++)
            if (const dash::PictureFile *pic=tile_picture(i)) picture_thumb(pic->path,&thumb_budget);
    }
    if (top>0) guide::chevron(px+pw-60,y0-(skin?24:34),false);
    if (top+rows<total_rows) guide::chevron(px+pw-60,y0+rows*(kTileH+gy)-(skin?14:20),true);
}
