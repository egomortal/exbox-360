// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_toast(Uint32 now) {
    if (!toast.live) return;
    if (model.notification_level==0) {toast.live=false;return;}
    // NXE's notification opens from the disc into a dark, rimmed pill.  The
    // dimensions below are the 1080p equivalents of the 2008 dashboard UI.
    constexpr Uint32 open=850, close=900;
    const Uint32 hold=model.notification_level==1?1600:model.notification_level==3?6000:3200;
    if (notify_scene::draw(now,hold)) return;
    const Uint32 t=now-toast.at;
    if (t>=open+hold+close) {toast.live=false; return;}
    constexpr int h=96;
    const int title_width=text_width(toast.title,regular);
    const int body_width=text_width(toast.text,regular);
    const int w=std::clamp(std::max(title_width,body_width)+180,400,900);
    const int x=(W-w)/2, y=H-208;
    float disc_scale=1.0f;
    float pill_width=1.0f;
    if (t<650) {
        const float p=progress(now,toast.at,650);
        disc_scale=p<0.72f?0.1f+1.35f*ease_out(p/0.72f):1.45f-0.45f*progress(now,toast.at+468,182);
    }
    if (t<383) pill_width=0;
    else if (t<850) pill_width=ease_out(float(t-383)/467.0f);
    if (t>=open+hold) {
        const float p=float(t-open-hold)/float(close);
        // Close the text body before the disc contracts, so no tiny icon is
        // left floating inside a half-open empty pill.
        pill_width=1.0f-ease_out(std::max(0.0f,(p-0.55f)/0.25f));
        disc_scale=p<0.44f?1.0f+0.45f*ease_out(p/0.44f):1.45f-1.25f*ease_out((p-0.44f)/0.56f);
    }
    if (pill_width>0.001f) {
        const int visible_w=std::max(h,int(h+(w-h)*pill_width));
        rounded(x,y,visible_w,h,h/2,0xb6b9b9,0x858989);
        rounded(x+3,y+3,visible_w-6,h-6,(h-6)/2,0x4b4d4d,0x3b3d3d);
    }
    if (disc_scale>0.01f) {
        const int cx=x+47, cy=y+h/2;
        const int radius=std::max(1,int(45*disc_scale));
        disc(cx,cy,radius,0x0a0b0b);
        const int outer=std::max(1,int(38*disc_scale));
        if (toast_segments) {
            SDL_Rect at{cx-outer,cy-outer,outer*2,outer*2};
            blit_quality(toast_segments,nullptr,screen,&at);
        }
        const int icon_size=std::max(1,int(58*disc_scale));
        if (toast.icon && (t/1000)%2) {
            SDL_Rect destination{cx-icon_size/2,cy-icon_size/2,icon_size,icon_size};
            blit_quality(toast.icon,nullptr,screen,&destination);
        } else glyph(xb::Guide,cx-icon_size/2,cy-icon_size/2,icon_size);
    }
    if (pill_width>0.97f && t>=650 && t<open+hold+Uint32(close*0.55f)) {
        const int max_text=w-155;
        text(fit(toast.title,regular,max_text),x+108,y+15,regular,{224,227,227,255});
        text(fit(toast.text,regular,max_text),x+108,y+53,regular,{212,216,216,255});
    }
}
