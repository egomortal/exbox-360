// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_hub(Uint32 now) {
    if (model.era==0) {if (!source_blades::draw(now)) draw_blades_hub();return;}
    if (model.era==2) {draw_kinect_hub();return;}
    if (model.era==3) {draw_metro_hub();return;}
    nxe_reflection::begin_frame();
    apply_theme();
    const Tint tint=theme_tint(model.theme);
    if (SDL_Surface *bg=themed_background?themed_background:background) SDL_BlitSurface(bg,nullptr,screen,nullptr);
    else {gradient(0,0,W,620,0x78a937,0xe6edce);gradient(0,620,W,H-620,0x57646d,0xd6d9d6);}
    const bool custom=draw_picture_background();
    if (!custom && !retail_background) {
        rect(0,614,W,2,0xe9eedb);
        for (int i=0;i<16;i++) ring((i*337+107)%W,(i*173+43)%570,12+(i*11)%31,2,tint_rgb(0xadd77a,tint));
    }
    draw_atmosphere(now);
    // Changing channel slides the panes: the new ones rise into place while the
    // old ones carry on upward and fade (or the other way when going up).
    float e=1;
    if (slide_from>=0) {
        if (now-slide_at>=kSlide || slide_from==model.channel) slide_from=-1;
        else e=ease_out(float(now-slide_at)/kSlide);
    }
    const int dir=slide_from<0?0:(model.channel_rank(model.channel)>model.channel_rank(slide_from)?1:-1);
    // The channel list (designer captures): the current channel sits lowest,
    // large, with the ones before it stacked above in smaller, fainter type;
    // the channels after it are not listed. Ranks slide while the panes do.
    {
        struct Rank {int y;TTF_Font *font;float alpha;};
        TTF_Font *const fallback[5]={large,era_text,regular,small,small};
        auto font_at=[&](int rank) {return nxe_channel[rank]?nxe_channel[rank]:fallback[rank];};
        const Rank ranks[]={{264,font_at(0),1},{212,font_at(1),0.78f},{172,font_at(2),0.66f},{136,font_at(3),0.55f},{100,font_at(4),0.32f}};
        for (int i=0;i<dash::ChannelCount;i++) {
            if (i==dash::Welcome && !model.welcome_available()) continue;
            const float before=float(model.channel_rank(slide_from>=0?slide_from:model.channel)-model.channel_rank(i)),after=float(model.channel_rank(model.channel)-model.channel_rank(i));
            const float d=before*(1-e)+after*e;
            if (d<=-1 || d>=4.99f) continue;
            const int lo=std::clamp(int(std::floor(d)),0,4),hi=std::min(4,lo+1);
            const float t=std::clamp(d-float(lo),0.0f,1.0f);
            const int y=int(ranks[lo].y*(1-t)+ranks[hi].y*t);
            float alpha=ranks[lo].alpha*(1-t)+ranks[hi].alpha*t;
            if (d<0) alpha=1+d;
            TTF_Font *font=ranks[d<0?0:int(std::lround(std::max(0.0f,d)))>4?4:int(std::lround(std::max(0.0f,d)))].font;
            const Uint8 saved=text_alpha;
            text_alpha=Uint8(saved*std::clamp(alpha,0.0f,1.0f));
            if (d<0.5f && d>-0.5f) shade(125,y+34,11,11,0xffffff,text_alpha);
            text(dash::channel_name(i),145,y+(d<0?int(-d*40):0),font,{255,255,255,255});
            text_alpha=saved;
        }
    }
    card_dy=int(float(dir)*(1-e)*kSlideDistance);
    draw_cards();
    card_dy=0;
    // The rendered avatar is framed tightly, so it stands smaller than the
    // silhouette art did: feet on the floor line beside the panes.
    const bool built=look3d::active() || edited_avatar::fullbody, own_avatar=built || avatar::installed();
    const bool show_figure=model.channel==dash::MyXbox || slide_from==dash::MyXbox;
    SDL_Surface *figure=!show_figure?nullptr:edited_avatar::owner_still(avatar_slots::Image::FullBody)?edited_avatar::owner_still(avatar_slots::Image::FullBody):built?look3d::hub_frame(now):own_avatar?avatar::hub_frame(now):avatar_art;
    // The avatar stands on its gamer card slot (2008 capture: centred near
    // x 600, feet at y 930); it shows while that slot is in front.
    const SDL_Rect figure_at=built?SDL_Rect{440,275,400,660}:own_avatar?SDL_Rect{455,310,372,620}:SDL_Rect{370,30,540,900};
    const bool card_front=model.current_card().title==dash::kBladesGamercard;
    if (slide_from>=0 && begin_layer()) {
        const int channel=model.channel;
        const float kept=position;
        model.channel=slide_from;
        position=slide_position;
        cards_static=true;
        card_dy=int(-float(dir)*e*kSlideDistance);
        draw_cards();
        card_dy=0;
        cards_static=false;
        model.channel=channel;
        position=kept;
        end_layer(1-e);
    }
    // The avatar stands on the floor where it is: it does not ride with the
    // panes when the channel changes, it only fades in (arriving at My Xbox) or
    // out (leaving it), drawn after the panes so no old pane passes over it.
    if (figure) {
        float shown=0;
        if (model.channel==dash::MyXbox) shown=slide_from>=0 && slide_from!=dash::MyXbox?e:1;
        else if (slide_from==dash::MyXbox) shown=1-e;
        {
            // Fade (never slide) with the gamer card slot coming to or leaving the front.
            static float card_shown=1;static Uint32 last=0;
            const float dt_s=last?std::min(0.1f,(now-last)/1000.0f):0;last=now;
            card_shown=std::clamp(card_shown+(card_front?dt_s:-dt_s)*6,0.0f,1.0f);
            shown*=card_shown;
        }
        if (shown>0.01f) {
            SDL_Rect destination=figure==edited_avatar::fullbody?edited_avatar::contain(figure,figure_at):figure_at;
            nxe_reflection::remember_figure(figure,destination,shown);
            SDL_SetSurfaceAlphaMod(figure,Uint8(255*shown));
            blit_quality(figure,nullptr,screen,&destination,false);
            SDL_SetSurfaceAlphaMod(figure,255);
        }
    }
    reflect_cards();
    draw_gamercard();
    // Counter under the focused tile's reflection, as the captures: y 871 cap.
    const Uint8 saved=text_alpha;text_alpha=Uint8(saved*0.62f);
    text(std::to_string(model.current()+1)+" of "+std::to_string(model.count()),145,862,regular,{43,54,60,255});
    text_alpha=saved;
}
