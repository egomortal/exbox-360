// firstrun DetailsPanel and oobe TwoThirdsPane, using each build's private text/art.
void draw_welcome_setup() {
    const bool light=model.era>=2, setup=model.viewer==dash::Viewer::InitialSetup;
    const auto page=model.welcome_page();
    if (model.era==0) {
        shade(0,0,W,H,0x000000,185);
        if (blade_dark) nine_slice(blade_dark,102,142,1716,826,24,36);
        else rounded(120,160,1680,790,12,0x42505e,0x2c3742);
    } else {
        media_skin::scene_background();
        media_skin::background_panel(120,160,1680,790);
    }
    TTF_Font *body=era_text?era_text:regular;
    const SDL_Color ink=light?SDL_Color{16,18,20,255}:SDL_Color{235,235,235,255};
    text(fit(page.title,large,1500),180,185,large,ink);
    if (SDL_Surface *art=title_icon(page.art)) blit_contained(art,{180,300,440,490});
    SDL_Rect saved{};SDL_GetClipRect(screen,&saved);
    SDL_Rect clip{660,290,1060,500};SDL_SetClipRect(screen,&clip);
    system_page::paragraphs(page.body,680,300,1020,body,ink,40);
    SDL_SetClipRect(screen,&saved);
    if (setup) {
        const int step=model.setup_step;
        if (step==dash::Model::SetupResolution) text("1080p",680,810,body,ink);
        if (step==dash::Model::SetupAutoOff || step==dash::Model::SetupClock) {
            const bool first=step==dash::Model::SetupAutoOff?model.dimming:!model.clock_24_hour;
            const char *left=step==dash::Model::SetupAutoOff?"Enable":"12-Hour Clock";
            const char *right=step==dash::Model::SetupAutoOff?"Disable":"24-Hour Clock";
            rect(first?680:1190,805,480,65,0x589c16);
            text(left,700,815,regular,first?SDL_Color{255,255,255,255}:ink);
            text(right,1210,815,regular,first?ink:SDL_Color{255,255,255,255});
        }
        text(std::to_string(step+1)+" / 5",1600,895,small,ink);
    } else if (model.welcome_confirm) {
        rect(model.welcome_yes?680:1190,805,480,65,0x589c16);
        text("Yes",700,815,regular,model.welcome_yes?SDL_Color{255,255,255,255}:ink);
        text("No",1210,815,regular,model.welcome_yes?ink:SDL_Color{255,255,255,255});
    } else text(std::to_string(model.viewer_item+1)+" / "+std::to_string(model.welcome_pages().size()),1600,895,small,ink);
}
