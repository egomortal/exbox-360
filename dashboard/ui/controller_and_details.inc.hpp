// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_controller_map(Uint32 now) {
    shade(0,0,W,H,0x000000,200);
    constexpr int px=150, py=90, pw=1620, ph=880;
    gradient(px,py,pw,ph,0x262a25,0x121412);
    gradient(px,py,pw,8,0x8bd336,0x4f9a16);
    outline(px,py,pw,ph,2,0x5d6659);
    text("Controller Map",px+60,py+42,large,{255,255,255,255});
    text("Press any button to test your controller and its Xbox button labels.",px+62,py+112,regular,{190,214,170,255});
    constexpr int col_w=760, row_h=78;
    const int rows=int(sizeof map_rows/sizeof map_rows[0]);
    for (int i=0;i<rows;i++) {
        const MapRow &r=map_rows[i];
        int x=px+50+(i/8)*(col_w+20), y=py+180+(i%8)*row_h;
        bool on=lit(r.button,now);
        if (on) gradient(x,y,col_w,row_h-10,0x7fca2a,0x427f12);
        else shade(x,y,col_w,row_h-10,0xffffff,14);
        if (r.button==xb::Up) {
            SDL_Surface *pad=button_image(xb::Up);
            if (pad) {SDL_Rect d{x+14,y+8,52,52};blit_quality(pad,nullptr,screen,&d);}
        } else if (r.button==xb::None) {
            SDL_Surface *stick_glyph=button_image(xb::LS);
            if (stick_glyph) {SDL_Rect d{x+14,y+8,52,52};blit_quality(stick_glyph,nullptr,screen,&d);}
        } else glyph(r.button,x+14,y+8,52);
        SDL_Color white{255,255,255,255}, dim{176,186,170,255};
        text(r.xbox,x+82,y+8,semibold?semibold:regular,white);
        text(std::string("PS5: ")+r.ps5,x+82,y+38,small,on?white:dim);
        const char *action=r.action;
        if (r.button==xb::LB && (model.era==0 || model.era==3)) action=model.era==0?"Previous blade":"Previous tab";
        if (r.button==xb::RB && (model.era==0 || model.era==3)) action=model.era==0?"Next blade":"Next tab";
        int aw=text_width(action,small);
        text(action,x+col_w-aw-18,y+22,small,r.button==xb::Back?SDL_Color{230,160,120,255}:(on?white:dim));
    }
    // Ex-Box addition: no 360 original; the retail Guide legend stays source-authored.
    text("Last input: "+last_input,px+60,py+ph-74,small,{200,226,176,255});
    text("L3 + R3 returns to PS5 Home.",px+1050,py+ph-74,small,{150,160,146,255});
}
// Details: the title large on the left (its trailer when it has one), what
// it is beneath, and what can be done with it on the right.
void draw_info() {
    constexpr int px=300, py=190, pw=1320, ph=680;
    shade(0,0,W,H,0x000000,170);
    if (blade_dark) nine_slice(blade_dark,px-18,py-18,pw+36,ph+36,24,36);
    else rounded(px,py,pw,ph,12,0x42505e,0x2c3742);
    const dash::Card c=model.info_card();
    // Media box.
    constexpr int mx=350, my=250, mw=560, mh=420;
    shade(mx,my,mw,mh,0x000000,90);
    const Uint32 now=SDL_GetTicks();
    const MediaFade fade=model.info?media_fade(now):MediaFade{};
    if (fade.video>0 || fade.spin>=1) {
        // The disc, then the trailer over it.
    } else if (SDL_Surface *own=title_icon(c.image)) {
        constexpr int longest=380;
        const int aw=std::max(1,own->w*longest/std::max(own->w,own->h));
        const int ah=std::max(1,own->h*longest/std::max(own->w,own->h));
        SDL_Rect r{mx+(mw-aw)/2,my+(mh-ah)/2,aw,ah};
        rect(r.x-3,r.y-3,aw+6,ah+6,0xf2f8ea);
        blit_quality(own,nullptr,screen,&r);
    } else if (c.icon>=0 && c.icon<dash::IconCount && card_icons[c.icon]) {
        SDL_Rect r{mx+(mw-260)/2,my+(mh-260)/2,260,260};
        blit_contained(card_icons[c.icon],r);
    }
    if (fade.video<1) draw_spin(mx+mw/2,my+mh/2,300,now,fade.spin);
    if (fade.video>0) {
        const int vh=mw*270/480;
        draw_video(mx,my+(mh-vh)/2,mw,vh,fade.video);
    }
    text(fit(c.title,large,mw),mx,my+mh+16,large,{255,255,255,255});
    std::string what;
    if (c.kind==dash::Kind::Game) {
        const std::string &id=model.games[size_t(c.game)].title_id;
        what=(id.rfind("CUSA",0)==0?"PS4 game  ·  ":"PS5 game  ·  ")+id;
    } else if (c.kind==dash::Kind::App) {
        const std::string &exe=model.apps[size_t(c.app)].executable;
        what="Homebrew  ·  "+exe.substr(0,exe.rfind('/'));
    } else what="Not installed now";
    text(fit(what,small,mw),mx,my+mh+80,small,{196,208,216,255});
    std::string where=model.pinned(model.info_target.key)?"Attached to My Xbox":"Not attached";
    if (c.kind==dash::Kind::App) where+=model.game_apps.count(model.info_target.key)?"  ·  In Games":"  ·  In Apps";
    text(where,mx,my+mh+112,small,{150,214,82,255});
    // Actions.
    constexpr int lx=960, lw=600;
    text("Details",lx,py+40,semibold?semibold:regular,{150,214,82,255});
    const auto entries=model.info_entries();
    const int item=std::clamp(model.info_item,0,std::max(0,int(entries.size())-1));
    TTF_Font *font=guide::item_font?guide::item_font:regular;
    const int row_h=std::min(66,560/std::max(1,int(entries.size())));
    for (size_t i=0;i<entries.size();i++) {
        const int y=py+100+int(i)*row_h;
        if (int(i)==item) {
            rounded(lx-2,y-2,lw+4,row_h,4,0x4e8e17,0x3b7410);
            rounded(lx,y,lw,row_h-4,3,0x8fd23c,0x5da516);
            rect(lx+3,y+1,lw-6,2,0xb9ea7c);
        }
        const bool artwork_picker=entries[i].action==dash::InfoAction::CycleArtwork &&
            c.kind==dash::Kind::Game && !model.games[size_t(c.game)].xbox_cover.empty();
        const bool picker=int(i)==item &&
            (entries[i].action==dash::InfoAction::CycleTrailer || artwork_picker);
        const std::string label=fit(entries[i].label,font,picker?lw-96:lw-28);
        text(label,lx+16,y+9,font,{0,0,0,110});
        text(label,lx+14,y+7,font,{255,255,255,255});
        if (picker) {
            // Left / Right step through the trailers.
            text("‹",lx+lw-70,y+7,font,{255,255,255,255});
            text("›",lx+lw-34,y+7,font,{255,255,255,255});
        }
    }
}
