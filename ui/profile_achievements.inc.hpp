// Included inside main.cpp's private namespace.
// gamer/GamerActionPanelScene, PlayedGamePanelScene; games/402_Achievements (9199).
namespace profile_page {
void background(const char *heading) {
    namespace sk=media_skin;
    if (model.era>0) {
        sk::scene_background();sk::background_panel(120,150,1680,810);
    } else {
        shade(0,0,W,H,0x000000,185);
        if (blade_dark) nine_slice(blade_dark,102,132,1716,846,24,36);
        else rounded(120,150,1680,810,12,0x42505e,0x2c3742);
    }
    text(heading,165,model.era>0?70:175,large,sk::kInk);
}
SDL_Color ink(bool selected=false) {
    return model.era>=2 && !selected?media_skin::kRowSel:media_skin::kInk;
}
void highlight(int x,int y,int w,int h) {
    if (model.era>=2) media_skin::green_highlight(x,y,w,h);
    else if (model.era==1) outline(x,y,w,h,3,media_skin::nxe_green());
    else rounded(x,y,w,h,4,0x8fd23c,0x5da516);
}
void progress(const std::string &key,int x,int y) {
    text("Achievements  "+model.achievement_counts(key),x,y,regular,ink());
    text("Gamerscore  "+model.achievement_counts(key,true),x,y+48,regular,ink());
    const auto p=model.achievement_progress(key);
    if (p.known && p.total>0) {
        rect(x,y+102,550,12,0x56616a);
        rect(x,y+102,550*p.earned/p.total,12,media_skin::green());
        if (p.complete()) text("Complete",x,y+132,regular,ink());
    }
}
void mapping() {
    // Ex-Box addition: no 360 original. Same values as ps5lib::Trophies::points().
    text("Ex-Box mapping: Bronze 15 / Silver 30 / Gold 90 / Platinum 300",165,910,small,ink());
}
}
void draw_profile() {
    namespace p=profile_page;
    p::background("Profile");
    draw_gamerpic(195,285,144,144);
    text(fit(model.player,large,650),195,460,large,p::ink());
    const auto rows=model.profile_entries();
    for (int i=0;i<int(rows.size());++i) {
        const int y=280+i*108;
        if (i==model.viewer_item) p::highlight(940,y,750,90);
        text(rows[std::size_t(i)].label,970,y+22,regular,p::ink(i==model.viewer_item));
    }
}
void draw_achievements() {
    namespace p=profile_page;
    p::background("Achievements");
    const auto games=model.achievement_games();
    const bool listing=model.achievement_game.empty();
    const int n=model.viewer_count(),item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const int top=std::clamp(item-2,0,std::max(0,n-6));
    if (listing) {
        text("Played Games",165,250,regular,p::ink());
        if (n==0) system_page::paragraphs(dash::kAchievementsEmpty,195,390,1450,regular,p::ink(),48);
        for (int i=top;i<std::min(n,top+6);++i) {
            const auto &game=games[std::size_t(i)];
            const int y=320+(i-top)*85;
            if (i==item) p::highlight(165,y,870,78);
            if (SDL_Surface *art=title_icon(game.image)) blit_contained(art,{180,y+7,64,64});
            text(fit(game.title,regular,730),265,y+18,regular,p::ink(i==item));
        }
        if (n>0) {
            const auto &game=games[std::size_t(item)];
            text("Achievement Progress",1100,320,regular,p::ink());
            p::progress(game.key,1100,400);
            if (!model.achievement_title(game.key))
                system_page::paragraphs(dash::kAchievementsEmpty,1100,650,590,small,p::ink(),36);
        }
    } else {
        std::string title=model.achievement_game;
        for (const auto &game:games) if (game.key==model.achievement_game) {title=game.title;break;}
        text(fit(title,regular,1500),165,250,regular,p::ink());
        const auto *source=model.achievement_title(model.achievement_game);
        const auto progress=model.achievement_progress(model.achievement_game);
        if (source) text(std::to_string(progress.earned)+" of "+std::to_string(progress.total)+" Unlocked",1100,320,regular,p::ink());
        p::progress(model.achievement_game,1100,390);
        if (n==0) system_page::paragraphs(dash::kAchievementsEmpty,195,400,800,regular,p::ink(),48);
        if (source) for (int i=top;i<std::min(n,top+6);++i) {
            const auto &a=source->entries[std::size_t(i)];
            const int y=320+(i-top)*85;
            if (i==item) p::highlight(165,y,870,78);
            if (achievement_art[a.art()]) blit_contained(achievement_art[a.art()],{180,y+7,64,64});
            // shared/LiveAll.xus: Secret; secret description below.
            text(fit(a.display_name(),regular,730),265,y+18,regular,p::ink(i==item));
        }
        if (source && n>0) {
            const auto &a=source->entries[std::size_t(item)];
            text(std::to_string(dash::trophy_points(a.grade))+" G",1100,590,regular,p::ink());
            if (!a.display_date().empty()) text(fit(a.display_date(),small,590),1100,640,small,p::ink());
            system_page::paragraphs(fit(a.display_description(),small,2200),
                                    1100,700,590,small,p::ink(),36);
        }
    }
    if (n>0) text(std::to_string(item+1)+" / "+std::to_string(n),165,850,small,p::ink());
    p::mapping();
}
