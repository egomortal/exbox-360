// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
void refresh_update_art() {
    refresh_picture_background();
    sfx::use_era_set();
    notify_scene::refresh();
    custom_scene::refresh();
    const bool library_changed=library_art_build!=library_build();
    load_welcome_resources();
    if (library_changed) load_library_art();
    if (artwork_build==model.dashboard_build && !library_changed) return;
    // dash.xex/FFFE07D1: extracted by tools/build_small_items.py, never inferred.
    std::string title_root=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) title_root=std::string(override)+"/";
#endif
    SDL_FreeSurface(dashboard_title);
    dashboard_title=load_png(title_root+std::to_string(notify_scene::selected_build())+"/dashboard-title.png");
    if (dashboard_title) SDL_SetSurfaceBlendMode(dashboard_title,SDL_BLENDMODE_BLEND);
    std::string directory=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) directory=std::string(override)+"/";
#endif
    directory+=std::to_string(model.dashboard_build)+"/";
    clear_scaled_art();
    auto replace=[&](SDL_Surface *&surface,const std::string &name) {
        SDL_FreeSurface(surface);surface=load_png(directory+name+".png");
        if (surface) SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);
    };
    for (int i=0;i<dash::IconCount;i++) replace(era_icons[i],"category-"+std::to_string(i));
    replace(era_panel,"panel-gradient");replace(era_floor,"floor-shadow");
    replace(era_pin,"pin");replace(era_recent,"recent");
    replace(era_aura_floor,"aura-floor");
    replace(era_banner,"blade-banner");
    SDL_FreeSurface(era_background);era_background=load_art(directory+"background.jpg");
    era_light_navigation=false;era_dark_footer=model.metro_v1();
    if (era_background) {
        SDL_Surface *pixels=SDL_ConvertSurfaceFormat(era_background,SDL_PIXELFORMAT_ARGB8888,0);
        if (pixels) {
            auto luma=[&](int x,int y) {
                const auto *row=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(pixels->pixels)+y*pixels->h/H*pixels->pitch);
                const Uint32 rgb=row[x*pixels->w/W];
                return (54*int((rgb>>16)&255)+183*int((rgb>>8)&255)+19*int(rgb&255))/256;
            };
            era_light_navigation=(luma(240,175)+luma(900,175)+luma(1400,175))/3>165;
            era_dark_footer=luma(400,1020)<128;
            SDL_FreeSurface(pixels);
        }
    }
    for (int i=0;i<dash::IconCount;i++) {
        const std::string path=directory+"icon-"+std::to_string(i)+".png";
        if (auto *image=load_png(path)) {
            SDL_FreeSurface(blades_icons[i]);blades_icons[i]=image;
            SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
            blades_native[i]=true;blades_icon_paths[i]=path;
        }
    }
    if (auto *image=load_png(directory+"logo.png")) {
        SDL_FreeSurface(xbox_logo);xbox_logo=image;SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
    }
    for (int i=0;i<6;i++) if (auto *image=load_png(directory+"music-"+std::to_string(i)+".png")) {
        SDL_FreeSurface(music_controls[i]);music_controls[i]=image;SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
    }
    artwork_build=model.dashboard_build;
    load_promotions();
}
