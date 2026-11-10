// Included inside main.cpp's private namespace.
// gamer/ThemePicker.xur (9199-17559): DetailsPanel, labHeading, lstThemes
// with SkinsList; legend_a Select / legend_b Back. Uses the era's shared skin.
void draw_theme_picker() {
    namespace sk=media_skin;
    sk::scene_background();
    // Original canvas: 460x495 (9199), 450x495 (13604 onward), at 720p.
    // Ex-Box addition: center the panel until RomeRoot placement is decoded.
    const bool light=sk::light();
    const bool picker=model.viewer==dash::Viewer::Themes;
    const int w=picker?1250:(light?675:690), h=743, x=(W-w)/2, y=(H-h)/2;
    sk::background_panel(x,y,w,h);
    TTF_Font *font=model.era>=2 && era_text?era_text:large;
    if (light) gradient(x,y,w,108,0xcdd0d3,0xb9bdc2);
    const char* filter=model.theme_filter==1?"Favorites":model.theme_filter==2?"Recently Used":"All Themes";
    text(picker?std::string("Select Theme - ")+filter:"Profile",x+(light?45:47),y+(light?30:38),font,
         light?sk::kRowSel:sk::kInk);
    std::vector<std::string> rows;
    if (!picker) for (const auto &entry:model.profile_entries()) rows.push_back(entry.label);
    const int count=picker?model.theme_count():int(rows.size());
    constexpr int visible=6;
    const int first=picker?std::clamp(model.viewer_item-visible/2,0,std::max(0,count-visible)):0;
    const int last=picker?std::min(count,first+visible):count;
    for (int i=first;i<last;++i) {
        const int lx=x+(light?0:33), lw=picker?(light?675:623):(light?w:623);
        const int ry=y+(light?108:105)+(i-first)*90;
        if (i==model.viewer_item) {
            if (light) sk::green_highlight(lx,ry,lw,78);
            else outline(lx,ry,lw,78,3,sk::nxe_green());
        }
        const std::string label=picker?model.theme_entry(i):rows[std::size_t(i)];
        text(fit(label,regular,lw-60),lx+30,ry+18,regular,
             light && i!=model.viewer_item?sk::kRowSel:sk::kInk);
    }
    if(picker&&count>visible){
        const std::string position=std::to_string(model.viewer_item+1)+" / "+std::to_string(count)+"   Left/Right: page   LB/RB: letter   X: favorite   Y: filter";
        text(position,x+45,y+h-61,small,light?sk::kRowSel:sk::kInk);
    }
    if(picker){
        SDL_Rect preview{x+720,y+166,480,270};
        rect(preview.x-4,preview.y-4,preview.w+8,preview.h+8,light?0x72777c:0x18251b);
        if(installed_theme_preview)blit_quality(installed_theme_preview,nullptr,screen,&preview);
        else {gradient(preview.x,preview.y,preview.w,preview.h,light?0xaeb3b8:0x33413a,light?0x747a80:0x17221b);text(model.viewer_item==0?"Xbox 360 default":"Loading preview...",preview.x+35,preview.y+112,regular,sk::kInk);}
        const std::string label=model.theme_entry(model.viewer_item);
        text(fit(label,regular,preview.w),preview.x,preview.y+preview.h+32,regular,light?sk::kRowSel:sk::kInk);
    }
}
