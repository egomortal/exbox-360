// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_avatar_stage() {
    gradient(0,0,W,H,0xe8efe0,0x9fbf7c);
    if (glow) {
        SDL_Rect spot{W/2-520,40,1040,900};
        SDL_SetSurfaceAlphaMod(glow,140);
        SDL_BlitScaled(glow,nullptr,screen,&spot);
    }
    // A soft floor shadow under the feet.
    if (glow) {
        SDL_Rect floor{W/2-230,930,460,70};
        SDL_SetSurfaceColorMod(glow,20,40,10);
        SDL_SetSurfaceAlphaMod(glow,150);
        SDL_BlitScaled(glow,nullptr,screen,&floor);
        SDL_SetSurfaceColorMod(glow,255,255,255);
    }
    text("My Avatar",95,70,large,{40,60,30,255});
    text(model.player,97,135,regular,{70,96,58,255});
    if (edited_avatar::fullbody) {const int h=860,w=edited_avatar::fullbody->w*h/std::max(1,edited_avatar::fullbody->h);SDL_Rect at{(W-w)/2,960-h,w,h};blit_quality(edited_avatar::fullbody,nullptr,screen,&at,false);return;}
    if (look3d::active()) {
        const Uint32 now=SDL_GetTicks();
        look3d::ease(now,false);
        if (SDL_Surface *f=look3d::render(look3d::stage,580,920,look3d::stage_camera(),now,33)) {
            SDL_Rect r{(W-580)/2,40,580,920};
            SDL_BlitSurface(f,nullptr,screen,&r);
        }
        return;
    }
    if (!avatar::installed()) {
        const std::string a="No avatar installed yet.";
        const std::string b=look3d::ready?"Press Y to make one in the Avatar Editor.":
                            "Copy assets/avatar/kit.nxak to the console (deploy.py) for the Avatar Editor.";
        text(a,(W-text_width(a,large))/2,H/2-60,large,{40,60,30,255});
        text(b,(W-text_width(b,regular))/2,H/2+10,regular,{70,96,58,255});
        return;
    }
    const Uint32 now=SDL_GetTicks();
    SDL_Surface *f=avatar::wave_frame(now);
    if (!f) f=avatar::turn.count()?avatar::turn.frame(model.viewer_item):avatar::idle.frame(int(now*15u/1000u));
    if (f) {
        const int h=860, w=f->w*h/std::max(1,f->h);
        SDL_Rect r{(W-w)/2,960-h,w,h};
        SDL_BlitScaled(f,nullptr,screen,&r);
    }
}
// A small "playing" triangle.
void play_mark(int x, int y, int size, unsigned hex) {
    for (int i=0;i<size;i++) rect(x,y+i,std::max(1,int(float(std::min(i,size-1-i)*2+1)*0.866f)),1,hex);
}
// The Music Library's shared pieces: the same rows, covers and player state
// in each era's own dress (draw_music picks the era).
namespace music_page {
constexpr const char *kViews[]={"Songs","Albums","Artists","Genres","Saved Playlists"}; // music/1003_IndividualDevice.xur, every build
constexpr const char *kKinds[]={"Song","Album","Artist","Genre","Playlist"};     // music/Strings.xus (1888) edit fields
// Metro's dress: the same words in lower case, as its pivots and headers.
std::string lower(std::string s) {for (char &c:s) c=char(std::tolower(static_cast<unsigned char>(c))); return s;}
int view() {return std::clamp(model.music_view,0,dash::kMusicViews-1);}
unsigned hex(SDL_Color c) {return unsigned(c.r)<<16|unsigned(c.g)<<8|c.b;}
const music::Song &first(const tunes::Row &row) {return music_songs[size_t(row.songs[0])];}
bool holds(const tunes::Row &row, const music::Song *playing) {
    if (!playing) return false;
    if (!row.cover_path.empty() && row.songs.empty()) return row.cover_path==playing->path; // a Now Playing row
    for (int s:row.songs) if (music_songs[size_t(s)].path==playing->path) return true;
    return false;
}
// A row worth drawing: a song or group, an option, a playlist song or a picture.
bool visible_row(const tunes::Row &row) {return !row.songs.empty() || !row.label.empty();}
const std::string &name(const tunes::Row &row) {return !row.label.empty()?row.label:row.group?row.name:first(row).title;}
// A song's second line: its artist, or (listing an artist's songs) its album.
const std::string &detail(const tunes::Row &row) {
    if (!row.label.empty()) return row.sub;
    if (row.group) return row.detail;
    const music::Song &song=first(row);
    return view()==2?song.album:song.artist;
}
// The first of `shown` rows so that `item` stays in view.
int top(int item, int n, int shown) {return std::clamp(item-shown/2,0,std::max(0,n-shown));}
// The focused row (null when the list is empty).
const tunes::Row *focused() {
    const int n=std::min(model.viewer_count(),int(tunes::rows.size()));
    if (n<=0) return nullptr;
    const tunes::Row &row=tunes::rows[size_t(std::clamp(model.viewer_item,0,n-1))];
    return visible_row(row)?&row:nullptr;
}
// The repeat sign: a loop with an arrowhead on its top run, a bar in it for one song.
void repeat_glyph(int x, int y, int s, unsigned ink, bool one) {
    const int t=std::max(2,s/11), top=y+s/4, h=s/2;
    outline(x+t,top,s-2*t-s/6,h,t,ink);
    play_mark(x+s-2*t-s/6-s/12,top+t/2-s/7,s*2/7,ink);
    if (one) rect(x+s/2-t/2-s/12,top+t+2,t+1,h-2*t-4,ink);
}
// The shuffle sign, when the 1888 button art is missing: two crossing arrows.
void shuffle_glyph(int x, int y, int s, unsigned ink, bool shuffled) {
    const int t=std::max(2,s/11), a=y+s/4, b=y+s*3/4-t;
    for (int i=0;i<s*3/4;i++) {
        const int k=shuffled?i*(b-a)/(s*3/4):0;
        rect(x+i,a+k,1,t,ink);
        rect(x+i,b-k,1,t,ink);
    }
    play_mark(x+s*3/4-1,a+t/2-s/7,s*2/7,ink);
    play_mark(x+s*3/4-1,b+t/2-s/7,s*2/7,ink);
}
// Shuffle and repeat as they stand: two signs with their words, bright when
// on, dim when off (both opaque: the signs are drawn, not blended).
// `blades` draws the words in the Blades' tracked type.
int order_state(int x, int y, int size, TTF_Font *font, SDL_Color on, SDL_Color off, bool blades=false) {
    const bool shuffled=model.music_shuffle;
    const int repeat=std::clamp(model.music_repeat,0,2);
    const int ty=y+(size-TTF_FontHeight(font))/2;
    auto words=[&](const char *label,int at,SDL_Color ink) {
        return blades?blades_type::draw(label,at,ty,font,ink):text(label,at,ty,font,ink);
    };
    int at=x;
    const SDL_Color s=shuffled?on:off;
    if (SDL_Surface *art=music_order_art[shuffled?1:0]) {
        SDL_SetSurfaceColorMod(art,s.r,s.g,s.b);SDL_SetSurfaceAlphaMod(art,s.a);
        blit_contained(art,{at,y,size,size});
        SDL_SetSurfaceColorMod(art,255,255,255);SDL_SetSurfaceAlphaMod(art,255);
    } else shuffle_glyph(at,y,size,hex(s),shuffled);
    at+=size+8;
    at+=words(shuffled?"Shuffle":"In Order",at,s)+34; // Ex-Box addition: no 360 original
    const SDL_Color r=repeat?on:off;
    repeat_glyph(at,y,size,hex(r),repeat==2);
    at+=size+8;
    constexpr const char *repeats[]={"Repeat Off","Repeat All","Repeat One"}; // Ex-Box addition: no 360 original
    at+=words(repeats[repeat],at,r);
    return at-x;
}
// Where the song is: the clock beside its progress bar.
std::string clock_line() {
    const double at=music_player.elapsed_seconds(), length=music_player.length_seconds();
    return music::clock(at)+(length>0?" / "+music::clock(length):"");
}
// "Paused", or why the music is held, or nothing.
std::string pause_line() {
    if (!music_player.paused()) return {};
    return model.music_paused?"Paused":"Held for the video"; // Ex-Box addition: no 360 original
}
// Small dark triangles for more rows above or below (the Blades' ink).
void arrow(int x, int y, bool down, unsigned ink) {
    for (int i=0;i<9;i++) {const int half=down?8-i:i; rect(x+8-half,y+i,half*2+1,1,ink);}
}

// ------------------------------------------------------- NXE, Kinect, Metro
// music/1003_IndividualDevice and 1028_NowPlaying in the era's skin (9199;
// 13604 for Kinect, whose scenes 15574 and 17559 keep): Label_Head over the
// dashboard's background, one BackgroundPanel holding the views as
// btn_Music_arrow rows (420 x 47 at 720p, a 32 px icon), the songs as
// btn_3rdList rows (47 px: a name and a second column at 305/420) and Now
// Playing in Label_MetaHead_1line_white / Label_Sub_white_music. 13604 on:
// the views on the light panel in 0x656d72, the songs and Now Playing on the
// grey RightPanelShader in white, the focus the GreenHighlight bar; 9199: all
// on the dark 9-grid, white, the focus greenBox1's rim.
void draw_skinned() {
    namespace sk=media_skin;
    const bool light=sk::light(), metro=model.era==3;
    constexpr int px=120, py=180, pw=1680, ph=770;
    sk::scene_background();
    sk::background_panel(px,py,pw,ph);
    TTF_Font *head=light?(era_text?era_text:large):large;          // Label_Head, 22 pt
    TTF_Font *row_font=guide::item_font?guide::item_font:regular;   // btn_* text_Label, 18 pt
    TTF_Font *col_font=era_text?era_text:regular;                   // btn_3rdList text_Label1, 18 pt
    TTF_Font *meta_head=era_text?era_text:regular;                  // Label_MetaHead_1line_white
    const SDL_Color ink=sk::kInk, sub=light?sk::kInk:sk::kSub;      // Label_Sub_white: 9199 at 0.7
    // The heading: the album, artist or genre opened, else the library.
    const int v=view();
    const bool opened=!tunes::group_name.empty();
    const bool page=model.music_page!=dash::Model::MusicLibrary;
    const std::string kind=page?tunes::page_kind:kKinds[v];
    const int hy=py-(light?66:80);
    if (metro) {
        std::string app=opened && !kind.empty()?std::string("MUSIC  \xc2\xb7  ")+kind:"MUSIC";
        for (char &c:app) c=char(std::toupper(static_cast<unsigned char>(c)));
        const int w=text(app,px+30,hy,head,ink);
        if (opened) text(fit(tunes::group_name,head,1000),px+30+w+40,hy,head,ink);
    } else if (opened) {
        const int w=text(fit(tunes::group_name,head,1000),px+30,hy,head,ink);
        text(kind,px+30+w+30,hy+(TTF_FontHeight(head)-TTF_FontHeight(regular))-4,regular,sub);
    } else text("Music Library",px+30,hy,head,ink); // homepage/strings.xus (9199, 13604)
    // The views: btn_Music_arrow, the one in use marked by the arrow it shows
    // when focused (the focus itself is in the songs).
    constexpr int vx=px+30, vy=py+30, vw=450, pitch=70;
    for (int i=0;i<dash::kMusicViews;i++) {
        const int y=vy+i*pitch;
        const bool here=i==v;
        sk::rules(vx,y,vw,pitch);
        SDL_Color c=light?(here?sk::kRowSel:sk::kRowInk):ink;
        if (here && !light) shade(vx+3,y+5,vw-6,(pitch-8)/2,0xebebeb,22); // Highlight14's wash
        if (SDL_Surface *icon=music_kind_art[i]) {
            if (light) SDL_SetSurfaceColorMod(icon,c.r,c.g,c.b);
            blit_contained(icon,{vx+(light?15:20),y+12,48,48});
            SDL_SetSurfaceColorMod(icon,255,255,255);
        }
        const std::string label=metro?lower(kViews[i]):kViews[i];
        const int tx=vx+(light?121:126);
        if (light) text(label,tx,y+12,row_font,c); else sk::shadowed(label,tx,y+12,row_font,c);
        if (here) play_mark(vx+vw-40,y+23,24,hex(c)); // arrowB
    }
    // The songs: List_3rdList on the right.
    constexpr int sx=px+540, lx=sx+30, ly=py+30, lw=690, rows=dash::kMusicRows;
    if (light) sk::panel_shader(sx,py,pw-(sx-px),ph);
    const int n=model.viewer_count();
    const int item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const music::Song *playing=music_player.current_song();
    if (n>0 && !tunes::rows.empty()) {
        if (!metro) {
            const std::string where=std::to_string(item+1)+" of "+std::to_string(n); // Label_SubRight
            TTF_Font *f=light?small:regular;
            text(where,lx+lw-text_width(where,f),hy+(TTF_FontHeight(head)-TTF_FontHeight(f))-4,f,light?ink:sk::kSub);
        }
        const int first_row=top(item,n,rows);
        for (int r=0;r<rows && first_row+r<n;r++) {
            const int i=first_row+r, y=ly+r*pitch;
            const tunes::Row &row=tunes::rows[size_t(std::min(i,int(tunes::rows.size())-1))];
            if (!visible_row(row)) continue;
            const bool on=i==item;
            if (on) {
                if (light) sk::green_highlight(lx,y-1,lw,pitch+1);
                else sk::green_box(lx,y+2,lw,pitch-2);
            } else sk::rules(lx,y,lw,pitch);
            if (holds(row,playing)) play_mark(lx+16,y+25,20,on||light?0xebebeb:sk::nxe_green());
            if (SDL_Surface *cover=tunes::row_cover(row)) {
                rect(lx+43,y+9,52,52,0x10161c);
                blit_contained(cover,{lx+44,y+10,50,50});
            }
            const int tx=lx+108, cx=lx+lw*305/420; // text_Label1 at 305 of 420
            const std::string &second=detail(row);
            const std::string title=fit(name(row),row_font,(second.empty()?lx+lw-20:cx-14)-tx);
            if (light) text(title,tx,y+12,row_font,ink); else sk::shadowed(title,tx,y+12,row_font,ink);
            if (!second.empty()) {
                const std::string col=fit(second,col_font,lw-(cx-lx)-12);
                if (light) text(col,cx,y+14,col_font,{235,235,235,178}); // text_Label1: 0.7
                else sk::shadowed(col,cx,y+14,col_font,ink);
            }
        }
        if (first_row>0) guide::chevron(lx+lw-24,ly-20,false);
        if (first_row+rows<n) guide::chevron(lx+lw-24,ly+rows*pitch+6,true);
    } else {
        const std::string empty=dash::media_text::music_empty(v);
        text(metro?lower(empty):empty,lx,ly+12,row_font,ink);
        // Ex-Box addition: no 360 original (where the PS5 finds music)
        text(fit("Copy MP3, FLAC, Ogg or WAV files to /data/nxe/music on the PS5 (FTP),",small,lw),lx,ly+72,small,sub);
        text(fit("or to a Music folder on a USB drive, then open Music again.",small,lw),lx,ly+107,small,sub);
    }
    // Now Playing (music/1028_NowPlaying's labels), right of the songs.
    constexpr int nx=lx+lw+40, ny=py+30, nw=px+pw-30-nx;
    if (!light) shade(nx-20,py+24,2,ph-48,0xebebeb,26);
    const std::string np=dash::media_text::now_playing(model);
    text(metro?lower(np):np,nx,ny,meta_head,ink);
    SDL_Surface *cover=playing?song_cover(playing->path):nullptr;
    constexpr int cs=300, cy=ny+64;
    if (cover) {
        rect(nx+(nw-cs)/2-2,cy-2,cs+4,cs+4,0x10161c);
        blit_contained(cover,{nx+(nw-cs)/2,cy,cs,cs});
    } else {
        SDL_Surface *art=themed_icons[dash::IconMusic]?themed_icons[dash::IconMusic]:card_icons[dash::IconMusic];
        if (model.era>=2) art=card_icons[dash::IconMusic]?card_icons[dash::IconMusic]:art;
        if (art) blit_contained(art,{nx+(nw-240)/2,cy+10,240,280});
    }
    const SDL_Color off=light?SDL_Color{0xb4,0xb8,0xbb,255}:SDL_Color{120,128,134,255}; // disabled: 0.4
    order_state(nx,py+ph-70,34,small,ink,off);
    const int ty=cy+cs+24;
    if (!playing) {
        if (metro) {
            // Ex-Box addition: no 360 original
            text("nothing playing",nx,ty,row_font,ink);
            text("Choose a song and press A.",nx,ty+56,small,sub);
            return;
        }
        // Ex-Box addition: no 360 original (the 360 showed an empty playlist)
        text("Nothing is playing.",nx,ty,row_font,ink);
        text("Choose a song and press A.",nx,ty+56,small,sub);
        text("It keeps playing while you use",nx,ty+90,small,sub);
        text("the dashboard.",nx,ty+124,small,sub);
        return;
    }
    const int badge=custom_scene::draw_equalizer(nx,ty)?86:0;
    text(fit(playing->title,row_font,nw-badge),nx+badge,ty,row_font,ink);           // Label_Body_1line
    int y=ty+54;
    if (metro) {
        const std::string by=playing->artist+(playing->album.empty()?"":(playing->artist.empty()?"":"  \xc2\xb7  ")+playing->album);
        if (!by.empty()) text(fit(by,regular,nw),nx,y,regular,sub);
        y+=44;
    } else {
        if (!playing->artist.empty()) text(fit(playing->artist,regular,nw),nx,y,regular,sub); // Label_Sub_white_music
        y+=40;
        if (!playing->album.empty()) text(fit(playing->album,regular,nw),nx,y,regular,sub);
        y+=44;
    }
    const double length=music_player.length_seconds();
    sk::progress_bar(nx,y+8,nw,12,length>0?music_player.elapsed_seconds()/length:0.0);
    text(clock_line(),nx,y+30,regular,sub);
    const std::string held=pause_line();
    if (!held.empty()) text(held,nx+nw-text_width(held,regular),y+30,regular,ink);
}

// ------------------------------------------------------------------- Blades
// The Media blade itself, as the hub draws it: its chrome and rails, the
// heading, rows like the blade's items, and the meta panel on the right
// (what is focused, and a gamercard-style Now Playing card).
constexpr int kBladesMedia=3; // dash::kBladesChannels: Apps, Friends, Games, Media, MyXbox
void draw_blades() {
    TTF_Font *body=blades_body?blades_body:era_text;
    TTF_Font *caption=blades_caption?blades_caption:regular;
    const int active=kBladesMedia;
    const auto &p=era_chrome::palettes[active];
    const SDL_Color ink{15,15,15,230}, soft{15,15,15,150};
    const int early=model.dashboard_build<5759?1:0;
    era_chrome::select_revision(model.dashboard_build);
    if (SDL_Surface *chrome=early?era_chrome::blades_early[active]:era_chrome::blades[active]) SDL_BlitSurface(chrome,nullptr,screen,nullptr);
    else gradient(0,0,W,H,p.mid,p.light);
    const auto layout=era_chrome::layout(active,early!=0);
    if (!era_chrome::has_source_labels(model.dashboard_build,active))
        for (int i=early;i<5;i++) {
            const auto label=era_chrome::rail_label(active,i,early!=0);
            if (SDL_Surface *letters=blades_labels[blades_rails::family(model.dashboard_build)][i==active][i]) {
                SDL_Rect at{label.center-(letters->w+1)/2,label.top,0,0};
                SDL_BlitSurface(letters,nullptr,screen,&at);
            }
        }
    const int content_x=layout.content, right_x=layout.right, face_right=layout.edge;
    const int right_w=face_right-right_x-52;
    // The heading, and on its right which view the triggers turn to.
    const int v=view();
    const SDL_Color light{235,235,235,250};
    const std::string heading=tunes::group_name.empty()?"Music":tunes::group_name; // music/1000_MusicMain.xur, mediabla/mediaSignedIn.xur
    blades_type::draw(blades_type::fit(heading,blades_heading?blades_heading:body,right_x-content_x+right_w/2),content_x+10,62,blades_heading?blades_heading:body,light);
    {
        int at=face_right-52;
        if (model.music_page!=dash::Model::MusicLibrary) {
            const std::string &label=tunes::page_kind;
            blades_type::draw(label,at-blades_type::width(label,caption),72,caption,light);
        } else if (tunes::group_name.empty()) {
            at-=40;glyph(xb::RT,at,78,40);
            const std::string label=kViews[v];
            at-=12+blades_type::width(label,caption);
            blades_type::draw(label,at,72,caption,light);
            at-=52;glyph(xb::LT,at,78,40);
        } else {
            const std::string label=kKinds[v];
            blades_type::draw(label,at-blades_type::width(label,caption),72,caption,light);
        }
    }
    // The rows: the blade's own row material, its focus glint, a cover or
    // the 1888 music glyph, and the name.
    static int focus_row=-1, focus_view=-1;
    static Uint32 focus_at=0;
    const Uint32 now=SDL_GetTicks();
    const int n=model.viewer_count();
    const int item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    if (focus_row!=item || focus_view!=v) {focus_row=item;focus_view=v;focus_at=now;}
    auto material=[&](SDL_Surface *image,int x,int y,Uint8 alpha=255) {
        if (!image) return;
        SDL_SetSurfaceAlphaMod(image,alpha);SDL_Rect at{x,y,0,0};
        SDL_BlitSurface(image,nullptr,screen,&at);SDL_SetSurfaceAlphaMod(image,255);
    };
    constexpr int list_w=584, pitch=74, shown=9, list_top=166;
    const int list_x=content_x;
    const music::Song *playing=music_player.current_song();
    if (n>0 && !tunes::rows.empty()) {
        const int first_row=top(item,n,shown);
        for (int r=0;r<shown && first_row+r<n;r++) {
            const int i=first_row+r, y=list_top+r*pitch;
            const tunes::Row &row=tunes::rows[size_t(std::min(i,int(tunes::rows.size())-1))];
            if (!visible_row(row)) continue;
            const bool on=i==item;
            if (SDL_Surface *image=era_chrome::row_material[on?1:0]) {
                material(image,list_x-18,y-18);
                if (on) {
                    const auto glow=era_chrome::row_glint(now-focus_at);
                    SDL_Rect saved,clip{list_x,y,list_w,74},cut;
                    SDL_GetClipRect(screen,&saved);SDL_IntersectRect(&saved,&clip,&cut);SDL_SetClipRect(screen,&cut);
                    material(era_chrome::row_shine,list_x+int(std::lround(glow.x*list_w/420)),y+5,Uint8(glow.alpha*255));
                    SDL_SetClipRect(screen,&saved);
                }
            } else {
                if (on) gradient(list_x,y,list_w,64,era_chrome::mix(p.light,0xffffff,.35f),p.mid);
                rect(list_x,y+65,list_w,3,p.dark);
            }
            if (SDL_Surface *cover=tunes::row_cover(row)) {
                rect(list_x+6,y+7,52,52,0x2c342c);
                blit_contained(cover,{list_x+7,y+8,50,50});
            } else if (row.action>=0) {} // an option: its words alone
            else if (SDL_Surface *icon=music_kind_art[row.group?v:0]) blit_contained(icon,{list_x+8,y+10,46,46});
            const bool here=holds(row,playing);
            const int room=list_w-97-(here?40:0);
            blades_type::draw(blades_type::fit(name(row),body,room),list_x+84,y+4,body,ink);
            if (here) play_mark(list_x+list_w-38,y+20,26,hex(ink));
        }
        if (first_row>0) arrow(list_x+list_w-26,list_top-20,false,0x0f0f0f);
        if (first_row+shown<n) arrow(list_x+list_w-26,list_top+shown*pitch+4,true,0x0f0f0f);
    } else {
        blades_type::draw(dash::media_text::music_empty(v),list_x+10,list_top+4,body,ink);
        // Ex-Box addition: no 360 original (where the PS5 finds music)
        blades_type::wrapped("Copy MP3, FLAC, Ogg or WAV files to /data/nxe/music on the PS5, or to a Music folder on a USB drive.",
                             list_x+10,list_top+64,list_w-20,6,caption,ink);
    }
    // The recessed edge between the rows and the meta panel, as on the System blade.
    shade(list_x+list_w+10,list_top,2,shown*pitch-18,0x000000,60);
    shade(list_x+list_w+12,list_top,1,shown*pitch-18,0xffffff,70);
    // The meta panel: the focused row's name, what it holds, and its cover.
    const int mx=right_x+10, mw=std::min(list_w,right_w-10);
    if (const tunes::Row *row=focused(); row && row->songs.empty()) {
        // An option, a song of the current playlist or a picture: its words and picture.
        blades_type::draw(blades_type::fit(name(*row),body,mw),mx,154,body,ink);
        if (!row->sub.empty()) blades_type::wrapped(row->sub,mx,204,mw-250,3,caption,soft);
        if (SDL_Surface *cover=tunes::row_cover(*row)) {
            const int cx=mx+mw-226, cy=208;
            rect(cx,cy,226,214,0x2c342c);
            rect(cx+1,cy+1,224,212,0x50544f);
            blit_contained(cover,{cx+10,cy+10,206,194});
        }
    } else if (row) {
        blades_type::draw(blades_type::fit(name(*row),body,mw),mx,154,body,ink);
        const music::Song &song=first(*row);
        std::string about;
        if (row->group) {
            about=row->detail; // "Artist  ·  3 songs": a line each
            for (size_t at;(at=about.find("  \xc2\xb7  "))!=std::string::npos;) about.replace(at,6,"\n");
        }
        else {
            about=song.artist.empty()?"Unknown Artist":song.artist; // Ex-Box addition: no 360 original
            if (!song.album.empty()) about+="\n"+song.album;
            if (!song.genre.empty()) about+="\n"+song.genre;
        }
        blades_type::wrapped(about,mx,204,mw-250,3,caption,soft);
        // The cover in the gamerpic's recessed frame, at the panel's right.
        const int cx=mx+mw-226, cy=208;
        rect(cx,cy,226,214,0x2c342c);
        rect(cx+1,cy+1,224,212,0x50544f);
        if (SDL_Surface *cover=tunes::row_cover(*row)) blit_contained(cover,{cx+10,cy+10,206,194});
        else if (SDL_Surface *icon=music_kind_art[row->group?v:0]) blit_contained(icon,{cx+40,cy+34,146,146});
    }
    // Now Playing: a card like the gamercard, its name bar, the cover in the
    // recessed frame, the song's rows and where it is.
    const int card_x=mx, card_y=470, card_w=mw, card_h=208;
    rounded(card_x,card_y,card_w,card_h,15,0x95afc5,0x87a9ce);
    gradient(card_x+4,card_y+4,card_w-8,48,era_chrome::mix(0x95afc5,0xffffff,.08f),era_chrome::mix(0x95afc5,0x000000,.06f));
    material(era_chrome::profile_material,card_x-18,card_y-18);
    blades_type::draw(dash::media_text::now_playing(model),card_x+16,card_y,body,ink);
    rect(card_x+20,card_y+71,120,114,0x2c342c);
    rect(card_x+21,card_y+72,118,112,0x50544f);
    const int tx=card_x+162, tw=card_w-180;
    if (playing) {
        if (SDL_Surface *cover=song_cover(playing->path)) blit_contained(cover,{card_x+30,card_y+81,99,99});
        else if (SDL_Surface *icon=music_kind_art[0]) blit_contained(icon,{card_x+46,card_y+97,67,67});
        const int badge=custom_scene::draw_equalizer(tx,card_y+58)?86:0;
        blades_type::draw(blades_type::fit(playing->title,caption,tw-badge),tx+badge,card_y+53,caption,ink);
        blades_type::draw(blades_type::fit(playing->artist.empty()?"Unknown Artist":playing->artist,caption,tw),tx,card_y+101,caption,soft); // Ex-Box addition: no 360 original
        const std::string held=pause_line(), clock=held.empty()?clock_line():held;
        const int clock_w=blades_type::width(clock,caption);
        blades_type::draw(clock,card_x+card_w-22-clock_w,card_y+149,caption,ink);
        const int bar_w=card_w-22-clock_w-20-(tx-card_x);
        if (bar_w>40) {
            rounded(tx,card_y+165,bar_w,14,7,era_chrome::mix(p.dark,0x000000,.2f),p.dark);
            const double length=music_player.length_seconds();
            const int fill=length>0?int((bar_w-4)*std::clamp(music_player.elapsed_seconds()/length,0.0,1.0)):0;
            if (fill>6) rounded(tx+2,card_y+167,fill,10,5,p.high,p.light);
        }
    } else {
        if (SDL_Surface *icon=music_kind_art[0]) blit_contained(icon,{card_x+46,card_y+97,67,67});
        // Ex-Box addition: no 360 original
        blades_type::draw("Nothing is playing.",tx,card_y+53,caption,ink);
        blades_type::draw(blades_type::fit("Press A to play.",caption,tw),tx,card_y+101,caption,soft);
    }
    // Shuffle and repeat, under the card.
    const unsigned dim=era_chrome::mix(0x0f0f0f,p.light,.55f);
    order_state(card_x+16,card_y+card_h+22,40,caption,ink,{Uint8(dim>>16),Uint8(dim>>8),Uint8(dim),255},true);
}

}
// The Music Library, in the dress of the dashboard in use.
void draw_music() {
    if (model.era==0) music_page::draw_blades();
    else music_page::draw_skinned();
    if (model.music_page==dash::Model::MusicAlbumInfo && model.music_album_field>=0) {
        // Ex-Box addition: no 360 original keyboard. The surrounding page uses the era's skin.
        shade(0,0,W,H,0x000000,190);
        constexpr int x=390,y=190,cw=112,ch=66;
        gradient(x-30,y-30,1180,690,0x384339,0x182119);
        const char *fields[]={"Album","Artist","Genre"}; // music/1025_EditAlbumInfo.xur
        text(fields[model.music_album_field],x,y,large,{255,255,255,255});
        text(fit(model.music_album_input+"|",regular,1100),x,y+64,regular,{220,235,210,255});
        const std::string keys=dash::Model::music_album_keys;
        for (size_t i=0;i<keys.size();i++) {
            const int kx=x+int(i%10)*cw,ky=y+132+int(i/10)*ch;
            if (int(i)==model.music_album_key) rect(kx,ky,cw-8,ch-8,0x548d27);
            text(keys[i]==' '?"_":std::string(1,keys[i]),kx+32,ky+8,regular,{255,255,255,255});
        }
    }
}
// The Avatar Editor: categories down the left, the avatar on the right. The
// camera moves in on the head for hair, face, hats and glasses, as the 360's did.
void draw_avatar_editor() {
    gradient(0,0,W,H,0xe8efe0,0x9fbf7c);
    if (glow) {
        SDL_Rect spot{1380-480,60,960,900};
        SDL_SetSurfaceAlphaMod(glow,140);
        SDL_BlitScaled(glow,nullptr,screen,&spot);
    }
    text("Avatar Editor",95,58,large,{40,60,30,255});
    const int rows=look3d::rows();
    const int sel=std::clamp(model.avatar_category,0,std::max(0,rows-1));
    TTF_Font *font=guide::item_font?guide::item_font:regular;
    constexpr int lx=95, ly=136, lw=760, pitch=44, shown=17;
    const int top=std::clamp(sel-shown/2,0,std::max(0,rows-shown));
    for (int r=0;r<shown && top+r<rows;r++) {
        const int row=top+r, y=ly+r*pitch, cat=look3d::category_of(row);
        const bool on=row==sel;
        if (on) {
            rounded(lx-2,y-2,lw+4,pitch,4,0x4e8e17,0x3b7410);
            rounded(lx,y,lw,pitch-4,3,0x8fd23c,0x5da516);
            rect(lx+3,y+1,lw-6,2,0xb9ea7c);
        } else if (r%2) shade(lx,y,lw,pitch-4,0xffffff,60);
        const std::string label=cat==-2?"Animation":cat<0?"Avatar":avkit::categories()[size_t(cat)].label;
        const std::string value=cat==-2?look3d::animation_label():cat<0?(look3d::imported?"Imported (360sona)":"Made Here"):avkit::choice_label(look3d::look,cat);
        const SDL_Color ink=on?SDL_Color{255,255,255,255}:SDL_Color{40,60,30,255};
        text(label,lx+14,y+5,font,ink);
        int vx=lx+lw-16;
        if (on) {text("›",vx-10,y+4,font,ink); vx-=34;}
        const std::string v=fit(value,small,300);
        vx-=text_width(v,small);
        text(v,vx,y+10,small,on?SDL_Color{255,255,255,255}:SDL_Color{70,96,58,255});
        avkit::Colour c;
        if (cat>=0 && avkit::choice_swatch(cat,avkit::value(look3d::look,avkit::categories()[size_t(cat)]),c)) {
            vx-=40;
            rect(vx,y+8,26,26,0xffffff);
            rect(vx+2,y+10,22,22,unsigned(c.r)<<16|unsigned(c.g)<<8|c.b);
        }
        if (on) text("‹",vx-30,y+4,font,ink);
    }
    if (top>0) guide::chevron(lx+lw-30,ly-30,false);
    if (top+shown<rows) guide::chevron(lx+lw-30,ly+shown*pitch,true);
    // The choices of a colour category, as a row of swatches.
    const int cat=look3d::category_of(sel);
    if (cat>=0) {
        const auto &c=avkit::categories()[size_t(cat)];
        avkit::Colour col;
        if (avkit::choice_swatch(cat,0,col)) {
            const int now_v=avkit::value(look3d::look,c);
            const int sw=std::min(44,(lw+8)/c.count-6);
            for (int i=0;i<c.count;i++) {
                avkit::choice_swatch(cat,i,col);
                const int x=lx+i*(sw+6), y=ly+shown*pitch+22;
                rect(x-2,y-2,sw+4,sw+4,i==now_v?0x4e8e17:0xffffff);
                rect(x,y,sw,sw,unsigned(col.r)<<16|unsigned(col.g)<<8|col.b);
            }
        }
    }
    const Uint32 now=SDL_GetTicks();
    const bool head=cat>=0 && avkit::categories()[size_t(cat)].zoom;
    look3d::ease(now,head);
    if (SDL_Surface *f=look3d::render(look3d::stage,600,940,look3d::stage_camera(),now,33)) {
        SDL_Rect r{1080,40,600,940};
        SDL_BlitSurface(f,nullptr,screen,&r);
    } else {
        const std::string a="Showing your imported avatar.";
        text(a,1380-text_width(a,regular)/2,H/2,regular,{40,60,30,255});
    }
}
