// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void draw_dashboard_picker() {
    constexpr int x=256,y=123,w=1408,h=810;
    const int family=std::clamp(model.viewer==dash::Viewer::DashboardEras?model.viewer_item:model.picker_family,0,4);
    const bool updates=model.viewer==dash::Viewer::DashboardUpdates;
    shade(0,0,W,H,0x000000,145);
    rounded(x,y,w,h,8,0xe9ede6,0xc7cec4);
    rect(x,y,w,95,0x28362a);
    text("Dashboard styles",x+34,y+20,large,{250,252,246,255});
    text(std::string(updates?"Choose update > ":"Choose era > ")+dash::kDashboardFamilies[family].name,x+35,y+111,regular,{49,64,44,255});
    rect(x+372,y+169,1,h-216,0xa5afa0);
    for (int i=0;i<5;i++) {
        int top=y+178+i*94;
        if (i==family) gradient(x+18,top,332,81,updates?0xa7b79d:0x93cc36,updates?0x94a68a:0x619f19);
        text(dash::kDashboardFamilies[i].name,x+39,top+14,era_text,{44,60,37,255});
        text(std::to_string(dash::updates_for(i,model.picker_previews).size())+" updates",x+40,top+57,small,{65,82,56,255});
    }
    const auto choices=dash::updates_for(family,model.picker_previews);
    if (updates) {
        const int selected=std::clamp(model.viewer_item,0,int(choices.size())-1);
        const int first=std::clamp(selected-7,0,std::max(0,int(choices.size())-8));
        for (int row=0;row<8 && first+row<int(choices.size());row++) {
            const auto update=choices[size_t(first+row)];
            const int top=y+171+row*68;
            if (first+row==selected) gradient(x+398,top,w-430,64,0x94cf34,0x619f19);
            else rect(x+415,top+66,w-467,1,0xaeb8a7);
            text(dash::dashboard_version(update.build),x+421,top+12,era_text,{37,55,30,255});
            text(update.preview?"Preview":"Retail",x+w-220,top+19,regular,{51,74,41,255});
            if (update.build==model.dashboard_build && dash::kDashboardFamilies[family].era==model.era)
                text("Active",x+w-347,top+19,regular,{51,74,41,255});
        }
        if (first>0) guide::chevron(x+w-33,y+174,false);
        if (first+8<int(choices.size())) guide::chevron(x+w-33,y+694,true);
        text(std::to_string(selected+1)+" of "+std::to_string(choices.size()),x+422,y+731,small,{74,90,66,255});
    } else {
        text(dash::kDashboardFamilies[family].name,x+425,y+184,large,{44,63,37,255});
        const char *descriptions[]={
            "Curved metallic blades, colored sections and the original side Guide.",
            "Layered green cards, avatars and the blue five-page Guide.",
            "A brighter white and teal dashboard, with a lime-highlighted Guide.",
            "The first tile dashboard, with shaded tiles and lime highlights.",
            "Flat green tiles and a four-page Guide with Games & Apps."
        };
        wrapped_text(descriptions[family],x+425,y+267,w-493,4,era_text,{59,76,50,255});
        text(std::to_string(choices.size())+" updates available",x+426,y+474,regular,{65,86,52,255});
        text("Choose an era, then select its update.",x+426,y+533,regular,{65,86,52,255});
        text("Minor updates can share the same appearance.",x+426,y+580,regular,{65,86,52,255});
    }
}
