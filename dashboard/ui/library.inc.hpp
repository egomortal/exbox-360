// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_library_static() {
    shade(0,0,W,H,0x000000,185);
    rounded(120,90,1680,895,12,0x42505e,0x2c3742);
    const char *title=model.collection==dash::Collection::Pins?"My Pins":
                      model.collection==dash::Collection::Recent?"Continue Playing":"Search Games & Apps";
    text(title,190,130,large,{255,255,255,255});
    if (model.collection==dash::Collection::All) {
        const std::string filter=model.library_filter==0?"All":model.library_filter==27?"0-9":std::string(1,char('A'+model.library_filter-1));
        text("‹  "+filter+"  ›    Left / Right: filter by first letter",190,210,regular,{196,220,184,255});
    }
    const auto cards=model.library_cards();
    const int n=int(cards.size()), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    if (n==0) {
        text(model.collection==dash::Collection::Pins?"No installed titles attached yet.":
             model.collection==dash::Collection::Recent?"No recently played titles yet.":"No matching titles.",190,320,regular,{255,255,255,255});
        text(model.collection==dash::Collection::Pins?"Press Y on a game or app to attach it.":
             model.collection==dash::Collection::All?"Choose another letter with Left / Right.":"Games and apps appear here after you play them.",190,380,small,{196,208,216,255});
        return;
    }
    const int top=std::clamp(item-3,0,std::max(0,n-8));
    for (int row=0;row<8 && top+row<n;row++) {
        const int i=top+row,y=280+row*74;
        if (i==item) rounded(185,y-3,850,70,4,0x8fd23c,0x5da516);
        if (SDL_Surface *art=title_icon(cards[size_t(i)].image)) blit_contained(art,{195,y,62,62});
        text(fit(cards[size_t(i)].title,regular,735),280,y+12,regular,{255,255,255,255});
    }
    const auto &card=cards[size_t(item)];
    shade(1100,280,610,460,0x000000,100);
    if (SDL_Surface *art=title_icon(card.image)) blit_contained(art,{1120,300,570,420});
    text(fit(card.title,regular,600),1100,770,regular,{255,255,255,255});
    if(!card.subtitle.empty())
        text(fit(card.subtitle,small,600),1100,820,small,{196,220,184,255});
    text(std::to_string(item+1)+" of "+std::to_string(n),190,910,regular,{196,208,216,255});
    if (top>0) guide::chevron(1050,280,false);
    if (top+8<n) guide::chevron(1050,858,true);
}
void draw_library() {
    const auto cards=model.library_cards();
    std::string key=std::to_string(library_generation)+":"+std::to_string(model.era)+":"+
        std::to_string(int(model.collection))+":"+std::to_string(model.library_filter)+":"+std::to_string(model.viewer_item);
    for (const auto &card:cards) key+='\n'+card.title+'\t'+card.image+'\t'+card.subtitle+'\t'+std::to_string(int(card.kind));
    static std::string cached_key;
    if (library_base && key==cached_key) SDL_BlitSurface(library_base,nullptr,screen,nullptr);
    else {
        draw_library_static();
        if (library_base) {
            SDL_BlendMode blend;Uint8 alpha;
            SDL_GetSurfaceBlendMode(screen,&blend);SDL_GetSurfaceAlphaMod(screen,&alpha);
            SDL_SetSurfaceBlendMode(screen,SDL_BLENDMODE_NONE);SDL_SetSurfaceAlphaMod(screen,255);
            SDL_BlitSurface(screen,nullptr,library_base,nullptr);
            SDL_SetSurfaceBlendMode(screen,blend);SDL_SetSurfaceAlphaMod(screen,alpha);
            cached_key=key;
        }
    }
    if (!cards.empty() && !model.info) {
        const MediaFade fade=media_fade(SDL_GetTicks());
        if (fade.video>0) draw_video(1110,290,590,440,fade.video);
    }
}
