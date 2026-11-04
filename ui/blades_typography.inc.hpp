// Included inside main.cpp's private namespace; see docs/BLADES_TYPOGRAPHY.md.
namespace blades_type {
constexpr int tracking=1;
SDL_Surface *rail_surface(const std::string &value,TTF_Font *font) {
    if (!font || value.empty()) return nullptr;
    // Original presenter foreground/shadow bytes, not a synthetic bold face.
    // Render the whole run so font kerning remains intact and body tracking
    // does not leak into the narrow rails. Source alpha is composed explicitly.
    auto *glyph=TTF_RenderUTF8_Blended(font,value.c_str(),{255,255,255,255});
    if (!glyph) return nullptr;
    auto *result=SDL_CreateRGBSurfaceWithFormat(0,glyph->w+1,glyph->h+1,32,SDL_PIXELFORMAT_ARGB8888);
    if (result) {
        SDL_FillRect(result,nullptr,0);
        // One flat-surface pixel is a calibrated shadow offset. Source colors
        // are recovered; the Xbox shader's final offset is not verified here.
        SDL_LockSurface(glyph);
        SDL_LockSurface(result);
        const auto coverage=[&](int x,int y) -> Uint8 {
            if (x<0 || y<0 || x>=glyph->w || y>=glyph->h) return 0;
            Uint32 pixel=0;
            std::memcpy(&pixel,static_cast<const Uint8 *>(glyph->pixels)+y*glyph->pitch+x*glyph->format->BytesPerPixel,
                size_t(glyph->format->BytesPerPixel));
            Uint8 red,green,blue,alpha;
            SDL_GetRGBA(pixel,glyph->format,&red,&green,&blue,&alpha);
            return alpha;
        };
        for (int y=0;y<result->h;y++) {
            auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(result->pixels)+y*result->pitch);
            for (int x=0;x<result->w;x++) {
                const auto pixel=blades_rails::composite_coverage(coverage(x,y),coverage(x-1,y-1));
                row[x]=SDL_MapRGBA(result->format,pixel.gray,pixel.gray,pixel.gray,pixel.alpha);
            }
        }
        SDL_UnlockSurface(result);
        SDL_UnlockSurface(glyph);
        SDL_SetSurfaceBlendMode(result,SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(glyph);
    return result;
}
struct Line {
    TTF_Font *font;
    std::string value;
    Uint32 color;
    bool shadow;
    SDL_Surface *image;
    size_t used;
};
std::vector<Line> lines;
size_t serial=0,bytes=0;
std::vector<std::string> characters(const std::string &value) {
    std::vector<std::string> result;
    for (size_t i=0;i<value.size();) {
        size_t end=i+1;
        while (end<value.size() && (static_cast<unsigned char>(value[end])&0xc0)==0x80) ++end;
        result.push_back(value.substr(i,end-i));i=end;
    }
    return result;
}
int width(const std::string &value,TTF_Font *font) {
    const auto count=characters(value).size();
    return text_width(value,font)+(count?int(count-1)*tracking:0);
}
std::string fit(const std::string &value,TTF_Font *font,int limit) {
    if (width(value,font)<=limit) return value;
    auto chars=characters(value);
    std::string cut=value;
    while (!chars.empty() && width(cut+"...",font)>limit) {
        cut.resize(cut.size()-chars.back().size());chars.pop_back();
    }
    return width(cut+"...",font)<=limit?cut+"...":"";
}
SDL_Surface *surface(const std::string &value,TTF_Font *font,SDL_Color ink,bool shadow=true) {
    if (!font || value.empty()) return nullptr;
    const Uint32 color=(Uint32(ink.r)<<24)|(Uint32(ink.g)<<16)|(Uint32(ink.b)<<8)|ink.a;
    for (auto &line:lines) if (line.font==font && line.value==value && line.color==color && line.shadow==shadow) {
        line.used=++serial;return line.image;
    }
    auto *image=SDL_CreateRGBSurfaceWithFormat(0,width(value,font)+2,TTF_FontHeight(font)+1,32,SDL_PIXELFORMAT_ARGB8888);
    if (!image) return nullptr;
    SDL_FillRect(image,nullptr,0);
    const auto chars=characters(value);
    std::string prefix;
    const bool light=ink.r>200 && ink.g>200 && ink.b>200;
    const SDL_Color shade=light?SDL_Color{15,15,15,190}:SDL_Color{235,235,235,90};
    for (int pass=shadow?0:1;pass<2;++pass) {
        prefix.clear();
        for (size_t i=0;i<chars.size();++i) {
            auto *glyph=TTF_RenderUTF8_Blended(font,chars[i].c_str(),pass?ink:shade);
            if (glyph) {
                SDL_Rect at{text_width(prefix,font)+int(i)*tracking+(pass?0:1),pass?0:1,0,0};
                SDL_BlitSurface(glyph,nullptr,image,&at);SDL_FreeSurface(glyph);
            }
            prefix+=chars[i];
        }
    }
    const size_t cost=size_t(image->pitch)*image->h;
    if (cost>(4u<<20)) {SDL_FreeSurface(image);return nullptr;}
    while (!lines.empty() && (lines.size()>=64 || bytes+cost>(4u<<20))) {
        auto oldest=std::min_element(lines.begin(),lines.end(),[](const Line &a,const Line &b){return a.used<b.used;});
        bytes-=size_t(oldest->image->pitch)*oldest->image->h;
        SDL_FreeSurface(oldest->image);lines.erase(oldest);
    }
    SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
    lines.push_back({font,value,color,shadow,image,++serial});bytes+=cost;
    return image;
}
int draw(const std::string &value,int x,int y,TTF_Font *font,SDL_Color ink) {
    if (auto *image=surface(value,font,ink)) {
        SDL_SetSurfaceAlphaMod(image,text_alpha);SDL_Rect at{x,y,0,0};
        SDL_BlitSurface(image,nullptr,screen,&at);SDL_SetSurfaceAlphaMod(image,255);
    }
    return width(value,font);
}
SDL_Surface *ink_copy(SDL_Surface *source) {
    if (!source) return nullptr;
    int left=source->w,top=source->h,right=0,bottom=0;
    SDL_LockSurface(source);
    for (int y=0;y<source->h;++y) {
        const auto *row=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(source->pixels)+y*source->pitch);
        for (int x=0;x<source->w;++x) if (row[x]>>24) {
            left=std::min(left,x);top=std::min(top,y);
            right=std::max(right,x+1);bottom=std::max(bottom,y+1);
        }
    }
    SDL_Surface *copy=nullptr;
    if (left<right && top<bottom) {
        copy=SDL_CreateRGBSurfaceWithFormat(0,right-left,bottom-top,32,SDL_PIXELFORMAT_ARGB8888);
        if (copy) {
            SDL_LockSurface(copy);
            for (int y=0;y<copy->h;++y)
                std::memcpy(static_cast<Uint8 *>(copy->pixels)+y*copy->pitch,
                    static_cast<const Uint8 *>(source->pixels)+(top+y)*source->pitch+left*4,size_t(copy->w)*4);
            SDL_UnlockSurface(copy);SDL_SetSurfaceBlendMode(copy,SDL_BLENDMODE_BLEND);
        }
    }
    SDL_UnlockSurface(source);return copy;
}
void wrapped(const std::string &value,int x,int y,int limit,int max_lines,TTF_Font *font,SDL_Color ink) {
    std::string line,word;int row=0;
    auto place=[&]() {if (!line.empty() && row<max_lines) draw(fit(line,font,limit),x,y+row++*49,font,ink);line.clear();};
    for (size_t i=0;i<=value.size();++i) {
        const char c=i<value.size()?value[i]:' ';
        if (c!=' ' && c!='\n') {word+=c;continue;}
        if (!word.empty()) {
            const auto next=line.empty()?word:line+" "+word;
            if (!line.empty() && width(next,font)>limit) place();
            if (!line.empty()) line+=' ';
            line+=word;word.clear();
        }
        if (c=='\n') {if (line.empty()) ++row; else place();} // a blank line leaves a gap
    }
    place();
}
void clear() {
    for (auto &line:lines) SDL_FreeSurface(line.image);
    lines.clear();bytes=0;
}
#ifndef __PROSPERO__
void audit_rails(const char *path,bool all_regimes=false) {
    FILE *file=path?std::fopen(path,"w"):nullptr;
    if (!file) return;
    if (all_regimes) std::fprintf(file,"build\tsource_points\tfont_pixels\t");
    std::fprintf(file,"early\tactive\tblade\tleft\tright\tnotch\tx\ty\twidth\theight\tleft_gap\tright_gap\ttop_gap\n");
    for (int build:{1888,5766,6717}) {
        if (!all_regimes && build==5766) continue;
        const bool early=build<5759;
        const int family=blades_rails::family(build);
        for (int active=early?1:0;active<5;++active) {
            for (int blade=early?1:0;blade<5;++blade) {
                auto *image=blades_labels[family][blade==active][blade];
                if (!image) continue;
                const auto slot=era_chrome::rail_label(active,blade,early);
                const int x=slot.center-(image->w+1)/2;
                if (all_regimes) {
                    std::fprintf(file,"%d\t%d\t%d\t",build,blades_rails::source_points(build),
                        blades_rails::native_pixels(build,blade==active));
                }
                std::fprintf(file,"%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                    int(early),active,blade,slot.left,slot.right,slot.notch,x,slot.top,image->w,image->h,
                    x-slot.left,slot.right-x-image->w,slot.top-slot.notch);
            }
        }
    }
    std::fclose(file);
}
#endif
}
