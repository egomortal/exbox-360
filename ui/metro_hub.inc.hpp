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
void draw_metro_hub() {
    TTF_Font *body=metro_tabs_font?metro_tabs_font:(era_text?era_text:regular);
    const bool v1=dash::dashboard_family(model.era,model.dashboard_build)==3,classic_home=model.metro_v1_home();
    const auto &tiles=classic_home?dash::kMetroV1HomeTiles:v1?dash::kMetroV1Tiles:dash::kMetroTiles;
    gradient(0,0,W,H,v1?0x242625:0x505158,v1?0xa1a69e:0xb7b9bb);
    if (era_background) {SDL_Rect backdrop{0,0,W,H};blit_quality(era_background,nullptr,screen,&backdrop);}
    if (!v1) {
        // AuraScene's TopDark and Vignette over aura_background.jpg: fitted to
        // the 2012 capture, a vertical darkening strongest at the top and foot.
        static SDL_Surface *aura_shade=nullptr;
        if (!aura_shade && (aura_shade=SDL_CreateRGBSurfaceWithFormat(0,4,H,32,SDL_PIXELFORMAT_ARGB8888))) {
            constexpr float stops[][2]={{0,.33f},{120,.29f},{240,.23f},{300,.21f},{500,.13f},{850,.08f},{900,.09f},{940,.12f},{980,.18f},{1020,.24f},{1060,.31f},{1080,.34f}};
            for (int y=0;y<H;y++) {
                float a=stops[11][1];
                for (int i=1;i<12;i++) if (y<=stops[i][0]) {
                    const float t=(y-stops[i-1][0])/(stops[i][0]-stops[i-1][0]);a=stops[i-1][1]*(1-t)+stops[i][1]*t;break;
                }
                auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(aura_shade->pixels)+y*aura_shade->pitch);
                for (int x=0;x<4;x++) row[x]=(Uint32(a*255+0.5f)<<24)|0x0c0b12u;
            }
            SDL_SetSurfaceBlendMode(aura_shade,SDL_BLENDMODE_BLEND);
        }
        if (aura_shade) {SDL_Rect all{0,0,W,H};SDL_BlitScaled(aura_shade,nullptr,screen,&all);}
    }
    if (era_floor) {
        SDL_SetSurfaceAlphaMod(era_floor,120);
        SDL_Rect shadow{250,v1?794:875,1410,96};
        blit_quality(era_floor,nullptr,screen,&shadow);
    }
    // The 2011-12 corner (designer games capture): the gamerpic, the gamertag
    // to its left, and friends online / messages under the tag.
    draw_gamerpic(1692,45,82,82);
    const SDL_Color active_ink=era_light_navigation?SDL_Color{37,44,41,255}:SDL_Color{255,255,255,255};
    const SDL_Color idle_ink=era_light_navigation?SDL_Color{91,98,95,255}:SDL_Color{192,196,200,255};
    const SDL_Color footer_ink=era_dark_footer?SDL_Color{235,239,236,255}:SDL_Color{55,65,62,255};
    text(fit(model.player,regular,360),1675-text_width(fit(model.player,regular,360),regular),48,regular,idle_ink);
    {
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
    } else if (!v1) {
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
    auto draw_tile=[&](const dash::Card &card,int index,const SDL_Rect &d,bool focused,bool current) {
        if (v1) gradient(d.x,d.y,d.w,d.h,current?0xa1e91c:0x87d50e,current?0x75b700:0x72bc00);
        else rect(d.x,d.y,d.w,d.h,card.promo>=0?0x151a18:0x10800d);
        if (focused && v1) outline(d.x-5,d.y-5,d.w+10,d.h+10,5,0xf8fff0);
        const bool ad_caption=card.promo>=0 && promotion_art::has_caption(card);
        const int caption=card.promo>=0?(ad_caption?(d.w>=600?88:53):0):53;
        if (focused && hub_media_fade(SDL_GetTicks()).video>0)
            draw_video(d.x,d.y,d.w,d.h,hub_media_fade(SDL_GetTicks()).video);
        else if (card.promo>=0) {
            promotion_art::draw(card.image,d,caption?SDL_Rect{d.x,d.y+d.h-caption,d.w,caption}:SDL_Rect{},index);
        } else metro_art(card,d);
        if (card.promo>=0) {if (caption) shade(d.x,d.y+d.h-caption,d.w,caption,0x000000,185);}
        else if (v1) gradient(d.x,d.y+d.h-53,d.w,53,0x69a900,0x568c05);
        else rect(d.x,d.y+d.h-53,d.w,53,card.promo>=0?0x202522:0x10800d);
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
    for (int slot=0;slot<model.metro_page_size();slot++) {
        const int index=page*model.metro_page_size()+slot;
        if (index>=model.count()) break;
        const dash::Card card=model.card(index);
        const auto &bounds=tiles[slot];
        const SDL_Rect d{bounds.x,bounds.y,bounds.w,bounds.h};
        const bool focused=index==selected && model.metro_focus==slot;
        if (focused && !v1) continue;   // drawn lifted, last
        draw_tile(card,index,d,focused,index==selected);
    }
    if (!v1) {
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
    // The originals show no "n of N" counter or paging legend under the grid.
    (void)footer_ink;
}
