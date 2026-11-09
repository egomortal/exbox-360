// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_cards() {
    static constexpr unsigned tops[]={0xb5d825,0xc4db32,0xcfdc4f,0x678bc0,0x936cba};
    static constexpr unsigned bottoms[]={0x477505,0x557e10,0x6e7925,0x304c74,0x4c3765};
    const Tint tint=theme_tint(model.theme);
    const Uint32 now=SDL_GetTicks();
    // The disc and trailer play on the front pane only while the hub is in
    // front (Details shows them itself).
    const MediaFade fade=cards_static?MediaFade{}:hub_media_fade(now);
    const int selected=model.current();
    // Pane depth, measured off the 2008 capture: right edges, scales and
    // centre lines of the front five panes (each tucked behind the one before).
    constexpr float kRight[]={772,1238,1514,1696,1826,1930,2010};
    constexpr float kScale[]={1,.7375f,.5875f,.5f,.417f,.36f,.32f};
    constexpr float kCentre[]={615,603,597,596,590,588,587};
    auto at=[](const float *table,float d) {
        d=std::clamp(d,0.0f,5.999f);const int i=int(d);const float t=d-i;
        return table[i]+(table[i+1]-table[i])*t;
    };
    for (int i=std::min(model.count()-1,selected+5);i>=selected;i--) {
        float depth=std::max(0.0f,float(i)-position);
        if (depth>5.7f) continue;
        const dash::Card card=model.card(i);
        const float scale=at(kScale,depth);
        const int w=int(630*scale+0.5f),h=int(480*scale+0.5f);
        const int x=int(at(kRight,depth)+0.5f)-w, y=int(at(kCentre,depth)-h/2.0f+0.5f)+card_dy;
        nxe_reflection::remember_pane({x,y,w,h});
        gradient(x,y,w,h,tint_rgb(tops[i%5],tint),tint_rgb(bottoms[i%5],tint));
        const auto welcome_bg=model.welcome_content.find("background");
        SDL_Surface *pane=model.channel==dash::Welcome && welcome_bg!=model.welcome_content.end()?title_icon(welcome_bg->second.art):nullptr;
        if (!pane) pane=themed_cards[i%5]?themed_cards[i%5]:card_art[i%5];
        if (SDL_Surface *art=pane) {
            SDL_Rect destination{x,y,w,h};
            blit_quality(art,nullptr,screen,&destination);
        }
        if (card.kind==dash::Kind::Missing || card.kind==dash::Kind::Empty) shade(x,y,w,h,0x202820,110);
        // The panes carry no outline; the caption sits on a darker foot.
        const int border=0;
        shade(x,y+int(h*0.72f),w,h-int(h*0.72f),0x000000,34);
        const bool gamercard=card.title==dash::kBladesGamercard;
        int cx=x+w/2,cy=y+int(h*0.42f);
        const bool front=i==selected;
        const bool ad_caption=card.promo>=0 && promotion_art::has_caption(card);
        const SDL_Rect ad_box{x+border,y+border,w-border*2,h-border*2};
        const SDL_Rect ad_caption_box=ad_caption?SDL_Rect{x+border,y+h-70,w-border*2,70-border}:SDL_Rect{};
        // The title's own art, until the disc (then the trailer) fades in over it.
        if (gamercard) {
            // NXE's gamer card slot: gamertag, score, latest games, status.
            TTF_Font *big=scale>0.9f?era_text:scale>0.65f?regular:small;
            TTF_Font *mid=scale>0.9f?regular:nullptr;
            text(fit(model.player,big,w-int(60*scale)),x+int(30*scale),y+int(39*scale),big,{250,252,246,255});
            const std::string points=(trophies.known?std::to_string(trophies.points()):std::string{});
            const int pw=text(points,x+int(34*scale),y+int(71*scale),big,{250,252,246,255});
            if (gamerscore_icon) blit_contained(gamerscore_icon,{x+int(34*scale)+pw+int(8*scale),y+int(83*scale),int(30*scale),int(30*scale)});
            if (mid) {
                text("Latest Games",x+int(28*scale),y+int(214*scale),mid,{236,242,226,235});
                int shown=0;
                for (const auto &key:model.recent) {
                    std::string icon;
                    for (const auto &g:model.games) if ("title:"+g.title_id==key) icon=g.icon;
                    if (icon.empty()) for (const auto &a:model.apps) if (a.executable==key) icon=a.icon;
                    if (SDL_Surface *own=title_icon(icon)) {
                        SDL_Rect to{x+int((32+shown*91)*scale),y+int(250*scale),int(78*scale),int(78*scale)};
                        blit_quality(own,nullptr,screen,&to);
                        if (++shown==4) break;
                    }
                }
            }
            text("Signed In",x+int(31*scale),y+h-int(99*scale),big,{250,252,246,255});
        } else if (!front || (fade.spin<1 && fade.video<1) || (card.promo>=0 && fade.video<1)) {
            SDL_Surface *icon=card.icon>=0 && card.icon<dash::IconCount?(themed_icons[card.icon]?themed_icons[card.icon]:card_icons[card.icon]):nullptr;
            if (card.promo>=0) {
                promotion_art::draw(card.image,ad_box,ad_caption_box,i);
            } else if (SDL_Surface *own=card_picture(card)) {
                // The game's or app's own icon, framed like a 360 game tile.
                const SDL_Rect box{x+w/10,y+h/10,w*8/10,h*6/10};
                SDL_Rect destination=contained_rect(own->w,own->h,box);
                if (model.channel!=dash::Welcome) {
                    shade(destination.x+6,destination.y+8,destination.w,destination.h,0x000000,90);
                    rect(destination.x-3,destination.y-3,destination.w+6,destination.h+6,0xf2f8ea);
                }
                blit_quality(own,nullptr,screen,&destination);
            } else if (icon && card.kind==dash::Kind::Hub &&
                       custom_scene::draw_slot(card.icon,i,front,{x,y,w,h},icon,now)) {
                // slots/*SlotScene.xur (9199): focus, skin decoration and reflection.
            } else if (icon) {
                // Preserve each extracted PNG's aspect ratio. Its lower
                // portion is a reflection, so centre the visible upper art
                // rather than the entire image within the card.
                const int width=int((card.icon==dash::IconDisc?305:250)*scale);
                const int height=std::max(1,icon->h*width/icon->w);
                const int upper_centre=int(icon->h*0.36f*width/icon->w);
                SDL_Rect destination{cx-width/2,y+h/2-upper_centre,width,height};
                blit_quality(icon,nullptr,screen,&destination);
            } else if (disc_art) {
                int size=int(190*scale);
                SDL_Rect destination{cx-size/2,cy-size/2,size,size};
                blit_quality(disc_art,nullptr,screen,&destination);
            } else {
                disc(cx,cy,int(93*scale),0xeaf5d7);
                ring(cx,cy,int(91*scale),std::max(2,int(6*scale)),0x5a7719);
                ring(cx,cy,int(22*scale),std::max(2,int(5*scale)),0x5a7719);
            }
        }
        if (front && !gamercard && fade.video<1 && card.promo<0) draw_spin(cx,cy,int(230*scale),now,fade.spin);
        if (front && fade.video>0) {
            draw_video(x+12,y+12,w-24,h-80,fade.video);
        }
        // Every pane is captioned (title, then a second line), as in the capture.
        if (!gamercard) {
            if (front && ad_caption) shade(ad_caption_box.x,ad_caption_box.y,ad_caption_box.w,ad_caption_box.h,0x000000,175);
            if (front && card.kind==dash::Kind::Pinned) {
                shade(x+w-150,y+18,128,34,0x16300e,190);
                text("ATTACHED",x+w-140,y+22,small,{214,240,190,255});
            }
            TTF_Font *title_font=scale>0.9f?era_text:scale>0.65f?regular:scale>0.5f?small:nullptr;
            TTF_Font *line_font=scale>0.9f?regular:scale>0.65f?small:nullptr;
            if (title_font && (card.promo<0 || ad_caption)) {
                text(fit(card.title,title_font,w-int(48*scale)),x+int(31*scale),y+h-int(99*scale),title_font,{255,255,255,255});
                if (line_font && !card.subtitle.empty())
                    text(fit(card.subtitle,line_font,w-int(48*scale)),x+int(31*scale),y+h-int(62*scale),line_font,{226,232,220,230});
            }
        }
    }
}
