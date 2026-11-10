// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
std::string clock_text() {
    std::time_t now=std::time(nullptr);
    std::tm snapshot{};
    if (!local_time_snapshot(now,snapshot)) return "";
    const std::tm *local=&snapshot;
    char buffer[32];
    int hour=local->tm_hour%12; if (hour==0) hour=12;
    if (model.clock_24_hour) std::snprintf(buffer,sizeof buffer,"%02d:%02d",local->tm_hour,local->tm_min);
    else std::snprintf(buffer,sizeof buffer,"%d:%02d %s",hour,local->tm_min,local->tm_hour<12?"AM":"PM");
    return buffer;
}
// The profile's picture in a thin dark frame: a classic gamerpic, or the
// avatar's head when no gamerpics are installed.
void draw_gamerpic(int x, int y, int w, int h) {
    rect(x-2,y-2,w+4,h+4,0x0c1018);
    if (edited_avatar::gamerpic) {SDL_Rect at{x,y,w,h};blit_quality(edited_avatar::gamerpic,nullptr,screen,&at,false);return;}
    if (!gamerpics.empty()) {
        SDL_Rect destination{x,y,w,h};
        blit_quality(gamerpics[size_t(gamerpic)%gamerpics.size()],nullptr,screen,&destination);
        return;
    }
    gradient(x,y,w,h,0xa9d86a,0x4e8c16);
    if (avatar_art) {
        const int crop_w=avatar_art->w*27/100;
        SDL_Rect source{avatar_art->w*355/1000,avatar_art->h*335/1000,crop_w,crop_w*h/w};
        SDL_Rect destination{x,y,w,h};
        blit_quality(avatar_art,&source,screen,&destination);
    }
}
void next_gamerpic() {
    if (gamerpics.empty()) return;
    gamerpic=(gamerpic+1)%int(gamerpics.size());
    if (FILE *f=fopen(gamerpic_path,"w")) {fprintf(f,"%d\n",gamerpic); fclose(f);}
}
// The gamercard at the hub's top right, as NXE showed the signed-in profile.
void draw_gamercard() {
    if (model.era==1) {
        // NXE showed only the signed-in profiles' gamerpics here, left to right,
        // the one in control framed in pale gold (designer capture: 1384,112).
        draw_gamerpic(1386,114,98,98);   // its dark keyline sits under the frame
        for (int i=0;i<4;i++) {          // a 4 px pale-gold frame, corners softened
            const unsigned c=i==3?0xc8c070:0xeae286;
            rect(1384+i,112+i,102-2*i,1,c);rect(1384+i,213-i,102-2*i,1,c);
            rect(1384+i,112+i,1,102-2*i,c);rect(1485-i,112+i,1,102-2*i,c);
        }
        return;
    }
    constexpr int pic=96, right=1776, top=96;
    draw_gamerpic(right-pic,top,pic,pic);
    const std::string name=fit(model.player,large,420);
    text(name,right-pic-18-text_width(name,large),top-6,large,{246,250,240,255});
    int x=right-pic-18;
    if (trophies.known) {
        // The PS5's trophy points stand where the gamerscore stood.
        if (trophy_icon) {x-=32; SDL_Rect r{x,top+56,32,32}; blit_quality(trophy_icon,nullptr,screen,&r); x-=8;}
        const std::string points=std::to_string(trophies.points());
        text(points,x-text_width(points,large),top+44,large,{246,250,240,255});
        return;
    }
    if (gamerscore_icon) {x-=30; SDL_Rect r{x,top+60,28,28}; blit_quality(gamerscore_icon,nullptr,screen,&r); x-=8;}
    else {x-=text_width("G",regular); text("G",x,top+56,regular,{246,250,240,255}); x-=8;}
    text("0",x-text_width("0",large),top+44,large,{246,250,240,255});
}
