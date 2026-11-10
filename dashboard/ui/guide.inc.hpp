// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
namespace guide {
constexpr float k=1.5f;
constexpr int PX=552, PY=300, PW=816, PH=473;      // panel, 720p x 368-912 y 200-515
constexpr int TAB=66;                               // the open blade's tab, 44
constexpr int SPIN_D=45, SPIN_Y=315;                // the arc above the tab label
constexpr int ITEM_TOP=PY+22, ITEM_PITCH=60, ITEM_H=60, ITEM_X=PX+TAB+19, BAR_W=710, ROWS=7;
constexpr float SLAB_W=41*k, SLAB_OVERLAP=4*k, SLAB_TUCK=3*k;
constexpr float SLAB_SCALE[4]={0.96f,0.93f,0.90f,0.87f};
constexpr int PIC_X=924, PIC_Y=222, PIC_W=72, PIC_H=75; // gamerpic, 616-664 x 148-198
constexpr int CLOCK_RIGHT=1360, CLOCK_Y=258; // nxe-guide capture: clock ends at 1357
constexpr int PROMPT_X=561, PROMPT_Y=788, PROMPT_D=40; // nxe-guide capture: 29 px orbs inside the 32 px art
constexpr Uint8 DIM=224;                            // the hub keeps 12% of its light
// Open: grow 0.1 to 1.05 by 233 ms, settle by 300, content in 233 to 366.
// Close: swell to 1.05 in 50 ms, shrink to 0.1 and fade by 283. Dim 230 / 250.
constexpr Uint32 GROW=233, SETTLE=300, CONTENT=133, POP=50, SHRINK=233, DIM_IN=230, DIM_OUT=250, BLADE=170;
SDL_Surface *tab_labels[dash::BladeCount]{}, *slab_labels[dash::BladeCount]{};
SDL_Surface *flat_labels[dash::BladeCount]{}, *metro_games_label=nullptr;
SDL_Surface *layer=nullptr;
TTF_Font *item_font=nullptr, *tab_font=nullptr, *slab_font=nullptr, *prompt_font=nullptr, *clock_font=nullptr;
float slab_x[dash::BladeCount]{}, slab_s[dash::BladeCount]{};
bool was_open=false, closing=false;
Uint32 opened_at=0, closed_at=0, blade_at=0;
int shown_blade=-1;

float ramp(int slot) { return SLAB_SCALE[std::clamp(slot,0,3)]; }
float offset(int slot) {
    float o=-SLAB_TUCK;
    for (int i=0;i<slot;i++) o+=SLAB_W*ramp(i)-SLAB_OVERLAP;
    return o;
}
// Where blade d rests when `focus` is open: left of the panel, right of it, or under it.
void place(int d, int focus, float &x, float &scale) {
    if (d<focus) {int slot=focus-1-d; scale=ramp(slot); x=PX-offset(slot)-SLAB_W;}
    else if (d>focus) {int slot=d-focus-1; scale=ramp(slot); x=PX+PW+offset(slot);}
    else {scale=1; x=PX;}
}
void make_labels() {
    for (int d=0;d<dash::BladeCount;d++) {
        if (tab_labels[d]) SDL_FreeSurface(tab_labels[d]);
        if (slab_labels[d]) SDL_FreeSurface(slab_labels[d]);
        if (flat_labels[d]) SDL_FreeSurface(flat_labels[d]);
        tab_labels[d]=rotated_label(tab_font,model.blade_title(d),{255,255,255,255});
        slab_labels[d]=rotated_label(slab_font,model.blade_title(d),{58,64,74,255});
        flat_labels[d]=rotated_label(font_sets[1][4],model.blade_title(d),{240,243,243,255});
    }
    if (metro_games_label) SDL_FreeSurface(metro_games_label);
    metro_games_label=rotated_label(font_sets[1][4],"Games & Apps",{240,243,243,255});
    if (!layer) {
        layer=render_density::create(W,H);
        if (layer) SDL_SetSurfaceBlendMode(layer,SDL_BLENDMODE_BLEND);
    }
}
void spinner(int cx, int cy) {
    if (custom_scene::draw_loading(cx,cy,SPIN_D)) return;
    // A ring, mostly light, with a green run and a gap, as on the tab.
    const float r=SPIN_D/2.0f;
    for (int a=0;a<360;a+=2) {
        if (a>20 && a<52) continue;
        float rad=float(a-90)*3.14159265f/180.0f;
        unsigned c=(a>=52 && a<120)?0x8fd23c:0xd5dbe2;
        for (float t=r-4;t<=r;t+=1.0f) rect(cx+int(std::cos(rad)*t),cy+int(std::sin(rad)*t),2,2,c);
    }
}
void chevron(int x, int y, bool down) {
    for (int i=0;i<8;i++) {
        int yy=down?y+i:y+7-i;
        rect(x+i,yy,3,2,0xc8d0dc); rect(x+14-i,yy,3,2,0xc8d0dc);
    }
}
// What a home-blade row carries at its right edge, as the 2008-2012 Guides
// showed it: the game's own icon beside "Play â€¦", and Metro's social counts.
void row_extra(const dash::GuideEntry &entry,int right,int y,int h,bool selected) {
    SDL_Surface *icon=nullptr;
    if (entry.action==dash::GuideAction::MyXbox && dashboard_title) {
        blit_contained(dashboard_title,{right-h+6,y+5,h-10,h-10});return;
    }
    if (entry.label.rfind("Play ",0)==0) {
        if (entry.action==dash::GuideAction::LaunchTitle && entry.app>=0 && entry.app<int(model.games.size()))
            icon=title_icon(model.games[size_t(entry.app)].icon,false);
        else if (entry.action==dash::GuideAction::Launch && entry.app>=0 && entry.app<int(model.apps.size()))
            icon=title_icon(model.apps[size_t(entry.app)].icon,false);
        if (icon) blit_contained(icon,{right-h+6,y+5,h-10,h-10});
        return;
    }
    if (model.era!=3) return;
    // metro-guide capture: each social row carries its 17559 icon at the right,
    // Friends / Party / Messages with a count before it.
    const std::string &l=entry.label;
    const char *glyph=l=="Friends"?"metro/ico_32x_Friend":l=="Party"?"metro/ico_32x_StartParty":l=="Messages"?"metro/ico_32x_Mail"
                     :l=="Beacons & Activity"?"metro/ico_32x_Beacons":l=="Chat"?"metro/ico_64x_Chat":l=="Open Tray"?"metro/ico_32x_OpticalMedia":nullptr;
    const bool counted=l=="Friends" || l=="Party" || l=="Messages";
    const SDL_Color ink=selected?SDL_Color{235,235,235,255}:SDL_Color{38,42,41,255};
    int x=right-14;
    if (glyph) {
        auto it=blades_system_icons.find(glyph);
        if (it!=blades_system_icons.end()) {
            const Uint8 v=selected?255:40;
            SDL_SetSurfaceColorMod(it->second,v,v,v);
            x-=36;SDL_Rect r{x,y+(h-36)/2,36,36};blit_quality(it->second,nullptr,screen,&r);
            SDL_SetSurfaceColorMod(it->second,255,255,255);
            x-=10;
        }
    }
    if (counted) text("0",x-text_width("0",era_text),y+8,era_text,ink);
}
// A compact transport embedded in the normal Guide row. It deliberately uses
// the same authored Play/Pause/Stop artwork and interaction pattern as Music.
void recording_transport(int x,int y,int h,bool selected,int gap=52,int size=34) {
    if(!model.recorder_available){
        if(blades_icons[dash::IconVideo])blit_contained(blades_icons[dash::IconVideo],{x,y+(h-size)/2,size,size});
        return;
    }
    SDL_Surface *icons[]={music_controls[0],music_controls[4],music_controls[2]};
    const bool enabled[]={!model.recorder_recording||model.recorder_paused,
        model.recorder_recording&&!model.recorder_paused&&model.recorder_pause_supported,
        model.recorder_recording};
    for(int i=0;i<3;++i){
        const int at=x+i*gap;
        if(selected&&model.recorder_control==i&&enabled[i])rounded(at-7,y+(h-size-14)/2,size+14,size+14,8,0x9bcf52,0x4f851d);
        if(!icons[i])continue;
        SDL_SetSurfaceAlphaMod(icons[i],Uint8(enabled[i]?(model.recorder_busy?150:255):75));
        blit_contained(icons[i],{at,y+(h-size)/2,size,size});
        SDL_SetSurfaceAlphaMod(icons[i],255);
    }
}
void slab(int d) {
    const float w=SLAB_W*slab_s[d], h=PH*slab_s[d];
    const bool right=slab_x[d]>=PX+PW/2;
    const int x=int(right?slab_x[d]:slab_x[d]+SLAB_W-w), y=int(PY+PH/2.0f*(1-slab_s[d]));
    const int iw=int(w+0.5f), ih=int(h+0.5f);
    if (blade_grey) {
        // Keep the source blade's full-height bowed edge. A vertical nine-slice
        // stretches its middle row and turns the distinctive curve into a bar.
        SDL_Rect at{x-6,y-16,iw+12,ih+32};
        blit_quality(blade_grey,nullptr,screen,&at);
    } else {
        rounded(x+4,y+6,iw,ih,12,0x101418,0x101418);                  // drop shadow
        rounded(x,y,iw,ih,12,0xa9b0ba,0x8c939d);                       // rim
        rounded(x+1,y+1,iw-2,ih-2,11,0xe9edf2,0xc3c9d1);                // face
        rect(right?x+1:x+iw-5,y+10,4,ih-20,0xb4bbc5);                   // the edge under its neighbour
    }
    if (SDL_Surface *label=slab_labels[d]) {
        int lx=right?x+iw-int(20*k)-render_density::logical_w(label)/2:x+int(20*k)-render_density::logical_w(label)/2;
        SDL_Rect r{lx,y+(ih-render_density::logical_h(label))/2,render_density::logical_w(label),std::min(render_density::logical_h(label),ih-20)};
        SDL_Rect src{0,0,render_density::logical_w(label),r.h};
        SDL_SetSurfaceAlphaMod(label,text_alpha);
        SDL_BlitSurface(label,&src,screen,&r);
    }
}
void flat_body() {
    const int focus=std::clamp(model.guide_blade,model.era==3?1:0,int(dash::BladeSettings));
    constexpr int x=548,y=303,body_x=642,right=1370,height=473,pitch=63;
    const int minimum=model.era==3?1:0;
    for (int d=minimum;d<dash::BladeCount;d++) {
        const int left=d<focus?x-(focus-d)*70:d>focus?right+(d-focus-1)*70:x;
        const int width=d==focus?94:70;
        rect(left,y,width,height,d==focus?0xc8cece:0x56636c);
        rect(left,y,2,height,d==focus?0xe4e8e7:0x717d83);
        SDL_Surface *label=model.era==3 && d==dash::BladeGames?metro_games_label:flat_labels[d];
        if (label) {
            SDL_Rect at{left+(width-render_density::logical_w(label))/2,y+24,render_density::logical_w(label),std::min(render_density::logical_h(label),height-48)};
            SDL_Rect source{0,0,render_density::logical_w(label),at.h};
            SDL_SetSurfaceColorMod(label,d==focus?68:255,d==focus?74:255,d==focus?75:255);
            SDL_SetSurfaceAlphaMod(label,text_alpha);
            SDL_BlitSurface(label,&source,screen,&at);
        }
    }
    // Body and focus colours sampled from the 2010 and 2012 Guide captures
    // (kinect-guide / metro-guide, 1080p): Kinect's panel falls from about
    // d6dad8 to bdc6c7 under a green bar 6eb010 -> 3f7f05 with no lit edge;
    // Metro's panel is greyer (d0d4d3 -> b4bcbf) under a flat 107c0f bar.
    const bool metro_guide=model.era==3;
    if (metro_guide) gradient(body_x,y,right-body_x,height,0xd0d4d3,0xb4bcbf);
    else gradient(body_x,y,right-body_x,height,0xd6dad8,0xbdc6c7);
    const auto entries=model.blade_entries(focus);
    const int n=int(entries.size()),item=std::clamp(model.guide_item[focus],0,std::max(0,n-1));
    const int first=std::clamp(item-6,0,std::max(0,n-7));
    for (int row=0;row<7 && first+row<n;row++) {
        const int entry_index=first+row;
        int top=y+row*pitch;
        bool selected=entry_index==item;
        if (selected) {
            if (!metro_guide) gradient(body_x,top,right-body_x,pitch,0x6eb010,0x3f7f05);
            else rect(body_x,top,right-body_x,pitch,0x107c0f);
        } else shade(body_x,top+pitch-1,right-body_x,2,0x9facb2,110); // the skin's row rule, at the capture's strength
        int label_x=body_x+22;
        if (entries[size_t(entry_index)].action==dash::GuideAction::Music && model.now_playing>=0)
            if (const music::Song *song=music_player.current_song())
                if (SDL_Surface *cover=song_cover(song->path)) {
                    rect(body_x+14,top+6,pitch-12,pitch-12,0x30383a);
                    blit_contained(cover,{body_x+15,top+7,pitch-14,pitch-14});
                    label_x=body_x+pitch+16;
                }
        const bool recorder=entries[size_t(entry_index)].action==dash::GuideAction::RecordScreen;
        text(fit(entries[size_t(entry_index)].label,era_text,right-label_x-(recorder?210:100)),label_x,top+8,era_text,
             selected?SDL_Color{235,235,235,255}:metro_guide?SDL_Color{38,42,41,255}:SDL_Color{20,24,24,255});
        if(recorder)recording_transport(right-174,top,pitch,selected,48);
        if (model.guide_blade==dash::BladeHome) row_extra(entries[size_t(entry_index)],right,top,pitch,selected);
    }
    if (first>0) chevron(right-27,y+10,false);
    if (first+7<n) chevron(right-27,y+height-22,true);
    text("Xbox Guide",x,235,era_text,{244,245,245,255});
    draw_gamerpic(PIC_X,PIC_Y,PIC_W,PIC_H);
    const std::string clock=clock_text();
    text(clock,right-text_width(clock,era_text),CLOCK_Y-3,era_text,{247,248,248,255});
    if (ring_of_light) blit_contained(ring_of_light,{1332,216,40,40});
    int at=x;
    for (const auto &hint:model.hints()) {
        glyph(hint.button,at-4,791,37);at+=43; // 2010-12 captures: 29 px discs from x 548
        at+=text(hint.label,at,789,regular,{247,248,248,255})+27;
    }
}
// The Blades Guide, measured off the designer capture (1080p). Every frame,
// button and line position below is a measured edge; the art is the 1888
// dashboard's own (shared/*-Button, ico_32x_*, dashcomm/btn_32x_*, splash_360).
namespace bg {
inline int inset(int r,int row,int h) {
    if (r<=0) return 0;
    const int dy=row<r?r-row:(row>=h-r?row-(h-r-1):0);
    return dy>0?r-int(std::sqrt(float(r*r)-(dy-0.5f)*(dy-0.5f))):0;
}
// A physical scanline, after logical geometry is converted exactly once.
inline void physical_span(int x,int y,int w,unsigned color,Uint8 alpha) {
    if(w<=0)return;
    if(alpha==255) {physical_rect(x,y,w,1,color);return;}
    if(!shade_pixel)return;
    physical_shade(x,y,w,1,color,alpha);
}
// Pixels between the rounded rect and the one inset by t.
inline void ring(int x,int y,int w,int h,int r,int t,unsigned color,Uint8 alpha=255) {
    const int d=render_density::density(screen);x*=d;y*=d;w*=d;h*=d;r*=d;t*=d;
    for (int row=0;row<h;row++) {
        const int io=inset(r,row,h);
        auto span=[&](int x0,int x1) {physical_span(x0,y+row,x1-x0,color,alpha);};
        if (row<t || row>=h-t) {span(x+io,x+w-io);continue;}
        const int ii=t+inset(std::max(0,r-t),row-t,h-2*t);
        span(x+io,x+std::max(io,ii));
        span(x+w-std::max(io,ii),x+w-io);
    }
}
inline void fill(int x,int y,int w,int h,int r,unsigned color,Uint8 alpha=255) {
    const int d=render_density::density(screen);x*=d;y*=d;w*=d;h*=d;r*=d;
    for (int row=0;row<h;row++) {
        const int io=inset(r,row,h);
        physical_span(x+io,y+row,w-2*io,color,alpha);
    }
}
inline unsigned grey(int v) {v=std::clamp(v,0,255);return unsigned(v)<<16|unsigned(v)<<8|unsigned(v);}
// A cached face: f(x,y) gives the grey level; the shape is a rounded rect.
struct Face {SDL_Surface *s=nullptr;int w=0,h=0;};
template<class F> SDL_Surface *make_face(int w,int h,int r,F f) {
    SDL_Surface *out=render_density::create(w,h);
    if (!out) return nullptr;
    const int d=render_density::density(out);
    for (int y=0;y<out->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        const int io=inset(r*d,y,out->h);
        for (int x=0;x<out->w;x++) row[x]=x<io||x>=out->w-io?0u:(0xff000000u|f(float(x)/d,float(y)/d));
    }
    SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
    return out;
}
inline SDL_Surface *art(const char *name) {
    auto it=blades_system_icons.find(std::string("guide/")+name);
    return it==blades_system_icons.end()?nullptr:it->second;
}
// Engraved frame (boxes): a lit lip outside, a 2-3 px dark line, a soft inner fall.
inline void frame(int x,int y,int w,int h,int r) {
    ring(x-2,y-2,w+4,h+4,r+2,2,0xffffff,26);
    ring(x,y,w,h,r,3,0x747474);
    ring(x+3,y+3,w-6,h-6,std::max(1,r-3),1,0x888888);
    ring(x+4,y+4,w-8,h-8,std::max(1,r-4),1,0x959595);
}
// Raised button: a dark halo (heavier right / below), a 3 px rim, top gloss.
inline void button(int x,int y,int w,int h,bool focus) {
    shade(x-3,y-4,w+7,h+10,0x000000,30);
    shade(x+2,y+h,w+2,6,0x000000,34);
    shade(x+w,y+3,5,h,0x000000,30);
    if (focus) for (int i=10;i>=1;i--) ring(x-i,y-i,w+2*i,h+2*i,8+i,1,0x8fd05e,Uint8(std::max(0,130-i*12)));
    constexpr int gloss[]={133,161,168,181,186,190,190,190,190,186,180,173,168,165,163};
    for (int row=3;row<h-3;row++) {
        const int i=(row-3)/2;
        int v=i<15?gloss[i]:162;
        if (row>=h-5) v=168;
        const int io=inset(7,row,h);
        rect(x+io,y+row,w-2*io,1,grey(v));
    }
    ring(x,y,w,h,8,3,0x737373);
}
inline void groove(int y) {rect(158,y,550,2,0x858585);rect(158,y+2,550,1,0xb0b0b0);}
inline void glow(int x,int y,int w,int h,int r) {
    for (int i=10;i>=1;i--) ring(x-i,y-i,w+2*i,h+2*i,r+i,1,0x8fd05e,Uint8(std::max(0,120-i*11)));
}
}
SDL_Surface *blades_ring=nullptr, *blades_spine=nullptr, *blades_gamercard=nullptr, *blades_xbox=nullptr;
std::string blades_spine_name;
void blades_ring_icon(int x,int y) {
    if (!blades_ring) {
        blades_ring=render_density::create(48,48);
        if (!blades_ring) return;
        SDL_FillRect(blades_ring,nullptr,0);
        // Player one's ring of light as the spine shows it: four grey quarter
        // arcs with soft gaps, the upper-left one lit green.
        const int density=render_density::density(blades_ring);
        for (int py=0;py<blades_ring->h;py++) for (int px=0;px<blades_ring->w;px++) {
            const float dx=(px+.5f)/density-24.f,dy=(py+.5f)/density-24.f,d=std::sqrt(dx*dx+dy*dy);
            float a=std::clamp(std::min(d-16.5f,21.5f-d)*density+0.5f,0.0f,1.0f);
            const float gap=std::min(std::fabs(dx),std::fabs(dy));
            a*=std::clamp((gap-1.0f)/1.2f,0.0f,1.0f);
            const bool lit=dx<0 && dy<0;
            const float t=std::clamp((21.5f-d)/5,0.0f,1.0f);      // 0 outer, 1 inner
            const unsigned c=lit?era_chrome::mix(0x3a9412,0x74cc36,t):era_chrome::mix(0x585858,0xa2a2a2,t);
            era_chrome::pixel(blades_ring,px,py,c,a);
        }
        SDL_SetSurfaceBlendMode(blades_ring,SDL_BLENDMODE_BLEND);
    }
    SDL_Rect to{x,y,48,48};SDL_BlitSurface(blades_ring,nullptr,screen,&to);
}
// Light caption with the capture's drop shadow.
int blades_caption_text(const std::string &label,int x,int y) {
    text(label,x+1,y+2,era_text,{30,32,28,200});
    return text(label,x,y,era_text,{214,216,212,255});
}
void blades_orb(xb::Button button,int x,int y) {
    const char *name=button==xb::A?"A-Button":button==xb::B?"B-Button":button==xb::X?"X-Button":button==xb::Y?"Y-Button":nullptr;
    SDL_Surface *orb=name?bg::art(name):nullptr;
    if (!orb) {glyph(button,x,y,38);return;}
    SDL_Rect to{x,y,38,38};blit_quality(orb,nullptr,screen,&to,true);
    const char *letter=button==xb::A?"A":button==xb::B?"B":button==xb::X?"X":"Y";
    TTF_Font *font=semibold?semibold:regular;
    text(letter,x+19-text_width(letter,font)/2,y+3,font,{28,28,26,235});
}
void blades_hints(int) {
    for (const auto &hint:model.hints()) {
        if (hint.button==xb::Y) {blades_orb(xb::Y,165,879);blades_caption_text(hint.label,213,875);}
        else if (hint.button==xb::X) {blades_orb(xb::X,142,918);blades_caption_text(hint.label,191,915);}
        else if (hint.button==xb::B) {blades_orb(xb::B,654,879);blades_caption_text(hint.label,644-text_width(hint.label,era_text),875);}
        else if (hint.button==xb::A) {blades_orb(xb::A,674,918);blades_caption_text(hint.label,665-text_width(hint.label,era_text),915);}
    }
}
void blades_panel() {
    using namespace bg;
    // The 1888 Guide's own scenes when installed; the home page is all scene,
    // the other pages draw their list over its frame.
    if (guide_scene::draw(model.guide_blade==dash::BladeHome)) return;
    const bool from_scene=false;
    if (!from_scene) {
    if (era_chrome::guide) SDL_BlitSurface(era_chrome::guide,nullptr,screen,nullptr);
    // Header: the controller battery (outline, dark cell) and the clock.
    // No verified controller battery reading is available in this fallback.
    std::string clock=clock_text();
    if (const auto at=clock.rfind(' ');at!=std::string::npos) clock.insert(at," ");
    text(clock,711-text_width(clock,era_text),90,era_text,{244,244,242,255});
    // Spine: player one's ring and the gamertag, reading downwards.
    blades_ring_icon(778,198);
    if (!blades_spine || blades_spine_name!=model.player) {
        SDL_FreeSurface(blades_spine);
        TTF_Font *spine_font=::blades_body?::blades_body:era_text;
        blades_spine=rotated_label(spine_font,fit(model.player,spine_font,330),{34,34,32,255});
        blades_spine_name=model.player;
    }
    if (blades_spine) {SDL_Rect to{765,373,render_density::logical_w(blades_spine),render_density::logical_h(blades_spine)};SDL_BlitSurface(blades_spine,nullptr,screen,&to);}
    // Gamercard: bright sheen face, peak right of centre, dark rim.
    if (!blades_gamercard) blades_gamercard=make_face(566,121,9,[](float x,float y) {
        const float t=(x-304)/(x<304?290.0f:262.0f);
        float v=160+55*std::max(0.0f,1-std::pow(std::fabs(t),1.4f));
        if (y<2) v=208-y*10; else if (y<8) v+=std::max(0.f,6-y)*3;
        if (y>=118) v=196;
        return grey(int(v));
    });
    if (blades_gamercard) {SDL_Rect to{151,163,0,0};SDL_BlitSurface(blades_gamercard,nullptr,screen,&to);}
    ring(148,160,572,127,11,3,0x7a7a7a);
    ring(148,160,572,127,11,1,0x6e6e6e);
    rect(169,181,86,86,0x505050);
    draw_gamerpic(170,182,84,84);
    const SDL_Color ink{22,22,22,255};
    if (blades_gscore) {blit_contained(blades_gscore,{271,181,29,29});text("amerscore",300,173,era_text,ink);}
    else text("Gamerscore",272,173,era_text,ink);
    const std::string points=(trophies.known?std::to_string(trophies.points()):std::string{});
    text(points,703-text_width(points,era_text),173,era_text,ink);
    text("Status",271,221,era_text,ink);
    text("Local",703-text_width("Local",era_text),221,era_text,ink);
    if (model.guide_blade==dash::BladeHome) {
        // Items: 0-2 the Community buttons (messages, avatar, games),
        // 3 PS5 Home, 4 Personal Settings, 5 music, 6 the XBOX 360 button.
        const int item=std::clamp(model.guide_item[dash::BladeHome],0,6);
        frame(149,298,568,124,11);
        text("Community",178,298,era_text,ink);
        const int bx[]={166,349,531},bw[]={171,171,170};
        const char *icons[]={"ico_32x_Mail","ico_32x_Friend","ico_32x_Players"};
        for (int i=0;i<3;i++) {
            button(bx[i],350,bw[i],55,item==i);
            if (SDL_Surface *icon=art(icons[i])) {SDL_Rect to{bx[i]+30,357,40,40};blit_quality(icon,nullptr,screen,&to,true);}
        }
        const char *rows[]={"PS5 Home","Personal Settings"};
        for (int i=0;i<2;i++) {
            const int top=433+i*61;
            if (item==3+i) {
                glow(158,top+1,550,58,8);
                fill(158,top+1,550,58,8,0xe6f0d8);
                for (int row=0;row<58;row+=2) rect(158+inset(8,row,58),top+1+row,550-2*inset(8,row,58),2,era_chrome::mix(0xf0f7e6,0xbdd3a2,row/57.0f));
            }
            text(rows[i],177,430+i*61,era_text,ink);
        }
        groove(431);groove(492);groove(553);
        frame(149,565,568,141,11);
        if (item==5) glow(149,565,568,141,11);
        const int mx[]={163,294,398,502,606},mw[]={123,95,96,96,96};
        const char *controls[]={"btn_32x_Play","btn_32x_Last","btn_32x_Next","btn_32x_Pause",nullptr};
        for (int i=0;i<5;i++) {
            button(mx[i],579,mw[i],56,false);
            SDL_Surface *icon=controls[i]?art(controls[i]):music_controls[5];
            if (icon) {
                SDL_SetSurfaceColorMod(icon,235,235,235);
                SDL_SetSurfaceAlphaMod(icon,item==5?230:150);
                SDL_Rect to{mx[i]+(mw[i]-34)/2,590,34,34};blit_quality(icon,nullptr,screen,&to,true);
                SDL_SetSurfaceAlphaMod(icon,255);SDL_SetSurfaceColorMod(icon,255,255,255);
            }
        }
        button(162,645,544,49,false);
        {
            // While a song plays the bar names it, with its cover beside.
            const music::Song *song=model.now_playing>=0?music_player.current_song():nullptr;
            SDL_Surface *cover=song?song_cover(song->path):nullptr;
            if (cover) {rect(171,651,38,38,0x303030);blit_contained(cover,{172,652,36,36});}
            const int tx=cover?222:185;
            text(fit(model.now_playing>=0?model.now_playing_title:"Select Music",era_text,700-tx),tx,644,era_text,ink);
        }
        // XBOX 360 button: white face, inner fall at the top and foot, darker left.
        if (!blades_xbox) blades_xbox=make_face(559,116,8,[](float x,float y) {
            float v=248;
            if (y<16) v=std::min(v,172+76*std::pow(y/16.0f,0.6f));
            if (y>100) v=std::min(v,248-54*std::pow((y-100)/15.0f,1.6f));
            v=std::min(v,186+62*std::min(1.0f,std::pow(x/250.0f,0.8f)));
            if (x>540) v=std::min(v,248-50*(x-540)/18.0f);
            return grey(int(v));
        });
        if (item==6) glow(151,718,565,122,10);
        if (blades_xbox) {SDL_Rect to{154,721,0,0};SDL_BlitSurface(blades_xbox,nullptr,screen,&to);}
        ring(151,718,565,122,10,3,0x7b7b7b);
        if (SDL_Surface *splash=art("splash_360")) {
            SDL_Rect orb{206,4,244,258},word{1,297,671,116};
            SDL_Rect orb_to{220,740,86,76},word_to{321,752,309,50};
            blit_quality(splash,&orb,screen,&orb_to,true);
            blit_quality(splash,&word,screen,&word_to,true);
        }
        blades_hints(884);
        return;
    }
    }
    const SDL_Color ink{22,22,22,255};
    // Other pages (Personal Settings and the rest): a framed list box.
    const auto entries=model.blade_entries(model.guide_blade);
    const int n=int(entries.size()),item=std::clamp(model.guide_item[model.guide_blade],0,std::max(0,n-1));
    const int first=std::clamp(item-6,0,std::max(0,n-7));
    frame(149,298,568,541,11);
    text(model.guide_blade==dash::BladeSettings?std::string("Personal Settings"):model.blade_title(model.guide_blade),178,298,era_text,ink);
    for (int row=0;row<7 && first+row<n;row++) {
        const int top=350+row*68;
        if (first+row==item) {
            glow(160,top,548,62,8);
            for (int r=0;r<62;r+=2) rect(160+inset(8,r,62),top+r,548-2*inset(8,r,62),2,era_chrome::mix(0xf0f7e6,0xbdd3a2,r/61.0f));
        } else if (row+1<7 && first+row+1<n) {rect(168,top+65,532,2,0x858585);rect(168,top+67,532,1,0xb0b0b0);}
        const bool recorder=entries[size_t(first+row)].action==dash::GuideAction::RecordScreen;
        text(fit(entries[size_t(first+row)].label,era_text,recorder?320:500),178,top+8,era_text,ink);
        if(recorder)recording_transport(522,top,62,first+row==item,48);
    }
    if (first>0) chevron(684,304,false);
    if (first+7<n) chevron(684,808,true);
    if (!from_scene) blades_hints(884);
}
void body(Uint32 now) {
    if (model.era==0) {blades_panel();return;}
    if (model.era>=1 && guide_scene::draw_era(model.era)) return;
    if (model.era>=2) {flat_body();return;}
    const int focus=model.guide_blade;
    // Farthest slabs first so nearer ones overlap them; the open blade is under the panel.
    for (int slot=3;slot>=0;slot--) for (int d=0;d<dash::BladeCount;d++) {
        if (d==focus) continue;
        int s=d<focus?focus-1-d:d-focus-1;
        if (s==slot) slab(d);
    }
    if (blade_dark) {
        // Blade_dark.png, the open blade: 9 px of shadow round it, so the box
        // grows by 9 * 1.5 to keep the blade itself on the measured edges.
        nine_slice(blade_dark,PX-14,PY-14,PW+28,PH+28,24,36);
        // Flat, as the capture: the tab column 24303c, the list 2c3c49.
        rect(PX+5,PY+6,TAB-5,PH-12,0x24303c);
        rect(PX+TAB+2,PY+7,PW-TAB-9,PH-14,0x2c3c49);
        shade(PX+TAB+2,PY+7,PW-TAB-9,5,0xffffff,42);
        rect(PX+TAB,PY+8,1,PH-16,0x2c3743);
    } else {
        rounded(PX+8,PY+12,PW,PH,10,0x05070a,0x05070a);
        rounded(PX-1,PY-1,PW+2,PH+2,10,0x55627a,0x2a3346);
        rounded(PX,PY,PW,PH,9,0x26344f,0x1b2639);
        rounded(PX,PY,TAB,PH,9,0x1d293f,0x151e2f);
        rect(PX+TAB-9,PY,9,PH,0x1a2539);
        rect(PX+TAB,PY,1,PH,0x0f1522);
    }
    if (ring_of_light) {
        // The Ring of Light, player one's quarter lit.
        constexpr int d=46;
        SDL_Rect r{PX+(TAB-d)/2,SPIN_Y+SPIN_D/2-d/2,d,d};
        SDL_SetSurfaceAlphaMod(ring_of_light,text_alpha);
        blit_quality(ring_of_light,nullptr,screen,&r);
    } else spinner(PX+TAB/2,SPIN_Y+SPIN_D/2);
    if (SDL_Surface *label=tab_labels[focus]) {
        // Centred on the panel's mid-height, never above the arc's foot.
        const int top=SPIN_Y+SPIN_D+14, run=PY+PH-14-top;
        const int h=std::min(render_density::logical_h(label),run);
        const int y=std::clamp(PY+PH/2-h/2,top,top+run-h);
        SDL_Rect r{PX+(TAB-render_density::logical_w(label))/2,y,render_density::logical_w(label),h};
        SDL_Rect src{0,0,render_density::logical_w(label),r.h};
        SDL_SetSurfaceAlphaMod(label,text_alpha);
        SDL_BlitSurface(label,&src,screen,&r);
    }
    const std::vector<dash::GuideEntry> entries=model.blade_entries(focus);
    const int n=int(entries.size());
    const int item=std::clamp(model.guide_item[focus],0,std::max(0,n-1));
    const int first=std::clamp(item-(ROWS-1),0,std::max(0,n-ROWS));
    const Uint8 saved=text_alpha;
    const Uint32 since=now-blade_at;
    if (since<BLADE) text_alpha=Uint8(saved*since/BLADE);
    for (int r=0;r<ROWS && first+r<n;r++) {
        const int i=first+r, y=ITEM_TOP+r*ITEM_PITCH;
        if (i==item && text_alpha>=96) { // the bar arrives with the list, not before it
            // The 2008 Guide bar (nxe-guide capture, 1080p): a glossy upper
            // half 8bbf37 -> 7ab51f over a flat 5a9d02 lower half.
            rounded(ITEM_X-2,y-2,BAR_W+4,ITEM_H+4,4,0x4e8e17,0x3b7410);
            rounded(ITEM_X,y,BAR_W,ITEM_H,3,0x5a9d02,0x5a9d02);
            gradient(ITEM_X+2,y+1,BAR_W-4,ITEM_H/2,0x8bbf37,0x7ab51f);
        }
        int label_x=ITEM_X+12;
        if (entries[size_t(i)].action==dash::GuideAction::Music && model.now_playing>=0)
            if (const music::Song *song=music_player.current_song())
                if (SDL_Surface *cover=song_cover(song->path)) {
                    rect(ITEM_X+8,y+4,ITEM_H-8,ITEM_H-8,0x1c2430);
                    blit_contained(cover,{ITEM_X+9,y+5,ITEM_H-10,ITEM_H-10});
                    label_x=ITEM_X+ITEM_H+8;
                }
        const std::string label=fit(entries[size_t(i)].label,item_font,BAR_W-80-(label_x-ITEM_X-12));
        text(label,label_x+2,y+9,item_font,{0,0,0,110});
        text(label,label_x,y+7,item_font,{255,255,255,255});
        if (focus==dash::BladeHome) row_extra(entries[size_t(i)],ITEM_X+BAR_W,y,ITEM_H,i==item);
    }
    if (text_alpha>=96 && first>0) chevron(PX+PW-34,PY+14,false);
    if (text_alpha>=96 && first+ROWS<n) chevron(PX+PW-34,PY+PH-26,true);
    text_alpha=saved;
    draw_gamerpic(PIC_X,PIC_Y,PIC_W,PIC_H);
    std::string clock=clock_text();
    if (const auto at=clock.rfind(' ');at!=std::string::npos) clock.insert(at," "); // "11:49  PM", as captured
    text(clock,CLOCK_RIGHT-text_width(clock,clock_font),CLOCK_Y,clock_font,{255,255,255,255});
    // The controller's battery and the controller itself, top left (nxe-guide
    // capture: a 51 x 28 outline with three bars at 559,257; the pad at 626).
    // Battery level is unknown; retain the controller icon, omit the gauge.
    {
        auto pad=blades_system_icons.find("ico_64x_controller");
        if (pad!=blades_system_icons.end()) {
            SDL_SetSurfaceColorMod(pad->second,205,205,205);
            blit_contained(pad->second,{624,252,48,40});
            SDL_SetSurfaceColorMod(pad->second,255,255,255);
        }
    }
    int x=PROMPT_X;
    for (const auto &hint:model.hints()) {
        glyph(hint.button,x,PROMPT_Y,PROMPT_D);
        x+=PROMPT_D-3; // the capture's captions start where the orb ends
        text(hint.label,x+2,PROMPT_Y-2,prompt_font,{0,0,0,140});
        x+=text(hint.label,x,PROMPT_Y-4,prompt_font,{255,255,255,255});
        x+=46;
    }
}
// Called every frame: dims the hub and draws the Guide opening, open or closing.
void draw(Uint32 now, float dt) {
    if (model.guide && !was_open) {
        opened_at=now; closing=false; shown_blade=model.guide_blade; blade_at=0;
        for (int d=0;d<dash::BladeCount;d++) {slab_x[d]=PX; slab_s[d]=1;} // slide out from under the panel
    }
    if (!model.guide && was_open) {
        // Leaving for another screen hands over at once; closing to the hub animates.
        closing=!model.controller_map && !model.details && model.viewer==dash::Viewer::None && running;
        closed_at=now;
    }
    was_open=model.guide;
    if (!model.guide && !closing) return;
    if (model.guide_blade!=shown_blade) {shown_blade=model.guide_blade; blade_at=now;}
    float scale=1, alpha=1, content=1, dim=1;
    if (model.guide) {
        const Uint32 t=now-opened_at;
        if (t<GROW) {float e=1-float(t)/GROW; e=1-e*e; scale=0.1f+0.95f*e; alpha=e; content=0;}
        else if (t<SETTLE) {scale=1.05f-0.05f*float(t-GROW)/float(SETTLE-GROW); content=std::min(1.0f,float(t-GROW)/CONTENT);}
        else content=std::min(1.0f,float(t-GROW)/CONTENT);
        dim=std::min(1.0f,float(t)/DIM_IN);
        // The stacks come out once the panel is up, and slide on a blade change
        // (about 170 ms to settle, easing out).
        const float r=std::min(1.0f,dt*22.0f);
        if (t>=GROW) for (int d=0;d<dash::BladeCount;d++) {
            float x,s; place(d,model.guide_blade,x,s);
            slab_x[d]+=(x-slab_x[d])*r; slab_s[d]+=(s-slab_s[d])*r;
        }
    } else {
        const Uint32 t=now-closed_at;
        // The original scenes end their own close (HalfToClosed runs up to 29 frames).
        if (t>=POP+SHRINK && !guide_scene::available(model.era)) {closing=false; return;}
        content=0;
        if (t<POP) scale=1+0.05f*float(t)/POP;
        else {float u=float(t-POP)/SHRINK; scale=1.05f-0.95f*u; alpha=1-u;}
        dim=std::max(0.0f,1-float(t)/DIM_OUT);
    }
    shade(0,0,W,H,0x000000,Uint8(DIM*dim));
    // The original scenes animate themselves (ClosedToHalf on opening).
    if (model.guide && guide_scene::available(model.era)) {body(now);return;}
    if (!model.guide && guide_scene::available(model.era)) {if (!guide_scene::draw_closing(model.era,now==closed_at)) closing=false;return;}
    if (model.era==0) content=1;
    const Uint8 saved=text_alpha;
    text_alpha=Uint8(255*content);
    if (scale==1 && alpha>=1) {body(now); text_alpha=saved; return;}
    if (!layer) {
        layer=render_density::create(W,H);
        if (layer) SDL_SetSurfaceBlendMode(layer,SDL_BLENDMODE_BLEND);
    }
    if (!layer) {text_alpha=saved; return;}
    // Only this source box is composited below. Keep the logical full-canvas
    // origin so every painter sees identical coordinates, but avoid clearing
    // and drawing the unused majority of the physical4K surface.
    const SDL_Rect box=model.era==0?SDL_Rect{0,0,860,H}:SDL_Rect{300,200,1320,640};
    SDL_Rect saved_layer_clip{};
    SDL_GetClipRect(layer,&saved_layer_clip);
    SDL_SetClipRect(layer,&box);
    x21layers::Scope gpu_layer;const bool recording_layer=gpu_layer.begin(screen,layer,box);
    if(!recording_layer&&x21hook::recording(screen))x21hook::active->packet.unsupported.push_back("guide_layer_unavailable");
    if(!recording_layer||x21hook::audit_cpu()){x21hook::Pause audit(recording_layer);SDL_FillRect(layer,&box,SDL_MapRGBA(layer->format,0,0,0,0));}
    SDL_Surface *target=screen;
    screen=layer;
    body(now);
    screen=target;
    SDL_SetClipRect(layer,&saved_layer_clip);
    text_alpha=saved;
    // Scale the panel and everything on it about the panel's centre.
    const float cx=PX+PW/2.0f, cy=PY+PH/2.0f;
    SDL_Rect to{int(cx+(box.x-cx)*scale),int(cy+(box.y-cy)*scale),int(box.w*scale),int(box.h*scale)};
    if (model.era==0) {
        const float progress=model.guide?std::min(1.0f,float(now-opened_at)/230):std::max(0.0f,1-float(now-closed_at)/250);
        to={int(-860*(1-progress)*(1-progress)),0,860,H};
    }
    if(recording_layer){gpu_layer.end(to,alpha);if(!x21hook::audit_cpu())return;}
    x21hook::Pause audit_layer(x21hook::recording(screen));
    SDL_SetSurfaceAlphaMod(layer,Uint8(255*std::clamp(alpha,0.0f,1.0f)));
    SDL_Rect from=box;
    SDL_BlitScaled(layer,&from,screen,&to);
}
void free_all() {
    for (auto *&s:flat_labels) if (s) {SDL_FreeSurface(s);s=nullptr;}
    SDL_FreeSurface(metro_games_label);metro_games_label=nullptr;
    for (auto *&s:tab_labels) if (s) {SDL_FreeSurface(s);s=nullptr;}
    for (auto *&s:slab_labels) if (s) {SDL_FreeSurface(s);s=nullptr;}
    if (layer) {SDL_FreeSurface(layer);layer=nullptr;}
    SDL_FreeSurface(blades_ring);blades_ring=nullptr;SDL_FreeSurface(blades_spine);blades_spine=nullptr;blades_spine_name.clear();
    SDL_FreeSurface(blades_gamercard);blades_gamercard=nullptr;SDL_FreeSurface(blades_xbox);blades_xbox=nullptr;
    for (TTF_Font *f:{item_font,tab_font,slab_font,prompt_font,clock_font}) if (f) TTF_CloseFont(f);
}
}
