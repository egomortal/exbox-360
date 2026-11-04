// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
constexpr int kChannelTop=272, kCounterTop=842, kTagTop=88;
void draw_kinect_hub() {
    TTF_Font *body=era_text?era_text:regular;
    SDL_Rect backdrop{0,0,W,H};
    if (era_background) blit_quality(era_background,nullptr,screen,&backdrop);
    else gradient(0,0,W,H,0x358f24,0xc9e713);
    draw_picture_background();
    if (era_aura_floor) blit_quality(era_aura_floor,nullptr,screen,&backdrop);
    // Channel list (2010 dashboard capture): one size, the current channel
    // lowest at x 370 over the tiles, earlier ones fading upward 51 px apart;
    // later channels are not listed.
    TTF_Font *channel_font=large?large:body;
    for (int i=0;i<dash::ChannelCount;i++) {
        if (i==dash::Welcome && !model.welcome_available()) continue;
        const int rank=model.channel_rank(model.channel)-model.channel_rank(i);
        if (rank<0 || rank>5) continue;
        const float alpha[]={1,0.78f,0.72f,0.62f,0.42f,0.18f};
        const Uint8 saved=text_alpha;
        text_alpha=Uint8(saved*alpha[rank]);
        text(dash::channel_name(i),370,kChannelTop-rank*51,channel_font,{255,255,255,255});
        text_alpha=saved;
    }
    const int selected=model.current();
    SDL_Rect gamercard_tile{0,0,0,0}; // the avatar stands at the gamer card's right edge
    for (int slot=0;slot<3;slot++) {
        const int index=selected+slot;
        if (index>=model.count()) break;
        const dash::Card card=model.card(index);
        // Focused tile 630 x 479 at 369,348; the rest 567 x 432 at y 371, 24 apart.
        const int x=slot==0?369:1023+(slot-1)*590,y=slot==0?348:371,w=slot==0?630:567,h=slot==0?479:432;
        // The 2010 capture's tiles carry the NXE pane art (green, ringed),
        // not the white slot gradient, which belongs to empty slots.
        const auto welcome_bg=model.welcome_content.find("background");
        SDL_Surface *pane=model.channel==dash::Welcome && welcome_bg!=model.welcome_content.end()?title_icon(welcome_bg->second.art):nullptr;
        if (!pane) pane=themed_cards[index%5]?themed_cards[index%5]:card_art[index%5];
        if (SDL_Surface *art=pane) {
            SDL_Rect at{x,y,w,h};
            blit_quality(art,nullptr,screen,&at);
        } else gradient(x,y,w,h,slot==0?0xc1d445:0xb4cf32,slot==0?0x80bb06:0x76b204);
        rect(x,y,w,3,0xe9ef4a); // the lit top edge
        const SDL_Rect media{x+20,y+40,w-40,h-135};
        const bool ad_caption=card.promo>=0 && promotion_art::has_caption(card);
        const int border=0;
        const SDL_Rect ad_box{x+border,y+border,w-border*2,h-border*2};
        const SDL_Rect ad_caption_box=ad_caption?SDL_Rect{x+border,y+h-110,w-border*2,110-border}:SDL_Rect{};
        if (slot==0 && hub_media_fade(SDL_GetTicks()).video>0)
            draw_video(media.x,media.y,media.w,media.h,hub_media_fade(SDL_GetTicks()).video);
        else if (card.promo>=0) promotion_art::draw(card.image,ad_box,ad_caption_box,index);
        else if (card.title==dash::kBladesGamercard) {}
        else if (card.image.empty()) {const int side=slot==0?245:210;era_art(card,x+(w-side)/2,y+(h-side)/2-39,side,side);}
        else era_art(card,media.x,media.y,media.w,media.h);
        if (ad_caption) shade(ad_caption_box.x,ad_caption_box.y,ad_caption_box.w,ad_caption_box.h,0x000000,175);
        auto shadowed=[&](const std::string &value,int tx,int ty,TTF_Font *font,SDL_Color ink) {
            const Uint8 saved=text_alpha;text_alpha=Uint8(saved*0.45f);
            text(value,tx+1,ty+2,font,{30,50,10,255});text_alpha=saved;
            return text(value,tx,ty,font,ink);
        };
        if (card.title==dash::kBladesGamercard) {
            gamercard_tile={x,y,w,h};
            // The gamer card slot (2010 capture): gamertag, score, latest
            // games, then the local Signed In status at the foot.
            shadowed(fit(model.player,body,w-50),x+22,y+22,body,{255,255,255,255});
            const std::string points=(trophies.known?std::to_string(trophies.points()):std::string{});
            const int pw=shadowed(points,x+24,y+64,body,{255,255,255,255});
            if (gamerscore_icon) blit_contained(gamerscore_icon,{x+24+pw+8,y+74,30,30});
            shadowed("Latest Games",x+22,y+178,small,{236,242,226,235});
            shadowed("Signed In",x+22,y+h-106,body,{255,255,255,255});
        } else if (card.promo<0 || ad_caption) {
            shadowed(fit(card.title,body,w-60),x+31,y+h-106,body,{255,255,255,255});
            if (!card.subtitle.empty()) shadowed(fit(card.subtitle,regular,w-60),x+31,y+h-66,regular,{226,236,220,230});
        }
    }
    // The 2010 home: the avatar belongs to the gamer card, overlapping its
    // right half (capture: card at 1023, figure 1280..1620, 310..850), and
    // scrolls away with it.
    SDL_Surface *figure=model.channel==dash::MyXbox && gamercard_tile.w?(edited_avatar::owner_still(avatar_slots::Image::FullBody)?edited_avatar::owner_still(avatar_slots::Image::FullBody):look3d::active()?look3d::hub_frame(SDL_GetTicks()):avatar_art):nullptr;
    if (figure) {
        const float k=gamercard_tile.w/567.0f;
        SDL_Rect at{gamercard_tile.x+int(257*k),gamercard_tile.y-int(61*k),int(340*k),int(540*k)};
        if(figure==edited_avatar::fullbody)at=edited_avatar::contain(figure,at);
        blit_quality(figure,nullptr,screen,&at,false);
    }
    text(std::to_string(selected+1)+" of "+std::to_string(model.count()),370,kCounterTop,regular,{73,87,86,255});
    TTF_Font *tag_font=kinect_tag?kinect_tag:body, *count_font=kinect_count?kinect_count:regular;
    const std::string tag=fit(model.player,tag_font,560);
    text(tag,1668-text_width(tag,tag_font),kTagTop-6,tag_font,{236,242,244,235});
    // Under the gamertag: unread messages and friends online, each with its
    // 13604 icon (no Xbox Live here, so both are 0).
    auto counter=[&](const char *icon,int count,int right,int size) {
        auto it=blades_system_icons.find(std::string("kinect/")+icon);
        int x=right;
        if (it!=blades_system_icons.end()) {
            x-=size;SDL_Rect r{x,166-size/2,size,size};
            SDL_SetSurfaceAlphaMod(it->second,215);blit_quality(it->second,nullptr,screen,&r);SDL_SetSurfaceAlphaMod(it->second,255);
            x-=9;
        }
        const std::string n=std::to_string(count);
        text(n,x-text_width(n,count_font),142,count_font,{230,238,240,235});
    };
    counter("ico_32x_Friend",0,1668,36);
    counter("ico_32x_Mail",0,1560,42);
}
