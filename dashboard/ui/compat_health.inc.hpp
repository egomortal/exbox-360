// Ex-Box addition: owner-local, read-only compatibility/health summary.
void draw_compat_health() {
    namespace sk=media_skin;
    sk::scene_background();
    constexpr int x=120,y=90,w=1680,h=895;
    sk::background_panel(x,y,w,h);
    const bool light=sk::light();const SDL_Color ink=light?sk::kRowSel:sk::kInk;
    text("Compatibility & Health",x+65,y+42,large,ink);
    const auto rows=model.compat_health_rows();
    const int n=int(rows.size()),item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    constexpr int lx=185,ly=225,lw=690,pitch=70;
    for(int i=0;i<n;++i){const int py=ly+i*pitch;const bool on=i==item;
        if(on){if(light)gradient(lx,py,lw,pitch-4,0x77ba02,0x51860f);else rounded(lx,py,lw,pitch-4,4,0x8fd23c,0x5da516);}
        else if(light)shade(lx,py+pitch-5,lw,1,0x9facb2,255);else rect(lx,py+pitch-5,lw,2,0x55636e);
        const SDL_Color row_ink=!light||on?SDL_Color{235,235,235,255}:SDL_Color{0x65,0x6d,0x72,255};
        text(fit(rows[std::size_t(i)].label,regular,lw-36),lx+18,py+15,regular,row_ink);
    }
    constexpr int rx=940,rw=760;
    text("Current Status",rx,ly,regular,light?SDL_Color{80,150,25,255}:ink);
    text(fit(rows[std::size_t(item)].value,regular,rw),rx,ly+55,regular,ink);
    wrapped_text(rows[std::size_t(item)].description,rx,ly+125,rw,6,regular,ink);
    wrapped_text("Read-only. Unknown and unpublished values are never displayed as zero. No network request, ROM read, artwork copy, launch, or compatibility guess occurs on this page.",rx,ly+430,rw,3,small,ink);
}
