// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void era_art(const dash::Card &card, int x, int y, int w, int h) {
    SDL_Surface *art=card_picture(card);
    if (!art && model.era>=2 && card.icon>=0 && card.icon<dash::IconCount) art=era_icons[card.icon];
    if (!art && card.icon>=0 && card.icon<dash::IconCount) art=card_icons[card.icon];
    if (!art) art=disc_art;
    if (!art) return;
    blit_contained(art,{x,y,w,h});
}
SDL_Rect solid_icon_source(int icon, SDL_Surface *art) {
    // Measured at the last opaque row of each supplied category PNG, plus a
    // few pixels for antialiasing. The remaining rows are its floor reflection.
    static constexpr float end[]={0.653f,0.673f,0.663f,0.653f,0.668f,0.646f,
                                  0.690f,0.690f,0.690f};
    const float fraction=icon>=0 && icon<dash::IconCount?end[icon]:1.0f;
    return {0,0,art->w,std::max(1,int(art->h*fraction))};
}
void metro_art(const dash::Card &card, const SDL_Rect &tile) {
    SDL_Rect area{tile.x,tile.y,tile.w,tile.h-53};
    SDL_Rect previous{};
    SDL_GetClipRect(screen,&previous);
    SDL_SetClipRect(screen,&area);
    if (card.goal==dash::Goal::ToMusic && card.image.empty() && card_picture(card)) {
        // Metro's music tile fills with the album art, cropped to the tile.
        SDL_Surface *art=card_picture(card);
        SDL_Rect full{tile.x,tile.y,tile.w,tile.h};
        SDL_SetClipRect(screen,&full);
        const double k=std::max(double(tile.w)/art->w,double(tile.h)/art->h);
        SDL_Rect to{tile.x+(tile.w-int(art->w*k))/2,tile.y+(tile.h-int(art->h*k))/2,int(art->w*k),int(art->h*k)};
        blit_quality(art,nullptr,screen,&to);
        shade(tile.x,tile.y+tile.h-53,tile.w,53,0x000000,110); // keeps the name legible
    } else if (SDL_Surface *art=card_picture(card)) {
        // Preserve the entire box cover inside this Metro tile's own bounds.
        SDL_Rect inset{area.x+10,area.y+10,area.w-20,area.h-20};
        blit_contained(art,inset);
    } else {
        SDL_Surface *icon=card.icon>=0 && card.icon<dash::IconCount?era_icons[card.icon]:nullptr;
        if (icon) {
            const int size=std::min(144,std::min(area.w,area.h)*56/100);
            blit_contained(icon,{area.x+(area.w-size)/2,area.y+(area.h-size)/2,size,size});
        }
    }
    SDL_SetClipRect(screen,&previous);
}
