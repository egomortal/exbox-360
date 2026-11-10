// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
// The 1888 blades' rows: each item's own icon and its meta panel's text,
// verbatim from gamesbla / mediabla / dashmain.
struct BladesItemArt {const char *title,*icon,*text;};
constexpr BladesItemArt kBladesItemArt[]={
    {"Create Gamer Profile","ico_32x_Friend","Create a new gamer profile to save games, unlock achievements, and personalize your Xbox 360 experience"},
    {"Join Xbox Live","ico_64x_xboxlive",""},
    {"Recover Gamertag from Xbox Live","ico_64x_friend",""},
    {"Messages","ico_32x_message",""},
    {"Friends","ico_32x_Friend",""},
    {"Marketplace","ico_32x_marketplace",""},
    {"Achievements","ico_32x_achievements_white","Unlock achievements by playing games"},
    {"Played Games","ico_32x_myGames","View the games you've played so far"},
    {"Xbox Live Arcade","ico_32x_arcade","Explore and enjoy a collection of simple, fun games. New titles are always being added so check back often"},
    {"Demos","ico_32x_demos","Try game demos! You can download demos from Xbox Live Marketplace."},
    {"Trailers","ico_32x_trailers","Watch game trailers. You can download trailers from Xbox Live Marketplace"},
    {"Music","ico_32x_MusicDisk","Play music! Rip CDs, stream music from a computer or portable device, or play music from a CD."},
    {"Pictures","ico_32x_DigitalCamera","View pictures stored on a camera, CD, computer, or portable device."},
    {"Video","ico_64x_TrailersFolder_white","Watch videos stored on your Xbox 360 Hard Drive."},
    {"Media Center","ico_32x_MCX","Extend your entertainment with Windows Media Center.\n\u2022 Live & recorded TV\n\u2022 On-demand movies\n\u2022 Music and photo"},
    {"Console Settings","ico_32x_console","Edit your Xbox 360 system settings, including:\n\u2022 Display\n\u2022 Audio\n\u2022 Language\n\u2022 Remote control\n\u2022 and more"},
    {"Family Settings","ico_32x_parentalControl","Protect younger family members by customizing content, communications, and online interactions. Adjust settings on the console or at the individual profile level."},
    {"Memory","ico_32x_MU","Move or delete saved games, profiles, and other items on Xbox 360 storage devices."},
    {"Network Settings","ico_32x_network","Connect your console to your home network or Xbox Live. Set up wireless network connections and test network settings."},
    {"Computers","ico_32x_PC","Manage your connection to a Windows XP or Media Center PC."},
    {"Initial Setup","ico_32x_systemSet","Run the system setup that you saw the first time you started the console."},
    // consoles/dashSysCslSet: text-only buttons; their meta text verbatim.
    {"Display","","Change your display output settings. Display settings include screen size, output mode, and HDTV."},
    {"Audio","","Change your audio output settings."},
    {"Language","","Change your console's language setting."},
    {"Clock","","Set the date, time, and other clock settings.\nWhen connected to Xbox Live, the clock is set automatically and can't be changed."},
    {"Locale","","Specify the locale you live in.\nYou can't change locale if Family Settings are enabled.\nChanging locale does not change the console's DVD region code."},
    {"Auto-Off","","Turn off your console after 6 hours of inactivity."},
    {"Screen Saver","","Automatically dim the screen to prevent damage to your display."},
    {"Remote Control","","Change your Xbox 360 remote control settings."},
    {"System Info","","View your console's system information."},
    // consoles/dashSysCslSetPControlSelect and dashSysCslSetPControl, verbatim.
    {"Console Controls","","Determine Family Settings for your console. These settings affect everyone who uses your console. Manage access to the following features:\n\u2022 Games\n\u2022 DVD movies\n\u2022 Xbox Live\n\u2022 Xbox Live membership creation"},
    {"Xbox Live Controls","","Determine Family Settings for your child's Xbox Live membership. Manage access to the following online features:\n\u2022 Online friends\n\u2022 Communications\n\u2022 Gamer profiles\n\u2022 Online status\n\u2022 Online games\n\u2022 Member content\n\u2022 Downloadable content"},
    {"Game Ratings","","Game ratings allow you to decide the games that can be played on this console.\n\nOnly games with the selected rating level and lower can be played."},
    {"DVD Movie Ratings","","DVD movie ratings control the level of movies that can be played on this console. Only movies with the selected rating level and lower can be played."},
    {"Access to Xbox Live","","Manage access to Xbox Live from this console.\n\nYou must have a high-speed connection and a valid Xbox Live membership to play on Xbox Live."},
    {"Xbox Live Membership Creation","","Decide whether new Xbox Live memberships can be created from this console."},
    {"Original Xbox Games","","Decide whether original Xbox games can be played on this console."},
    {"Set Pass Code","","Set a pass code to protect your Family Settings and to override game and movie ratings.\n\nYou are required to create a pass code if any Family Settings are used."},
    {"Turn Off Family Settings","","Reset and disable Family Settings on your console."},
    // memory: DeviceSelector and Categories, with memory's own 64 px icons.
    {"All Devices","ico_64x_AllDevices","Select an Xbox 360 storage device."},
    {"Hard Drive","ico_64x_HD","Select an Xbox 360 storage device."},
    {"Games","ico_32x_myGames",""},
    {"Videos and Trailers","ico_64x_Trailers",""},
    {"Gamer Pictures","ico_64x_profiles",""},
    {"Themes","ico_64x_skins",""},
};
// The live line under a settings page's meta text: the console's own state,
// in dashCSettingsStrings / memory Strings wording.
std::string blades_page_state(const std::string &title) {
    auto items=[](size_t n) {return std::to_string(n)+(n==1?" item":" items");};
    if (title=="Screen Saver") return model.dimming?"Screen Saver Enabled":"Screen Saver Disabled";
    if (model.blades_page==5) return title=="Game Ratings"?"Allow All Games":title=="DVD Movie Ratings"?"Allow All DVD Movies":
                                     title=="Set Pass Code"?"<Not set>":"Allowed";   // dashCSettingsStrings
    if (title=="Auto-Off") return "Auto-Off Disabled";
    if (title=="Display") return "Screen Resolution\n1920 x 1080";
    if (title=="Audio") return model.sounds?"Sounds: On":"Sounds: Off";
    if (title=="System Info") return "Dashboard: 2.0."+std::to_string(model.dashboard_build)+".0";
    if (title=="Hard Drive" || title=="All Devices") return model.storage_free;
    if (model.blades_page==3) {
        if (title=="Games") return items(model.games.size());
        if (title=="Music") return items(size_t(std::max(0,int(music_songs.size()))));
        if (title=="Videos and Trailers") return items(model.video_list().size());
        if (title=="Gamer Pictures") return items(gamerpics.size());
        if (title=="Themes") return items(size_t(dash::kThemeCount));
    }
    return "";
}
const BladesItemArt *blades_item_art(const std::string &title) {
    for (const auto &item:kBladesItemArt) if (title==item.title) return &item;
    return nullptr;
}
SDL_Surface *blades_item_icon(const char *name) {
    auto it=blades_system_icons.find(std::string("items/")+name);
    return it==blades_system_icons.end()?nullptr:it->second;
}
// The Media blade's Music item while a song plays: a small Now Playing strip
// in its meta panel, under the item's text: the cover, the title, the artist.
void draw_blades_now_playing(int x,int y,int w,const era_chrome::Palette &p) {
    const music::Song *song=music_player.current_song();
    if (!song) return;
    TTF_Font *body=blades_body?blades_body:era_text;
    TTF_Font *caption=blades_caption?blades_caption:regular;
    const SDL_Color ink{15,15,15,230}, soft{15,15,15,160};
    constexpr int h=100;
    rounded(x-3,y-3,w+6,h+6,14,era_chrome::mix(p.dark,0x000000,.35f),era_chrome::mix(p.dark,0x000000,.15f));
    rounded(x,y,w,h,12,era_chrome::mix(p.high,0xffffff,.2f),p.light);
    rect(x+10,y+9,82,82,0x2c342c);
    rect(x+11,y+10,80,80,0x50544f);
    if (SDL_Surface *cover=song_cover(song->path)) blit_contained(cover,{x+12,y+11,78,78});
    else if (SDL_Surface *icon=music_kind_art[0]) blit_contained(icon,{x+24,y+23,54,54});
    const int tx=x+110, tw=w-126;
    const std::string state=model.music_paused?"Paused":"Now Playing";
    const int badge=custom_scene::draw_equalizer(tx,y+7)?86:0;
    blades_type::draw(blades_type::fit(song->title,body,tw-badge),tx+badge,y+2,body,ink);
    const std::string by=song->artist.empty()?state:song->artist;
    blades_type::draw(blades_type::fit(by,caption,tw),tx,y+50,caption,soft);
}
void draw_blades_hub(bool draw_chrome=true,bool allow_video=true) {
    TTF_Font *body=blades_body?blades_body:era_text;
    TTF_Font *caption=blades_caption?blades_caption:regular;
    int active=0;
    while (active<4 && dash::kBladesChannels[active]!=model.channel) active++;
    const auto &p=era_chrome::palettes[active];
    const SDL_Color ink{15,15,15,230};
    const int first=model.dashboard_build<5759 && active>0?1:0;
    SDL_Surface *chrome=draw_chrome?era_chrome::get_blades(model.dashboard_build,active):nullptr;
    if (draw_chrome && chrome) SDL_BlitSurface(chrome,nullptr,screen,nullptr);
    else if (draw_chrome) gradient(0,0,W,H,p.mid,p.light);
    const auto layout=era_chrome::layout(active,first!=0);
    const int face_right=layout.edge;
    const int content_x=layout.content, right_x=layout.right, right_w=face_right-right_x-30; // the designer captures wrap "Entertainment. All the rewards." on one line
    const bool original_side_text=era_chrome::has_source_labels(model.dashboard_build,active);
    for (int i=first;draw_chrome && !original_side_text && i<5;i++) {
        const auto label=era_chrome::rail_label(active,i,first!=0);
        SDL_Surface *letters=blades_labels[blades_rails::family(model.dashboard_build)][i==active][i];
#ifndef __PROSPERO__
        if (getenv("NXE_RAIL_HIDE")) letters=nullptr; // Matched native backdrop for ink measurements.
#endif
        if (letters) {
            SDL_Rect at{label.center-(render_density::logical_w(letters)+1)/2,label.top,0,0};
            SDL_BlitSurface(letters,nullptr,screen,&at);
        }
    }
    const char *headings[]={"Marketplace","Xbox Live","Games","Media","System"};
    const std::string heading=model.channel==dash::MyXbox?model.blades_page_title():headings[active];
    blades_type::draw(heading,content_x+10,62,blades_heading?blades_heading:body,{235,235,235,250});
    const int selected=model.current();
    const Uint32 now=SDL_GetTicks();
    static int previous_blade=-1,previous_selection=-1,previous_build=-1;
    static Uint32 focus_at=0;
    if (previous_blade!=active || previous_selection!=selected || previous_build!=model.dashboard_build) {
        previous_blade=active;previous_selection=selected;previous_build=model.dashboard_build;focus_at=now;
    }
    auto material=[&](SDL_Surface *image,int x,int y,Uint8 alpha=255) {
        if (!image) return;
        SDL_SetSurfaceAlphaMod(image,alpha);SDL_Rect at{x,y,0,0};
        SDL_BlitSurface(image,nullptr,screen,&at);SDL_SetSurfaceAlphaMod(image,255);
    };
    auto draw_row_material=[&](int x,int y,bool focused) {
        if (auto *image=era_chrome::row_material[focused?1:0]) {
            material(image,x-18,y-18);
            if (focused) {
                const auto glow=era_chrome::row_glint(now-focus_at);
#ifndef __PROSPERO__
                if (getenv("NXE_MATERIAL_TRACE")) fprintf(stderr,"glint,%u,%u,%.3f,%.3f\n",now,now-focus_at,glow.x,glow.alpha);
#endif
                SDL_Rect saved,clip{x,y,584,74},intersection;
                SDL_GetClipRect(screen,&saved);SDL_IntersectRect(&saved,&clip,&intersection);
                SDL_SetClipRect(screen,&intersection);
                material(era_chrome::row_shine,x+int(std::lround(glow.x*584/420)),y+5,Uint8(glow.alpha*255));
                SDL_SetClipRect(screen,&saved);
            }
        } else {
            if (focused) gradient(x,y,584,64,era_chrome::mix(p.light,0xffffff,.35f),p.mid);
            rect(x,y+65,584,3,p.dark);
        }
    };
    if (model.channel==dash::MyXbox) {
        const int list_x=content_x,list_w=584;
        const int first=std::clamp(selected-3,0,std::max(0,model.count()-8));
        for (int row=0;row<8 && first+row<model.count();row++) {
            const int index=first+row,y=166+row*74;
            draw_row_material(list_x,y,index==selected);
            // Each item with its own icon from the 1888 System blade.
            const std::string title=model.card(index).title;
            const BladesItemArt *art=blades_item_art(title);
            const bool text_only=art && !*art->icon; // dashSysCslSet's buttons carry no icon
            SDL_Surface *icon=art && !text_only?blades_item_icon(art->icon):nullptr;
            if (!icon && !text_only) icon=blades_icons[dash::IconSettings];
            if (icon) blit_contained(icon,{list_x+6,y+11,46,46});
            const int tx=text_only?list_x+24:list_x+84;
            blades_type::draw(blades_type::fit(model.card(index).title,body,list_w-(tx-list_x)-13),tx,y+4,body,ink);
        }
        // The System blade's settings sit in a recessed column: a soft vertical
        // edge runs down beside the list, between it and the description.
        {
            const int x=list_x+list_w+10;
            shade(x,166,2,716,0x000000,60);
            shade(x+2,166,1,716,0xffffff,70);
        }
        const std::string current=model.current_card().title;
        std::string meta;
        if (const BladesItemArt *art=blades_item_art(current)) meta=art->text;
        if (model.blades_page>0) {
            const std::string state=blades_page_state(current);
            if (!state.empty()) meta+=(meta.empty()?"":"\n\n")+state;
        }
        // dashmain's meta text: a paragraph, then bulleted lines.
        if (!meta.empty()) blades_type::wrapped(meta,right_x+10,154,right_w-10,12,body,ink);
        return;
    }
    if (!era_chrome::profile_material) rounded(content_x-3,141,590,214,18,p.dark,p.mid);
    const unsigned profile_top[]={0xe3a67b,0xe4b762,0x9cc895,0x95afc5,0xb8a6ce};
    const unsigned profile_bottom[]={0xf2c8a2,0xeab74d,0x94ca88,0x87a9ce,0xd7c9e5};
    const unsigned profile_gold[]={0xd3cf72,0xd89834,0xbcc268,0xb9b984,0xd3cf72};
    // The gamercard leads the list (row 0); focused, its name bar turns gold.
    const bool listed=model.channel!=dash::Apps;
    const bool card_focus=listed && selected==0;
    rounded(content_x,144,584,208,15,profile_top[active],profile_bottom[active]);
    if (card_focus) gradient(content_x+4,148,576,48,era_chrome::mix(profile_gold[active],0xffffff,.12f),profile_gold[active]);
    else gradient(content_x+4,148,576,48,era_chrome::mix(profile_top[active],0xffffff,.08f),era_chrome::mix(profile_top[active],0x000000,.06f));
    material(era_chrome::profile_material,content_x-18,144-18);
    if (model.blades_signed_out) {
        // gamesSignedOut's fake gamercard: Sign In over the default gamer tile.
        blades_type::draw("Sign In",content_x+16,144,body,ink);
        rect(content_x+20,215,120,114,0x2c342c);rect(content_x+21,216,118,112,0x50544f);
        auto found=blades_system_icons.find("ico_64x_DefaultPicture");
        if (found!=blades_system_icons.end()) blit_contained(found->second,{content_x+30,225,99,99});
    } else {
    blades_type::draw(blades_type::fit(model.player,body,545),content_x+16,144,body,ink);
    // The gamerpic in its recessed grey frame (designer capture: 120 x 114
    // frame, 99 px picture inset 10 px).
    rect(content_x+20,215,120,114,0x2c342c);
    rect(content_x+21,216,118,112,0x50544f);
    draw_gamerpic(content_x+30,225,99,99);
    // The 360 gamercard's three rows: Rep (stars), Gamerscore (the G glyph is the
    // word's first letter) and Zone. Gamerscore shows the PS5 trophy points.
    // Xbox reputation and Zone are unavailable; PS5 trophies are not reputation.
    const std::string points=(trophies.known?std::to_string(trophies.points()):std::string{});
    if (blades_gscore) {
        SDL_Rect at{content_x+156,244,44,44};SDL_BlitScaled(blades_gscore,nullptr,screen,&at);
        blades_type::draw("amerscore",content_x+199,245,caption,ink);
    } else blades_type::draw("Gamerscore",content_x+162,245,caption,ink);
    blades_type::draw(points,content_x+562-blades_type::width(points,caption),245,caption,ink);
    }
    SDL_Surface *logo=model.channel==dash::Friends && xbox_live_logo?xbox_live_logo:xbox_logo;
    if (logo) blit_contained(logo,{right_x+80,149,std::max(50,right_w-140),192});
    const int row_count=model.count()-1;
    const int first_row=listed?1:0;
    const int start=std::max(first_row,std::min(selected-2,row_count-5));
    for (int line=0;line<5 && start+line<row_count;line++) {
        const int index=start+line,y=375+line*74;
        const dash::Card card=model.card(index);
        draw_row_material(content_x+10,y,index==selected);
        // Blades used small category glyphs, not miniature box covers in rows.
        const BladesItemArt *art=blades_item_art(card.title);
        SDL_Surface *icon=art?blades_item_icon(art->icon):nullptr;
        if (icon) blit_contained(icon,{content_x+17,y+5,57,53});
        const bool category=!icon;
        if (!icon && card.icon>=0 && card.icon<dash::IconCount) icon=blades_icons[card.icon]?blades_icons[card.icon]:card_icons[card.icon];
        if (icon && category) {
            SDL_Rect upper=solid_icon_source(card.icon,icon);
            blit_contained(icon,{content_x+17,y+5,57,53},category && !blades_native[card.icon]?&upper:nullptr);
        }
        blades_type::draw(blades_type::fit(promotion_art::label(card),body,465),content_x+104,y+4,body,ink);
    }
    const dash::Card card=model.current_card();
    // With the gamercard focused the meta panel is the blade's message of the
    // day, the Xbox Live billboard text every capture shows.
    const BladesItemArt *meta=blades_item_art(card.title);
    const std::string heading_text=card_focus?std::string("Xbox Live"):promotion_art::label(card);
    blades_type::draw(blades_type::fit(heading_text,body,right_w),right_x,379,body,ink);
    const std::string detail=card_focus?"Games. Tournaments. Entertainment. All the rewards. Endless possibilities. What are you waiting for?"
                            :meta?meta->text:model.channel==dash::Apps?"Open an app from your library.":"";
    if (card.promo<0 && !detail.empty()) blades_type::wrapped(detail,right_x,433,right_w,7,body,ink);
    // The custom hero uses a 4:3 region beside the rows, separately from the
    // preserved narrow service banner. Its position remains an adaptation.
    const int hero_top=433;
    const int hero_height=std::min(right_w*3/4,782-hero_top-24);
    const int hero_width=hero_height*4/3;
    const SDL_Rect preview=card.promo>=0
        ? SDL_Rect{right_x+(right_w-hero_width)/2,hero_top,hero_width,hero_height}
        : SDL_Rect{right_x,551,right_w,208};
    const auto fade=allow_video?hub_media_fade(SDL_GetTicks()):decltype(hub_media_fade(Uint32{})){};
    if (fade.video>0) draw_video(preview.x,preview.y,preview.w,preview.h,fade.video);
    else if (!card.image.empty()) {
        if (card.promo>=0) promotion_art::draw(card.image,preview,{},selected);
        // Blades showed no box art beside the rows; only promotions keep a picture.
    }
    const bool music_item=card.goal==dash::Goal::ToMusic || (model.channel==dash::Media && card.title=="Music"); // the blade's own row has no goal
    if (!card_focus && music_item && model.now_playing>=0 && fade.video<=0)
        draw_blades_now_playing(right_x,662,std::min(right_w,600),p);
    if (model.channel==dash::Friends && model.promotions && model.ad_mode[model.current_ad_profile()]==0 && era_banner) {
        // The original framed slot: a thin dark border, the banner art scaled to the
        // slot's height at its left, and its grey carried across the rest.
        const SDL_Rect slot{content_x,610,584,146};
        rounded(slot.x-3,slot.y-3,slot.w+6,slot.h+6,12,era_chrome::mix(p.dark,0x000000,.35f),era_chrome::mix(p.dark,0x000000,.15f));
        Uint8 r=80,g=80,b=80;
        if (era_banner->format->BytesPerPixel==4 && SDL_LockSurface(era_banner)==0) {
            const auto *row=static_cast<const Uint8 *>(era_banner->pixels)+(era_banner->h/2)*era_banner->pitch;
            Uint8 a;SDL_GetRGBA(reinterpret_cast<const Uint32 *>(row)[era_banner->w-4],era_banner->format,&r,&g,&b,&a);
            SDL_UnlockSurface(era_banner);
        }
        rounded(slot.x,slot.y,slot.w,slot.h,10,(unsigned(r)<<16)|(unsigned(g)<<8)|b,(unsigned(r)<<16)|(unsigned(g)<<8)|b);
        const int art_w=std::min(slot.w-8,era_banner->w*(slot.h-8)/std::max(1,era_banner->h));
        SDL_Rect art{slot.x+4,slot.y+4,art_w,slot.h-8};SDL_BlitScaled(era_banner,nullptr,screen,&art);
    }
    // Recessed tray bar, with a separate circular disc control at its left.
    const int tray_w=face_right-content_x-46;
    const bool tray_selected=selected==row_count;
    if (auto *image=era_chrome::tray_material[first?1:0][active][tray_selected?1:0]) {
        material(image,content_x-18,782-18);
        if (tray_selected) {
            const auto glow=era_chrome::tray_glint(now-focus_at);
            const float scale=float(tray_w)/712.061646f;
            SDL_Rect saved,clip{content_x,782,tray_w,105},intersection;
            SDL_GetClipRect(screen,&saved);SDL_IntersectRect(&saved,&clip,&intersection);SDL_SetClipRect(screen,&intersection);
            material(era_chrome::tray_shine[first?1:0][active],content_x+int(std::lround((glow.x-15.136129f)*scale)),792,Uint8(glow.alpha*255));
            SDL_SetClipRect(screen,&saved);
        }
    } else {
        rounded(content_x+25,790,tray_w-25,70,35,p.dark,p.high);
        rounded(content_x+28,794,tray_w-31,62,31,tray_selected?p.high:p.mid,p.light);
        rounded(content_x-3,768,112,112,56,p.dark,p.high);
        rounded(content_x+2,773,102,102,51,p.light,p.mid);
    }
    if (auto *icon=blades_item_icon("ico_32x_Eject")) blit_contained(icon,{content_x+30,802,42,42});
    else if (auto *icon=blades_icons[dash::IconDisc]) blit_contained(icon,{content_x+30,802,42,42});
    blades_type::draw(blades_type::fit("Open Tray",body,tray_w-150),content_x+128,798,body,ink);
}
