// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_ads_settings() {
    constexpr int x=150,y=95,w=1620,h=870;
    constexpr const char *profiles[]={"Original Blades","Transitional Blades","NXE","Kinect","Metro","Late Metro"};
    constexpr const char *modes[]={"Original","Custom selection","Shuffle selected","Off"};
    const int profile=model.current_ad_profile(),mode=std::clamp(model.ad_mode[profile],0,3);
    const auto &selected=model.ad_selected[profile];
    shade(0,0,W,H,0x000000,155);
    rounded(x,y,w,h,8,0xe9ede6,0xc7cec4);
    rect(x,y,w,95,0x28362a);
    text("Ads",x+34,y+18,large,{250,252,246,255});
    text(std::string(profiles[profile])+"  /  "+modes[mode],x+180,y+32,regular,{233,242,224,255});
    text(std::to_string(selected.size())+" selected  /  "+(model.ad_show_all?"All resources":"Ad candidates")+"  /  "+std::to_string(model.ad_visible_count())+" of "+std::to_string(model.ad_catalog.size()),x+35,y+112,regular,{49,64,44,255});
    constexpr int list_w=810,row_h=61,rows=10;
    const int n=model.viewer_count(),first=std::clamp(model.viewer_item-rows+1,0,std::max(0,n-rows));
    for (int row=0;row<rows && first+row<n;row++) {
        const int index=first+row,top=y+169+row*row_h;
        const bool on=index==model.viewer_item;
        if (on) gradient(x+22,top,list_w-26,row_h-3,0x94cf34,0x619f19);
        else rect(x+34,top+row_h-2,list_w-50,1,0xaeb8a7);
        std::string label;
        if (index==0) label=std::string("Mode: ")+modes[mode]+" >";
        else if (index==1) label="New shuffle";
        else if (index==2) label="Select all ad candidates";
        else if (index==3) label="Clear selection";
        else if (index==4) label="Reset this era to Original";
        else {
            const auto &ad=model.ad_catalog[size_t(model.ad_catalog_index(index-5))];
            const bool checked=std::find(selected.begin(),selected.end(),ad.id)!=selected.end();
            label=std::string(checked?"[x] ":"[ ] ")+ad.title;
        }
        text(fit(label,regular,list_w-75),x+42,top+12,regular,{31,48,27,255});
    }
    rect(x+list_w+15,y+164,1,h-215,0xa5afa0);
    const int right=x+list_w+42,right_w=w-list_w-78;
    if (model.viewer_item>=5 && model.ad_catalog_index(model.viewer_item-5)>=0) {
        const auto &ad=model.ad_catalog[size_t(model.ad_catalog_index(model.viewer_item-5))];
        rect(right,y+177,right_w,335,0x172017);
        if (SDL_Surface *art=title_icon(ad.image,true)) blit_contained(art,{right,y+177,right_w,335});
        else text("Image unavailable",right+30,y+315,regular,{230,236,226,255});
        text(fit(ad.title,regular,right_w),right,y+535,regular,{36,53,30,255});
        text(std::to_string(ad.width)+" x "+std::to_string(ad.height)+" original pixels",right,y+595,small,{60,76,50,255});
        std::string role_note="Role or framing unverified. The complete image is preserved.";
        if (!ad.auto_eligible) {
            role_note="Interface resource. Select all excludes it; manual selection still works.";
        } else if (ad.preferred_fit=="match_slot_aspect") {
            role_note="Mapped to an original dashboard slot. Fitted without stretching.";
        }
        const std::string source_note=ad.classification=="official-store-artwork"
            ? "Official store artwork; not a historical dashboard ad."
            : "Original dashboard resource. Its role is tracked separately from its source.";
        wrapped_text(role_note,right,y+640,right_w,3,small,{60,76,50,255});
        wrapped_text(source_note,right,y+748,right_w,2,small,{60,76,50,255});
    } else {
        text("Choose the ads for this era",right,y+186,regular,{36,53,30,255});
        wrapped_text("Original keeps this update's preserved promotion choices. Custom shows your selected images. Shuffle uses a saved order; choose New shuffle to change it. Off hides ads for this era.",right,y+249,right_w,7,regular,{60,76,50,255});
        wrapped_text(model.era==0?"Custom cards appear on the Xbox Live blade. Original restores the preserved banner.":"Your selected ads appear on Home / My Xbox. Larger selections use pages.",right,y+575,right_w,3,small,{60,76,50,255});
        text("An empty selection shows no custom ads.",right,y+723,small,{60,76,50,255});
    }
    text(std::to_string(model.viewer_item+1)+" of "+std::to_string(n)+"  /  changes saved automatically",x+40,y+h-51,small,{60,76,50,255});
}
