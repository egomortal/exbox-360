// Borrowed frame geometry only; no bitmap ownership or transition allocation.
namespace nxe_reflection {
std::array<SDL_Rect,10> panes;
size_t pane_count=0;
SDL_Surface *figure=nullptr;
SDL_Rect figure_bounds{};
float figure_opacity=0;
bool figure_has_color_key=false;
Uint32 figure_color_key=0;

void begin_frame() {
    pane_count=0;
    figure=nullptr;
    figure_opacity=0;
}

void remember_pane(const SDL_Rect &bounds) {
    // Five current panes plus five during a channel transition fit this fixed
    // array. Bounds come from the actual painter, not a second layout formula.
    if (pane_count<panes.size()) {
        panes[pane_count++]=bounds;
    }
}

void remember_figure(SDL_Surface *image,const SDL_Rect &bounds,float opacity) {
    // The avatar renderer owns this surface. It stays valid until this same
    // frame's reflect_cards call; begin_frame drops the borrowed pointer.
    figure=image;
    figure_bounds=bounds;
    figure_opacity=opacity;
    figure_has_color_key=SDL_GetColorKey(image,&figure_color_key)==0;
}

float source_alpha(int x,int y) {
    x=std::clamp(x,0,figure->w-1);
    y=std::clamp(y,0,figure->h-1);
    if (!figure->format->Amask && !figure_has_color_key) {
        return 1;
    }
    Uint32 pixel=0;
    const int bytes=figure->format->BytesPerPixel;
    const auto *address=static_cast<const Uint8 *>(figure->pixels)+y*figure->pitch+x*bytes;
    std::memcpy(&pixel,address,size_t(bytes));
    if (figure_has_color_key && pixel==figure_color_key) {
        return 0;
    }
    if (!figure->format->Amask) return 1;
    if (figure->format->Aloss==0) return float((pixel&figure->format->Amask)>>figure->format->Ashift)/255;
    Uint8 red,green,blue,alpha;
    SDL_GetRGBA(pixel,figure->format,&red,&green,&blue,&alpha);
    return float(alpha)/255;
}

float figure_coverage_at(float x,float y,float half_pixel) {
    if (!figure || figure_bounds.w<1 || figure_bounds.h<1 ||
        x<figure_bounds.x || x>=figure_bounds.x+figure_bounds.w ||
        y<figure_bounds.y || y>=figure_bounds.y+figure_bounds.h) {
        return 0;
    }
    // A bilinear alpha mask follows the visible silhouette. Reflecting its
    // whole bounding box would mirror unrelated background beside the avatar.
    const float source_x=(x-figure_bounds.x+half_pixel)*figure->w/figure_bounds.w-0.5f;
    const float source_y=(y-figure_bounds.y+half_pixel)*figure->h/figure_bounds.h-0.5f;
    const int left=int(std::floor(source_x));
    const int top=int(std::floor(source_y));
    const float fraction_x=source_x-left;
    const float fraction_y=source_y-top;
    const float upper=source_alpha(left,top)*(1-fraction_x)+source_alpha(left+1,top)*fraction_x;
    const float lower=source_alpha(left,top+1)*(1-fraction_x)+source_alpha(left+1,top+1)*fraction_x;
    return (upper*(1-fraction_y)+lower*fraction_y)*figure_opacity;
}

float coverage_at(float x,float y) {
    for (size_t i=0;i<pane_count;i++) {
        const auto &pane=panes[i];
        if (x>=pane.x && x<pane.x+pane.w && y>=pane.y && y<pane.y+pane.h) {
            return 1;
        }
    }
    return figure_coverage_at(x,y,0.5f/render_density::density(screen));
}

#ifndef __PROSPERO__
std::string audit_path;
void capture_before() {
    if (audit_path.empty()) {
        return;
    }
    SDL_SaveBMP(screen,(audit_path+"-before.bmp").c_str());
    if (figure) {
        IMG_SavePNG(figure,(audit_path+"-figure.png").c_str());
    }
    FILE *file=fopen((audit_path+".json").c_str(),"w");
    if (!file) {
        return;
    }
    fprintf(file,"{\"panes\":[");
    for (size_t i=0;i<pane_count;i++) {
        const auto &pane=panes[i];
        fprintf(file,"%s[%d,%d,%d,%d]",i?",":"",pane.x,pane.y,pane.w,pane.h);
    }
    fprintf(file,"],\"figure\":%s,\"figure_size\":[%d,%d],\"figure_bounds\":[%d,%d,%d,%d],\"figure_opacity\":%.8f}\n",
        figure?"true":"false",figure?figure->w:0,figure?figure->h:0,
        figure_bounds.x,figure_bounds.y,figure_bounds.w,figure_bounds.h,figure_opacity);
    fclose(file);
}

void capture_after() {
    if (!audit_path.empty()) {
        SDL_SaveBMP(screen,(audit_path+"-after.bmp").c_str());
        audit_path.clear();
    }
}
#endif
}
