// Included inside main.cpp's private namespace.
// gamer/ThemePicker.xur (9199-17559): DetailsPanel, labHeading, lstThemes
// with SkinsList; legend_a Select / legend_b Back. Uses the era's shared skin.
void draw_theme_picker() {
    namespace sk=media_skin;
    sk::scene_background();
    // Original canvas: 460x495 (9199), 450x495 (13604 onward), at 720p.
    // Ex-Box addition: center the panel until RomeRoot placement is decoded.
    const bool light=sk::light();
    const int w=light?675:690, h=743, x=(W-w)/2, y=(H-h)/2;
    sk::background_panel(x,y,w,h);
    const bool picker=model.viewer==dash::Viewer::Themes;
    TTF_Font *font=model.era>=2 && era_text?era_text:large;
    if (light) gradient(x,y,w,108,0xcdd0d3,0xb9bdc2);
    text(picker?"Select Theme":"Profile",x+(light?45:47),y+(light?30:38),font,
         light?sk::kRowSel:sk::kInk);
    std::vector<std::string> rows;
    if (picker) rows=model.theme_entries();
    else for (const auto &entry:model.profile_entries()) rows.push_back(entry.label);
    for (int i=0;i<int(rows.size());++i) {
        const int lx=x+(light?0:33), lw=light?w:623;
        const int ry=y+(light?108:105)+i*90;
        if (i==model.viewer_item) {
            if (light) sk::green_highlight(lx,ry,lw,78);
            else outline(lx,ry,lw,78,3,sk::nxe_green());
        }
        text(rows[std::size_t(i)],lx+30,ry+18,regular,
             light && i!=model.viewer_item?sk::kRowSel:sk::kInk);
    }
}
