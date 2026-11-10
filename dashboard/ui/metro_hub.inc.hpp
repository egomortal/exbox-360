// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
// Template6 / Template6HD settings, also used by neighboring-tab previews.
void draw_metro_settings_tiles(int offset,bool focus) {
    const bool v1=model.metro_v1();
    const int first=focus?model.current()/8*8:0;
    auto draw=[&](int i) {
        const int slot=i-first;
        const auto bounds=dash::metro_settings_bounds(slot,v1,offset);
        SDL_Rect d{bounds.x,bounds.y,bounds.w,bounds.h};
        if (d.x+d.w<=0 || d.x>=W) return;
        const bool on=focus && model.metro_focus==slot && model.current()==i;
        if (on && !v1) {
            // The existing Metro V2 focus lift, centred and six pixels lower.
            const int extra_w=std::min(40,d.w/10),extra_h=std::min(24,d.h*9/100);
            d.x-=extra_w/2;d.y-=extra_h/2-6;d.w+=extra_w;d.h+=extra_h;
            shade(d.x-4,d.y+6,d.w+8,d.h,0x000000,34);
            shade(d.x+4,d.y+d.h,d.w-8,9,0x000000,70);
        }
        const dash::Card card=model.card(i);
        if (v1) gradient(d.x,d.y,d.w,d.h,0x87d50e,0x72bc00);else rect(d.x,d.y,d.w,d.h,0x10800d);
        if (on && v1) outline(d.x-5,d.y-5,d.w+10,d.h+10,5,0xf8fff0);
        if (SDL_Surface *art=card_picture(card)) blit_contained(art,{d.x+(d.w-144)/2,d.y+64,144,144});
        text(fit(card.title,regular,d.w-28),d.x+14,d.y+d.h-48,regular,{255,255,255,255});
    };
    for (int i=first;i<std::min(first+8,model.count());++i) if (!focus || i!=model.current()) draw(i);
    if (focus) draw(model.current());
}

dash::Card metro_music_content(dash::Card card,int index) {
    if(model.channel!=dash::Media||model.metro_tab!=5||music_songs.empty())return card;
    std::vector<std::string> paths;paths.reserve(music_songs.size());
    for(const auto&song:music_songs)paths.push_back(song.path);
    const auto most=music_activity.most_played(paths);
    const auto recent=music_activity.recently_added(paths);
    const auto albums=tunes::groups(1);
    const std::uint64_t day=std::uint64_t(std::max<std::time_t>(0,std::time(nullptr)))/86400u;
    int song_index=0;
    auto valid=[&](int value){return value>=0&&value<int(music_songs.size());};
    switch(index) {
    case 1:
        if(!albums.empty()&&!albums[std::size_t(day%albums.size())].songs.empty()){
            const auto&album=albums[std::size_t(day%albums.size())];song_index=album.songs[0];
            card.subtitle=album.name+(album.detail.empty()?std::string{}:" • "+album.detail);
        }
        break;
    case 2:
        if(!most.empty()&&valid(most[0]))song_index=most[0];
        break;
    case 4:
        if(!recent.empty()&&valid(recent[0]))song_index=recent[0];
        break;
    case 5:
        if(!albums.empty()&&!albums[std::size_t(day%albums.size())].songs.empty())song_index=albums[std::size_t(day%albums.size())].songs[0];
        card.subtitle=std::to_string(albums.size())+(albums.size()==1?" album":" albums");
        break;
    case 6:
        if(const music::Song*playing=music_player.current_song()){
            card.subtitle=playing->title+(playing->artist.empty()?std::string{}:" • "+playing->artist);
            card.image="music:"+playing->path;return card;
        }
        card.subtitle="Choose something to play";
        break;
    default: break;
    }
    if(!valid(song_index))song_index=0;
    const auto&song=music_songs[std::size_t(song_index)];
    if(card.subtitle.empty()){
        if(index==0)card.subtitle=std::to_string(music_songs.size())+(music_songs.size()==1?" song":" songs")+" • "+song.title;
        else if(index==3)card.subtitle="Your saved mixes • "+song.title;
        else card.subtitle=song.title+(song.artist.empty()?std::string{}:" • "+song.artist);
    }
    card.image="music:"+song.path;
    return card;
}
void draw_metro_hub() {
    gpu_home::begin();
    TTF_Font *body=metro_tabs_font?metro_tabs_font:(era_text?era_text:regular);
    const bool v1=dash::dashboard_family(model.era,model.dashboard_build)==3,classic_home=model.metro_v1_home();
    const bool social_page=model.metro_tab==2;
    const auto &tiles=classic_home?dash::kMetroV1HomeTiles:v1?dash::kMetroV1Tiles:dash::kMetroTiles;
    gradient(0,0,W,H,v1?0x242625:0x505158,v1?0xa1a69e:0xb7b9bb);
    if (era_background) {SDL_Rect backdrop{0,0,W,H};blit_quality(era_background,nullptr,screen,&backdrop);}
    draw_installed_theme_background();
    if (!v1) {
        // AuraScene's TopDark and Vignette over aura_background.jpg: fitted to
        // the 2012 capture, a vertical darkening strongest at the top and foot.
        static SDL_Surface *aura_shade=nullptr;
        if (!aura_shade && (aura_shade=render_density::create(4,H,SDL_PIXELFORMAT_ARGB8888))) {
            constexpr float stops[][2]={{0,.33f},{120,.29f},{240,.23f},{300,.21f},{500,.13f},{850,.08f},{900,.09f},{940,.12f},{980,.18f},{1020,.24f},{1060,.31f},{1080,.34f}};
            for (int y=0;y<aura_shade->h;y++) {
                const float logical_y=float(y)/render_density::density(aura_shade);
                float a=stops[11][1];
                for (int i=1;i<12;i++) if (logical_y<=stops[i][0]) {
                    const float t=(logical_y-stops[i-1][0])/(stops[i][0]-stops[i-1][0]);a=stops[i-1][1]*(1-t)+stops[i][1]*t;break;
                }
                auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(aura_shade->pixels)+y*aura_shade->pitch);
                for (int x=0;x<aura_shade->w;x++) row[x]=(Uint32(a*255+0.5f)<<24)|0x0c0b12u;
            }
            SDL_SetSurfaceBlendMode(aura_shade,SDL_BLENDMODE_BLEND);
            surface_generations::seal(aura_shade);
        }
        if (aura_shade) {
            // Preserve the original immutable rows in one GPU dispatch.
            const bool gpu_strip=x21hook::vertical_strip(aura_shade,screen);
            if(!gpu_strip||x21hook::audit_cpu()){x21hook::Pause reference(gpu_strip);
            // Each source row is a constant colour/alpha; avoid a full-screen scaler.
            if(solid_blend::format_ok(screen)&&solid_blend::compatible(screen->format->format)) {
                for(int y=0;y<aura_shade->h;++y){const Uint32 p=*reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(aura_shade->pixels)+y*aura_shade->pitch);physical_shade(0,y,screen->w,1,p&0xffffff,Uint8(p>>24));}
            } else {SDL_Rect all{0,0,W,H};SDL_BlitScaled(aura_shade,nullptr,screen,&all);}
            }
        }
    }
    if (era_floor && !social_page) {
        SDL_SetSurfaceAlphaMod(era_floor,120);
        SDL_Rect shadow{250,v1?794:875,1410,96};
        blit_quality(era_floor,nullptr,screen,&shadow);
    }
    // The 2011-12 corner (designer games capture): the gamerpic, the gamertag
    // to its left, and friends online / messages under the tag.
    draw_gamerpic(social_page?1776:1692,45,social_page?48:82,social_page?48:82);
    const SDL_Color active_ink=era_light_navigation?SDL_Color{37,44,41,255}:SDL_Color{255,255,255,255};
    const SDL_Color idle_ink=era_light_navigation?SDL_Color{91,98,95,255}:SDL_Color{192,196,200,255};
    const SDL_Color footer_ink=era_dark_footer?SDL_Color{235,239,236,255}:SDL_Color{55,65,62,255};
    const int profile_right=social_page?1758:1675;
    text(fit(model.player,regular,360),profile_right-text_width(fit(model.player,regular,360),regular),48,regular,social_page?SDL_Color{245,247,244,255}:idle_ink);
    if(!social_page) {
        int x=1676;
        auto counter=[&](const char *glyph,int count) {
            const std::string n=std::to_string(count);
            x-=text_width(n,regular);text(n,x,84,regular,idle_ink);x-=12;
            auto it=blades_system_icons.find(glyph);
            if (it!=blades_system_icons.end()) {
                SDL_SetSurfaceColorMod(it->second,idle_ink.r,idle_ink.g,idle_ink.b);
                x-=24;SDL_Rect r{x,92,24,24};blit_quality(it->second,nullptr,screen,&r);
                SDL_SetSurfaceColorMod(it->second,255,255,255);
            }
            x-=30;
        };
        counter("kinect/ico_32x_Mail",0);    // messages
        counter("kinect/ico_32x_Friend",0);  // friends online
    }
    // The pivot header scrolls: the current tab sits at x 322 (designer apps
    // capture) unless that would pull "bing" right of its rest at 112.
    int tx=112;
    {
        int offset=0;
        for (int i=0;i<model.metro_tab && i<8;i++) offset+=text_width(dash::kMetroTabs[i],body)+51;
        tx=std::min(117,322-offset);
    }
    for (int i=0;i<8;i++) {
        const bool active=i==model.metro_tab;
        const int width=text_width(dash::kMetroTabs[i],body);
        text(dash::kMetroTabs[i],tx,158,body,active?active_ink:idle_ink);
        if (active && model.metro_focus==-1) rect(tx,223,width,3,era_light_navigation?0x252c29:0xffffff);
        tx+=width+51;
    }
    if (model.metro_tab==7) {draw_metro_settings_tiles(0,true);return;}
    rect(0,516,90,91,0x393939);   // the search square, as the 2012 capture
    if (metro_symbols[1]) blit_contained(metro_symbols[1],{17,536,49,49});
    const int selected=model.current();
    const int page=selected/model.metro_page_size();
    if (classic_home) {
        const auto &q=tiles[dash::kMetroPins];
        gradient(q.x,q.y,q.w,q.h,0x9de919,0x77c400);
        for (int row=0;row<4;row++) for (int col=0;col<4;col++)
            shade(q.x+col*q.w/4,q.y+row*(q.h-48)/4,q.w/4,(q.h-48)/4,(row+col)%3==0?0xcaf652:0x51a900,45);
        text("Quickplay",q.x+18,q.y+q.h-48,regular,{255,255,255,255});
    } else if (!v1 && !social_page) {
        for (int i=0;i<2;i++) {
            const auto &r=tiles[dash::kMetroPins+i];rect(r.x,r.y,r.w,r.h,0x10800d);
            SDL_Surface *symbol=i?(era_recent?era_recent:metro_symbols[2]):(era_pin?era_pin:metro_symbols[0]);
            if (symbol) blit_contained(symbol,{r.x+(r.w-96)/2,r.y+32,96,96});
            text(i?"Recent":"My Pins",r.x+18,r.y+r.h-48,regular,{255,255,255,255});
        }
    }
    // Focus: V1 rings the tile in white; V2 (2012 capture) lifts it instead,
    // about 10% (at most 40 x 24 px) about its centre, 6 px lower, over a shadow.
    auto lifted=[](const SDL_Rect &d) {
        const int w=d.w+std::min(40,d.w/10),h=d.h+std::min(24,d.h*9/100);
        return SDL_Rect{d.x+(d.w-w)/2,d.y+(d.h-h)/2+6,w,h};
    };
    auto lift_shadow=[](const SDL_Rect &z) {
        shade(z.x-4,z.y+6,z.w+8,z.h,0x000000,34);
        shade(z.x+4,z.y+z.h,z.w-8,9,0x000000,70);
    };
    auto pin_tile=[&](int i,const SDL_Rect &d) {
        rect(d.x,d.y,d.w,d.h,0x10800d);
        SDL_Surface *symbol=i?(era_recent?era_recent:metro_symbols[2]):(era_pin?era_pin:metro_symbols[0]);
        const int side=d.w*96/261;
        if (symbol) blit_contained(symbol,{d.x+(d.w-side)/2,d.y+d.h*32/195,side,side});
        text(i?"Recent":"My Pins",d.x+18,d.y+d.h-48,regular,{255,255,255,255});
    };
    for (int node:{dash::kMetroPins,dash::kMetroRecent}) if (model.metro_focus==node && v1) {
        const auto &r=tiles[node];
        outline(r.x-5,r.y-5,r.w+10,r.h+10,5,0xf8fff0);
    }
    auto draw_tile=[&](dash::Card card,int index,const SDL_Rect &d,bool focused,bool current) {
        card=metro_music_content(std::move(card),index);
        if (v1) gradient(d.x,d.y,d.w,d.h,current?0xa1e91c:0x87d50e,current?0x75b700:0x72bc00);
        else rect(d.x,d.y,d.w,d.h,card.promo>=0?0x151a18:0x10800d);
        if (focused && v1) outline(d.x-5,d.y-5,d.w+10,d.h+10,5,0xf8fff0);
        const bool ad_caption=card.promo>=0 && promotion_art::has_caption(card);
        const int caption=card.promo>=0?(ad_caption?(d.w>=600?88:53):0):card.subtitle.empty()?53:88;
        if (focused && hub_media_fade(SDL_GetTicks()).video>0)
            draw_video(d.x,d.y,d.w,d.h,hub_media_fade(SDL_GetTicks()).video);
        else if (card.promo>=0) {
            promotion_art::draw(card.image,d,caption?SDL_Rect{d.x,d.y+d.h-caption,d.w,caption}:SDL_Rect{},index);
        } else metro_art(card,d);
        if (card.promo>=0) {if (caption) shade(d.x,d.y+d.h-caption,d.w,caption,0x000000,185);}
        else if (v1) gradient(d.x,d.y+d.h-caption,d.w,caption,0x69a900,0x568c05);
        else rect(d.x,d.y+d.h-caption,d.w,caption,card.image.rfind("music:",0)==0?0x17221b:0x10800d);
        if (card.promo<0 || ad_caption) text(fit(card.title,regular,d.w-28),d.x+14,d.y+d.h-caption+5,regular,{255,255,255,255});
        if (caption>53) text(fit(card.subtitle,small,d.w-28),d.x+14,d.y+d.h-37,small,{238,240,239,255});
    };
    // Neighbouring tabs peek in at the screen edges, one page pitch (1490 px)
    // either side, as in both designer captures.
    {
        constexpr int kPitch=1490;
        constexpr int channels[]={dash::MyXbox,dash::MyXbox,dash::Friends,dash::Games,dash::Media,dash::Media,dash::Apps,dash::MyXbox};
        const int saved_tab=model.metro_tab,saved_channel=model.channel,saved_focus=model.metro_focus;
        for (int side:{-1,1}) {
            const int tab=saved_tab+side;
            if (tab<0 || tab>7) continue;
            model.metro_tab=tab;model.channel=channels[tab];model.metro_focus=-2;
            const bool peek_v1_home=model.metro_v1_home();
            const auto &peek_tiles=peek_v1_home?dash::kMetroV1HomeTiles:v1?dash::kMetroV1Tiles:dash::kMetroTiles;
            if (tab==7) draw_metro_settings_tiles(side*kPitch,false);
            const int n=tab==7?0:std::min(model.count(),model.metro_page_size());
            for (int slot=0;slot<n;slot++) {
                const auto &b=peek_tiles[slot];
                if (b.w<=0) continue;
                const SDL_Rect d{b.x+side*kPitch,b.y,b.w,b.h};
                if (d.x+d.w<=0 || d.x>=W) continue;
                draw_tile(model.card(slot),slot,d,false,false);
            }
            if (!v1) for (int i=0;i<2;i++) {
                const auto &r=peek_tiles[dash::kMetroPins+i];
                const SDL_Rect d{r.x+side*kPitch,r.y,r.w,r.h};
                if (d.x+d.w<=0 || d.x>=W) continue;
                rect(d.x,d.y,d.w,d.h,0x10800d);
                SDL_Surface *symbol=i?(era_recent?era_recent:metro_symbols[2]):(era_pin?era_pin:metro_symbols[0]);
                if (symbol) blit_contained(symbol,{d.x+(d.w-96)/2,d.y+32,96,96});
                text(i?"Recent":"My Pins",d.x+18,d.y+d.h-48,regular,{255,255,255,255});
            }
        }
        model.metro_tab=saved_tab;model.channel=saved_channel;model.metro_focus=saved_focus;
    }
    if (social_page) {
        // Retail Metro Social used a narrow three-row action strip and left the
        // rest of the stage open for the signed-in avatar and friends.  It was
        // not another Template5 tile wall.
        constexpr int x=252,y=307,w=280,h=137,gap=5;
        for(int slot=0;slot<model.count();++slot) {
            SDL_Rect d{x,y+slot*(h+gap),w,h};
            const bool focused=model.metro_focus==slot;
            if(focused&&!v1){d=lifted(d);lift_shadow(d);}
            // Use the same palette as every other tile in this dashboard
            // family.  Social must not look pasted in with a second green.
            if(v1)gradient(d.x,d.y,d.w,d.h,focused?0xa1e91c:0x87d50e,focused?0x75b700:0x72bc00);
            else rect(d.x,d.y,d.w,d.h,0x10800d);
            if(focused&&v1) outline(d.x-5,d.y-5,d.w+10,d.h+10,5,0xf8fff0);
            const dash::Card card=model.card(slot);
            SDL_Surface *symbol=card_picture(card);
            if(symbol) blit_contained(symbol,{d.x+18,d.y+22,48,48});
            text(fit(card.title,regular,d.w-30),d.x+15,d.y+d.h-46,regular,{255,255,255,255});
        }
        // The gamercard belongs beside the actions, independent of tile focus.
        const auto profile_name=fit(model.player,regular,260);
        text(profile_name,571,261,regular,{0,0,0,80});
        text(profile_name,570,260,regular,{246,248,244,255});
        if(trophies.known){
            const auto points=std::to_string(trophies.points());
            const int width=text(points,570,307,regular,{237,241,232,255});
            if(gamerscore_icon)blit_contained(gamerscore_icon,{580+width,317,28,28});
        }
        // Do not invent a nine-icon achievement history or substitute a large
        // gamerpic when per-achievement artwork is unavailable.
    } else for (int slot=0;slot<model.metro_page_size();slot++) {
        const int index=page*model.metro_page_size()+slot;
        if (index>=model.count()) break;
        const dash::Card card=model.card(index);
        const auto &bounds=tiles[slot];
        const SDL_Rect d{bounds.x,bounds.y,bounds.w,bounds.h};
        const bool focused=index==selected && model.metro_focus==slot;
        if (focused && !v1) continue;   // drawn lifted, last
        draw_tile(card,index,d,focused,index==selected);
    }
    if (!v1 && !social_page) {
        const int slot=model.metro_focus;
        const int index=page*model.metro_page_size()+slot;
        if (slot>=0 && slot<model.metro_page_size() && index==selected && index<model.count()) {
            const auto &b=tiles[slot];
            const SDL_Rect z=lifted({b.x,b.y,b.w,b.h});
            lift_shadow(z);
            draw_tile(model.card(index),index,z,true,true);
        } else if (slot==dash::kMetroPins || slot==dash::kMetroRecent) {
            const auto &b=tiles[slot];
            const SDL_Rect z=lifted({b.x,b.y,b.w,b.h});
            lift_shadow(z);
            pin_tile(slot==dash::kMetroRecent?1:0,z);
        }
    }
    if (classic_home) {
        const auto &hero=tiles[0];
        for (int i=0;i<5;i++) ring(hero.x+10+i*18,hero.y+hero.h+15,i==0?6:3,i==0?3:2,0xe5e9df);
        text("ADVERTISEMENT",tiles[3].x,tiles[3].y+tiles[3].h+10,small,era_light_navigation?active_ink:SDL_Color{227,231,224,255});
    }
    // Keep the Home shortcut, but do not miniaturize the live avatar into it.
    // The original Metro dashboard presented the signed-in avatar at full-body
    // scale on Social; this tile is navigation only.
    if(model.metro_avatar_home()) {
        const auto box=dash::metro_avatar_bounds(v1);
        SDL_Rect tile{box.x,box.y,box.w,box.h};
        const bool focused=model.metro_focus==dash::kMetroAvatar;
        if(focused&&!v1){tile=lifted(tile);lift_shadow(tile);}
        rect(tile.x,tile.y,tile.w,tile.h,0x10800d);
        if(focused&&v1)outline(tile.x-5,tile.y-5,tile.w+10,tile.h+10,5,0xf8fff0);
        if (metro_symbols[0]) blit_contained(metro_symbols[0],{tile.x+(tile.w-96)/2,tile.y+24,96,96});
        text("My Avatar",tile.x+14,tile.y+tile.h-48,regular,{255,255,255,255});
    }
    // Metro Social is where the regular 360 dashboard showed avatars: a
    // standing, transparent full-body figure in front of the hub, not a
    // thumbnail trapped inside a tile.  Keep the same accepted owner image /
    // live pose and the same GPU-avatar path used by NXE and Kinect.
    const auto friends_stage=exbox::friends_stage::route(v1?exbox::dashboard_director::Family::MetroV1:exbox::dashboard_director::Family::MetroV2,model.metro_tab==2);
    if (friends_stage.status==exbox::friends_stage::Status::Ready) {
        // Original floor asset has a soft alpha falloff. These are contact
        // shadows, without the former rectangular grey stand-in blocks.
        if(era_floor){
            SDL_SetSurfaceColorMod(era_floor,0,0,0);SDL_SetSurfaceAlphaMod(era_floor,160);
            auto contact=[&](int x,int y,int w,int h){SDL_Rect at{x-w/2,y-h/2,w,h};blit_quality(era_floor,nullptr,screen,&at,false);};
            contact(friends_stage.bounds.x+friends_stage.bounds.w/2,friends_stage.bounds.y+friends_stage.bounds.h-22,170,44);
            if(model.local_friend_avatars)for(int i=0;i<3;++i)contact(1080+i*240,807,130,32);
            SDL_SetSurfaceColorMod(era_floor,255,255,255);SDL_SetSurfaceAlphaMod(era_floor,255);
        }
        if(model.local_friend_avatars) {
            // Stable offline friends rendered from the installed avatar kit.
            // They use real decoded body/clothes/head parts and upload once;
            // no mannequin geometry or invented Xbox Live identities remain.
            for(int i=0;i<3;i++) {
                const bool focused=model.metro_focus==dash::kMetroSocialFriend0+i;
                prepared_avatar::focus(i+1,focused,SDL_GetTicks());
                // Retail Metro puts friends behind the owner: a shared rear
                // floor line and scale, preserving each original body shape.
                const int cx=1080+i*240,top=355,foot=825;
                SDL_Surface* guest=edited_avatar::friend_still(i);
                if(!guest)guest=look3d::local_guest(i);
                if(guest) {
                    SDL_Rect at=edited_avatar::contain(guest,{cx-104,top,208,foot-top});
                    const bool gpu=gpu_home::avatar(guest,at,1.f);
                    if(!gpu||x21hook::audit_cpu()){x21hook::Pause reference(gpu);prepared_avatar::materialize(guest);blit_quality(guest,nullptr,screen,&at,false);}
                }
            }
        }
        prepared_avatar::focus(0,model.metro_focus==dash::kMetroSocialOwner,SDL_GetTicks());
        SDL_Surface* figure=edited_avatar::live_or_still(SDL_GetTicks(),true,avatar_art);
        if(figure) {
            const SDL_Rect stage{friends_stage.bounds.x,friends_stage.bounds.y,friends_stage.bounds.w,friends_stage.bounds.h};
            SDL_Rect at=edited_avatar::contain(figure,stage);
            const bool gpu=gpu_home::avatar(figure,at,1.f);
            if(!gpu||x21hook::audit_cpu()) {x21hook::Pause reference(gpu);
                prepared_avatar::materialize(figure);if(figure==look3d::hub.surface)look3d::materialize_hub();
                blit_quality(figure,nullptr,screen,&at,false);
            }
        }
    }
    // The originals show no "n of N" counter or paging legend under the grid.
    (void)footer_ink;
}
