#pragma once
#include "xui_runtime.hpp"
#include "xui_raster_avx2.hpp"
#include "xui_text.hpp"
#include "xui_image.hpp"
#include <functional>
#include <cstdlib>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <limits>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace xui {
class Renderer {
    struct Inverse {
        float a,b,c,d,x,y;bool valid;
        explicit Inverse(const Matrix &m) {
            const float det=m.a[0][0]*m.a[1][1]-m.a[0][1]*m.a[1][0];valid=std::abs(det)>1e-10f;
            const float inv=valid?1/det:0;
            a=m.a[1][1]*inv;b=-m.a[0][1]*inv;c=-m.a[1][0]*inv;d=m.a[0][0]*inv;
            x=-a*m.a[0][3]-b*m.a[1][3];y=-c*m.a[0][3]-d*m.a[1][3];
        }
        Point point(float px,float py) const {return {a*px+b*py+x,c*px+d*py+y};}
    };
    struct Scratch {
        std::vector<Point> polygon;
        std::vector<float> scan_crossings,coverage;
        std::vector<int> scan_counts;
        size_t scan_stride=0;
        std::vector<SDL_Rect> clips;
    };
    std::vector<Scratch> scratch;
    std::vector<std::array<uint32_t,4097>> gradient_tables;
    std::vector<Stop> cached_stops;
    std::vector<uint8_t> gradient_valid,native_tabs;
    std::vector<std::thread> workers;
    std::mutex job_mutex;
    std::condition_variable job_start,job_done;
    bool stopping=false;
    uint64_t generation=0;
    size_t remaining=0;
    // Work is handed out in bands (several per worker) so uneven rows balance.
    std::atomic<int> next_band{0};int band_count=1;
    Scene *job_scene=nullptr;
    Matrix job_view;
    bool job_text=true;
    std::vector<SDL_Surface *> text_surfaces;
    std::map<std::pair<std::string,int>,TTF_Font *> fonts;
    std::vector<std::string> text_keys;
    std::vector<SDL_Surface *> bitmap_surfaces;
    // Nine-grid images (kind 6, the launch Guide's panels): each is rebuilt at
    // 2x its node size when the bitmap or size changes; margins in ImageMode.
    std::vector<SDL_Surface *> nine_surfaces;
    std::vector<std::array<float,3>> nine_keys;
    static constexpr float nine_oversample=2;
    static SDL_Surface *nine_slice(const SDL_Surface *src,float w,float h,const float *margin) {
        const int ow=std::max(1,int(std::lround(w*nine_oversample))),oh=std::max(1,int(std::lround(h*nine_oversample)));
        if (size_t(ow)*oh>16u*1024*1024) return nullptr;
        auto *out=SDL_CreateRGBSurfaceWithFormat(0,ow,oh,32,SDL_PIXELFORMAT_ARGB8888);
        if (!out) return nullptr;
        const float sl=margin[0],st=margin[1],sr=margin[2],sb=margin[3];
        const float dl=std::min(sl,w/2),dt=std::min(st,h/2),dr=std::min(sr,w/2),db=std::min(sb,h/2);
        // Maps a destination coordinate (node units) to the source along one axis.
        auto axis=[](float d,float size,float a,float b,float src,float da,float db) {
            if (d<da) return d*a/std::max(1e-3f,da);
            if (d>size-db) return src-(size-d)*b/std::max(1e-3f,db);
            const float mid=std::max(1e-3f,size-da-db);return a+(d-da)/mid*std::max(0.f,src-a-b);
        };
        for (int y=0;y<oh;y++) {
            auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(out->pixels)+y*out->pitch);
            const float sy=axis((y+.5f)/nine_oversample,h,st,sb,float(src->h),dt,db);
            for (int x=0;x<ow;x++) {
                const float sx=axis((x+.5f)/nine_oversample,w,sl,sr,float(src->w),dl,dr);
                row[x]=bilinear(src,sx-.5f,sy-.5f);
            }
        }
        SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
        return out;
    }
    std::vector<uint32_t> bitmap_tokens;
    std::vector<Point> text_origins;
    std::string default_font;
    std::map<std::string,std::string> font_aliases;
    std::function<SDL_Surface *(const std::string &)> bitmap_loader;
    std::map<std::string,SDL_Surface *> bitmaps;
    static constexpr float text_oversample=8;
    void update_resources(const Scene &scene) {
        for (size_t i=0;i<scene.nodes.size();i++) {
            const auto &n=scene.nodes[i];const auto &w=scene.world[i];const auto *p=scene.state[i].data();
            if (!w.visible || native_tabs[i]) continue;
            if (n.kind==3 || n.kind==4 || n.kind==1 || n.kind==6) {
                uint32_t token=p[Texture].token;
                if (n.kind==4 && !*scene.str(token)) for (int parent=n.parent;parent>=0;parent=scene.nodes[parent].parent)
                    if (*scene.str(scene.state[parent][Texture].token)) {token=scene.state[parent][Texture].token;break;}
                if (bitmap_tokens[i]!=token) {
                    bitmap_tokens[i]=token;const std::string name=scene.str(token);
                    auto found=bitmaps.find(name);
                    if (!name.empty() && found==bitmaps.end() && bitmap_loader) found=bitmaps.emplace(name,bitmap_loader(name)).first;
                    bitmap_surfaces[i]=found==bitmaps.end()?nullptr:found->second;
                }
                if (n.kind==6) {
                    const std::array<float,3> key{w.width,w.height,float(token)};
                    if (nine_keys[i]!=key) {
                        nine_keys[i]=key;SDL_FreeSurface(nine_surfaces[i]);nine_surfaces[i]=nullptr;
                        if (bitmap_surfaces[i] && w.width>0 && w.height>0) nine_surfaces[i]=nine_slice(bitmap_surfaces[i],w.width,w.height,p[ImageMode].v);
                    }
                }
            }
            if (n.kind!=2 && n.kind!=5) continue;
            uint32_t text_token=p[Text].token;
            if (n.kind==5 && !*scene.str(text_token)) for (int parent=n.parent;parent>=0;parent=scene.nodes[parent].parent)
                if (*scene.str(scene.state[parent][Text].token)) {text_token=scene.state[parent][Text].token;break;}
            const std::string value=scene.str(text_token),alias=scene.str(p[Font].token);
            const auto face=font_aliases.find(alias);const std::string font_path=face==font_aliases.end()?default_font:face->second;
            const int size=std::max(1,int(std::lround(p[PointSize].v[0]*4/3*text_oversample)));
            const float scale=p[TextScale].v[0]>0?p[TextScale].v[0]:1;
            const int flags=int(p[TextStyle].v[0]);
            const std::string key=value+"|"+font_path+"|"+std::to_string(size)+"|"+std::to_string(flags)+"|"+
                std::to_string(w.width)+"|"+std::to_string(w.height)+"|"+std::to_string(p[LineSpacing].v[0])+"|"+std::to_string(scale);
            if (text_keys[i]==key) continue;
            text_keys[i]=key;SDL_FreeSurface(text_surfaces[i]);text_surfaces[i]=nullptr;
            if (value.empty() || w.width<=0 || w.height<=0) continue;
            const auto font_key=std::make_pair(font_path,size);auto found=fonts.find(font_key);
            if (found==fonts.end()) found=fonts.emplace(font_key,TTF_OpenFont(font_path.c_str(),size)).first;
            auto *font=found->second;if (!font) continue;
            // TextStyle bit 0 selects the original shadow shader. Font weight is
            // selected by the actual face resource; never synthesize bold here.
            const auto measure=[&](const std::string &line) {int width=0;TTF_SizeUTF8(font,line.c_str(),&width,nullptr);return float(width)/text_oversample*scale;};
            const auto lines=layout_text(value,w.width,flags,measure);
            const float advance=float(TTF_FontLineSkip(font))/text_oversample*scale+p[LineSpacing].v[0];
            const float line_height=float(TTF_FontHeight(font))/text_oversample*scale;
            const float total_height=line_height+std::max(0.f,advance)*float(lines.size()-1);
            const float y=(flags&TextVCenter)?(w.height-total_height)*.5f:0;
            float left=w.width,right=0;
            for (const auto &line:lines) {
                const float x=(flags&TextRight)?w.width-line.width:(flags&TextCenter)?(w.width-line.width)*.5f:0;
                left=std::min(left,x);right=std::max(right,x+line.width);
            }
            const int sw=std::clamp(int(std::ceil((right-left)/scale*text_oversample)),1,16384);
            const int sh=std::clamp(int(std::ceil(total_height/scale*text_oversample)),1,16384);
            if (size_t(sw)*sh>32u*1024*1024) continue;
            auto *surface=SDL_CreateRGBSurfaceWithFormat(0,sw,sh,32,SDL_PIXELFORMAT_ARGB8888);
            if (!surface) continue;
            SDL_FillRect(surface,nullptr,0);SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);
            for (size_t row=0;row<lines.size();row++) if (!lines[row].text.empty()) {
                auto *line=TTF_RenderUTF8_Blended(font,lines[row].text.c_str(),{255,255,255,255});
                if (!line) continue;
                const float x=(flags&TextRight)?w.width-lines[row].width:(flags&TextCenter)?(w.width-lines[row].width)*.5f:0;
                SDL_Rect dest{int(std::lround((x-left)/scale*text_oversample)),int(std::lround(float(row)*advance/scale*text_oversample)),0,0};
                SDL_SetSurfaceBlendMode(line,SDL_BLENDMODE_NONE);SDL_BlitSurface(line,nullptr,surface,&dest);SDL_FreeSurface(line);
            }
            text_surfaces[i]=surface;text_origins[i]={left,y};
        }
    }
    int width=0,height=0;
    bool vector_raster=false;
    static SDL_Rect bounds(const Matrix &m,float w,float h) {
        auto lo=Point{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};auto hi=Point{-lo.x,-lo.y};
        for (auto p:{m.point(0,0),m.point(w,0),m.point(w,h),m.point(0,h)}) {lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
        const int left=int(std::floor(lo.x)),top=int(std::floor(lo.y));
        return {left,top,int(std::ceil(hi.x))-left,int(std::ceil(hi.y))-top};
    }
    static uint32_t blend(uint32_t dst,uint32_t src,float alpha,int mode) {
        return composite(dst,src,alpha,1,mode);
    }
    static uint32_t gradient(const Scene &scene,const Node &n,float phase) {
        if (!n.stop_count) return 0;
        const Stop *stops=scene.stops.data()+n.stop_first;
        if (phase<=stops[0].position) return stops[0].color;
        for (int i=1;i<n.stop_count;i++) if (phase<=stops[i].position) {
            const float span=stops[i].position-stops[i-1].position;
            const float t=span>0?std::clamp((phase-stops[i-1].position)/span,0.f,1.f):1;
            uint32_t color=0;
            for (int shift=0;shift<32;shift+=8) {
                const float a=float((stops[i-1].color>>shift)&255),b=float((stops[i].color>>shift)&255);
                color|=uint32_t(std::clamp(int(std::lround(a+(b-a)*t)),0,255))<<shift;
            }
            return color;
        }
        return stops[n.stop_count-1].color;
    }
    void figure(const Scene &scene,int index,const Matrix &view,SDL_Surface *target,const SDL_Rect &clip,Scratch &work) {
        auto &polygon=work.polygon;auto &scan_counts=work.scan_counts;auto &scan_crossings=work.scan_crossings;
        auto &coverage=work.coverage;const auto &gradient_table=gradient_tables[index];const size_t scan_stride=work.scan_stride;
        const Node &n=scene.nodes[index];const World &world=scene.world[index];const auto *p=scene.state[index].data();
        if (!n.fill_type || n.point_count<3 || n.path_w<=0 || n.path_h<=0 || world.width<=0 || world.height<=0) return;
        const Matrix m=view*world.matrix*Matrix::scale(world.width/n.path_w,world.height/n.path_h);
        const Inverse inverse(m);if (!inverse.valid) return;
        polygon.resize(n.point_count);
        float lx=float(width),ly=float(height),rx=0,by=0;
        for (int i=0;i<n.point_count;i++) {
            const auto &v=scene.points[n.point_first+i];auto &q=polygon[i];q=m.point(v.x,v.y);
            lx=std::min(lx,q.x);ly=std::min(ly,q.y);rx=std::max(rx,q.x);by=std::max(by,q.y);
        }
        const int x0=std::max(clip.x,int(std::floor(lx))),x1=std::min(clip.x+clip.w,int(std::ceil(rx)));
        const int y0=std::max(clip.y,int(std::floor(ly))),y1=std::min(clip.y+clip.h,int(std::ceil(by)));
        if (x0>=x1 || y0>=y1) return;
        const float rotation=p[FillRotation].v[0]*3.14159265359f/180,cs=std::cos(rotation),sn=std::sin(rotation);
        const bool gradient_fill=n.fill_type==2 || n.fill_type==3;
        const float sx=gradient_fill?p[FillScale].v[0]:std::max(std::abs(p[FillScale].v[0]),1e-5f);
        const float sy=gradient_fill?p[FillScale].v[1]:std::max(std::abs(p[FillScale].v[1]),1e-5f);
        const float tx=p[FillTranslation].v[0],ty=p[FillTranslation].v[1];
        const int mode=world.blend;
        const auto *bitmap=bitmap_surfaces[index];
        const float ux=inverse.a/n.path_w,uy=inverse.b/n.path_w,uc=inverse.x/n.path_w-.5f;
        const float vx=inverse.c/n.path_h,vy=inverse.d/n.path_h,vc=inverse.y/n.path_h-.5f;
        const float gx=gradient_fill?ux*cs*sx-vx*sn*sy:(ux*cs-vx*sn)*sx;
        const float gy=gradient_fill?uy*cs*sx-vy*sn*sy:(uy*cs-vy*sn)*sx;
        const float gc=gradient_fill?uc*cs*sx-vc*sn*sy+tx:(uc*cs-vc*sn)*sx+tx;
        const float hx=gradient_fill?ux*sn*sx+vx*cs*sy:(ux*sn+vx*cs)*sy;
        const float hy=gradient_fill?uy*sn*sx+vy*cs*sy:(uy*sn+vy*cs)*sy;
        const float hc=gradient_fill?uc*sn*sx+vc*cs*sy+ty:(uc*sn+vc*cs)*sy+ty;

        // Bin intersections along each edge once. Testing every curve segment on
        // every scan line made the source geometry prohibitively expensive.
        std::fill(scan_counts.begin()+y0*2,scan_counts.begin()+y1*2,0);
        for (size_t j=0;j<polygon.size();j++) {
            auto a=polygon[j],b=polygon[(j+1)%polygon.size()];
            if (a.y>b.y) std::swap(a,b);
            if (b.y-a.y<1e-8f) continue;
            const float slope=(b.x-a.x)/(b.y-a.y);
            for (int sub=0;sub<2;sub++) {
                const float offset=sub==0?.25f:.75f;
                const int first=std::max(y0,int(std::ceil(a.y-offset)));
                const int last=std::min(y1,int(std::ceil(b.y-offset)));
                for (int y=first;y<last;y++) {
                    const size_t row=size_t(y*2+sub);
                    const int at=scan_counts[row]++;
                    scan_crossings[row*scan_stride+size_t(at)]=a.x+(float(y)+offset-a.y)*slope;
                }
            }
        }
        for (int y=y0;y<y1;y++) {
            // The row's spans for both sub-scanlines, and the pixels they touch.
            int lo=x1,hi=x0;
            float *crossing_rows[2];int counts[2];
            for (int sub=0;sub<2;sub++) {
                const size_t scan=size_t(y*2+sub);float *crossings=scan_crossings.data()+scan*scan_stride;
                const int count=scan_counts[scan];std::sort(crossings,crossings+count);
                crossing_rows[sub]=crossings;counts[sub]=count;
                for (int j=0;j+1<count;j+=2) {
                    const float left=std::max(float(x0),crossings[j]),right=std::min(float(x1),crossings[j+1]);
                    const int first=std::max(x0,int(std::floor(left))),last=std::min(x1,int(std::ceil(right)));
                    if (first<last) {lo=std::min(lo,first);hi=std::max(hi,last);}
                }
            }
            if (lo>=hi) continue; // nothing on this row: the pixels stay as they are
            // Pixels strictly inside one span of each sub-scanline are exactly
            // 0.5 + 0.5; they are written in one fill instead of two passes.
            int inner_lo=hi,inner_hi=lo;
            if (counts[0]==2 && counts[1]==2) {
                int f[2],l[2];
                for (int sub=0;sub<2;sub++) {
                    const float left=std::max(float(x0),crossing_rows[sub][0]),right=std::min(float(x1),crossing_rows[sub][1]);
                    f[sub]=std::max(x0,int(std::floor(left)));l[sub]=std::min(x1,int(std::ceil(right)));
                }
                inner_lo=std::max(f[0],f[1])+1;inner_hi=std::min(l[0],l[1])-1;
                if (inner_hi<=inner_lo) {inner_lo=hi;inner_hi=lo;}
            }
            // Clear only what the spans will add into (the inside is written below).
            if (inner_lo<inner_hi) {
                std::fill(coverage.begin()+lo,coverage.begin()+inner_lo,0);
                std::fill(coverage.begin()+inner_hi,coverage.begin()+hi,0);
            } else std::fill(coverage.begin()+lo,coverage.begin()+hi,0);
            for (int sub=0;sub<2;sub++) {
                const float *crossings=crossing_rows[sub];const int count=counts[sub];
                for (int j=0;j+1<count;j+=2) {
                    const float left=std::max(float(x0),crossings[j]),right=std::min(float(x1),crossings[j+1]);
                    const int first=std::max(x0,int(std::floor(left))),last=std::min(x1,int(std::ceil(right)));
                    if (last-first<=2) {
                        for (int x=first;x<last;x++) coverage[x]+=.5f*std::max(0.f,std::min(right,float(x+1))-std::max(left,float(x)));
                        continue;
                    }
                    // Only the two end pixels are partial; every pixel between is exactly
                    // half covered by this sub-scanline (the same value the general
                    // formula gives, so results are bit-identical).
                    coverage[first]+=.5f*std::max(0.f,std::min(right,float(first+1))-std::max(left,float(first)));
                    float *inside=coverage.data();
                    if (inner_lo>=inner_hi) {
                        for (int x=first+1;x<last-1;x++) inside[x]+=.5f;
                    } else {
                        for (int x=first+1;x<std::min(last-1,inner_lo);x++) inside[x]+=.5f;
                        for (int x=std::max(first+1,inner_hi);x<last-1;x++) inside[x]+=.5f;
                    }
                    coverage[last-1]+=.5f*std::max(0.f,std::min(right,float(last))-std::max(left,float(last-1)));
                }
            }
            if (inner_lo<inner_hi) std::fill(coverage.begin()+inner_lo,coverage.begin()+inner_hi,1.f);
            auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(target->pixels)+y*target->pitch);
            const int processed=vector_raster && !bitmap?raster_avx2(row+lo,coverage.data()+lo,hi-lo,lo,gy*(float(y)+.5f),hy*(float(y)+.5f),gx,hx,gc,hc,
                n.stop_count && gradient_fill?gradient_table.data():nullptr,n.fill_type,p[FillColor].token,world.opacity,mode):0;
            for (int x=lo+processed;x<hi;x++) if (coverage[x]>0) {
                uint32_t color=p[FillColor].token;
                if (bitmap || (n.stop_count && gradient_fill)) {
                    const float gu=gx*(float(x)+.5f)+gy*(float(y)+.5f)+gc;
                    const float gv=hx*(float(x)+.5f)+hy*(float(y)+.5f)+hc;
                    const float phase=n.fill_type==3?2*std::sqrt(gu*gu+gv*gv):gu+.5f;
                    color=bitmap?bilinear(bitmap,(gu+.5f)*bitmap->w-.5f,(gv+.5f)*bitmap->h-.5f):
                        gradient_table[size_t(std::clamp(int(phase*4096+.5f),0,4096))];
                }
                row[x]=composite(row[x],color,world.opacity,coverage[x],mode);
            }
        }
    }
    void stroke(const Scene &scene,int index,const Matrix &view,const SDL_Rect &clip,Scratch &work) {
        const auto &n=scene.nodes[index];const auto &w=scene.world[index];const auto *p=scene.state[index].data();
        const float thickness=p[StrokeWidth].v[0];
        if (thickness<=0 || !(p[StrokeColor].token>>24) || n.point_count<2 || n.path_w<=0 || n.path_h<=0) return;
        const auto transform=view*w.matrix;const auto m=transform*Matrix::scale(w.width/n.path_w,w.height/n.path_h);
        const float radius=thickness*.25f*(std::hypot(transform.a[0][0],transform.a[1][0])+std::hypot(transform.a[0][1],transform.a[1][1]));
        auto &polygon=work.polygon;polygon.resize(n.point_count);
        float lo=float(height),hi=0;
        for (int i=0;i<n.point_count;i++) {const auto q=scene.points[n.point_first+i];polygon[i]=m.point(q.x,q.y);lo=std::min(lo,polygon[i].y);hi=std::max(hi,polygon[i].y);}
        const int first=std::max(clip.y,int(std::floor(lo-radius-1))),last=std::min(clip.y+clip.h,int(std::ceil(hi+radius+1)));
        for (int y=first;y<last;y++) {
            auto &coverage=work.coverage;std::fill(coverage.begin()+clip.x,coverage.begin()+clip.x+clip.w,0);
            int left=clip.x+clip.w,right=clip.x;
            for (int i=0;i<n.point_count-(n.closed?0:1);i++) {
                const auto a=polygon[i],b=polygon[(i+1)%n.point_count];
                if (float(y)+.5f<std::min(a.y,b.y)-radius-1 || float(y)+.5f>std::max(a.y,b.y)+radius+1) continue;
                const int x0=std::max(clip.x,int(std::floor(std::min(a.x,b.x)-radius-1))),x1=std::min(clip.x+clip.w,int(std::ceil(std::max(a.x,b.x)+radius+1)));
                left=std::min(left,x0);right=std::max(right,x1);
                const float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
                for (int x=x0;x<x1;x++) {
                    const float t=length>0?std::clamp(((x+.5f-a.x)*dx+(y+.5f-a.y)*dy)/length,0.f,1.f):0;
                    const float distance=std::hypot(x+.5f-a.x-t*dx,y+.5f-a.y-t*dy);
                    coverage[x]=std::max(coverage[x],std::clamp(radius+.5f-distance,0.f,1.f));
                }
            }
            auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(canvas->pixels)+y*canvas->pitch);
            for (int x=left;x<right;x++) if (coverage[x]>0) row[x]=composite(row[x],p[StrokeColor].token,w.opacity,coverage[x],w.blend);
        }
    }
    static uint32_t bilinear(const SDL_Surface *image,float x,float y) {
        const int ix=std::clamp(int(std::floor(x)),0,image->w-1),iy=std::clamp(int(std::floor(y)),0,image->h-1);
        const int ix1=std::min(ix+1,image->w-1),iy1=std::min(iy+1,image->h-1);
        const float fx=std::clamp(x-float(ix),0.f,1.f),fy=std::clamp(y-float(iy),0.f,1.f);
        const auto *a=reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(image->pixels)+iy*image->pitch);
        const auto *b=reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(image->pixels)+iy1*image->pitch);
        const uint32_t pixels[]={a[ix],a[ix1],b[ix],b[ix1]};const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
        float alpha=0,rgb[3]{};
        for (int i=0;i<4;i++) {const float w=weights[i]*float(pixels[i]>>24);alpha+=w;for (int c=0;c<3;c++) rgb[c]+=w*float((pixels[i]>>(c*8))&255);}
        if (alpha<.001f) return 0;
        uint32_t color=uint32_t(std::clamp(int(std::lround(alpha)),0,255))<<24;
        for (int c=0;c<3;c++) color|=uint32_t(std::clamp(int(std::lround(rgb[c]/alpha)),0,255))<<(c*8);
        return color;
    }
public:
    SDL_Surface *canvas=nullptr;
    uint64_t figures_drawn=0;
    ~Renderer() {clear();}
    void clear() {
        {std::lock_guard<std::mutex> lock(job_mutex);stopping=true;}job_start.notify_all();
        for (auto &worker:workers) if (worker.joinable()) worker.join();
        workers.clear();scratch.clear();gradient_tables.clear();cached_stops.clear();gradient_valid.clear();native_tabs.clear();stopping=false;generation=0;
        for (auto *s:text_surfaces) SDL_FreeSurface(s);
        text_surfaces.clear();
        for (auto &entry:fonts) TTF_CloseFont(entry.second);
        fonts.clear();
        for (auto &entry:bitmaps) SDL_FreeSurface(entry.second);
        bitmaps.clear();bitmap_tokens.clear();bitmap_surfaces.clear();text_keys.clear();text_origins.clear();
        for (auto *s:nine_surfaces) SDL_FreeSurface(s);
        nine_surfaces.clear();nine_keys.clear();
        SDL_FreeSurface(canvas);canvas=nullptr;
    }
    bool prepare(const Scene &scene,int w,int h,const char *font,unsigned requested_workers=0) {
        clear();width=w;height=h;vector_raster=supports_avx2() && !std::getenv("NXE_XUI_SCALAR");canvas=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if (!canvas) return false;
        SDL_SetSurfaceBlendMode(canvas,SDL_BLENDMODE_NONE);
        size_t max_points=4;for (const auto &n:scene.nodes) max_points=std::max(max_points,size_t(n.point_count));
        if (max_points>4096) return false;
        const size_t bytes_per_worker=size_t(h)*2*max_points*sizeof(float)+size_t(h)*2*sizeof(int)+size_t(w)*sizeof(float);
        const unsigned budget_workers=unsigned(std::max(size_t(1),(64u<<20)/bytes_per_worker));
        const size_t count=std::clamp(requested_workers?requested_workers:std::thread::hardware_concurrency(),1u,std::min(8u,budget_workers));
        scratch.resize(count);
        for (auto &work:scratch) {
            work.coverage.resize(w);work.clips.resize(scene.nodes.size());work.polygon.reserve(max_points);
            work.scan_stride=max_points;work.scan_counts.resize(size_t(h)*2);
            work.scan_crossings.resize(size_t(h)*2*max_points);
        }
        default_font=font;bitmap_tokens.assign(scene.nodes.size(),~0u);bitmap_surfaces.assign(scene.nodes.size(),nullptr);
        nine_surfaces.assign(scene.nodes.size(),nullptr);nine_keys.assign(scene.nodes.size(),{-1.f,-1.f,-1.f});
        text_surfaces.assign(scene.nodes.size(),nullptr);text_keys.assign(scene.nodes.size(),{});text_origins.resize(scene.nodes.size());
        gradient_tables.resize(scene.nodes.size());cached_stops=scene.original_stops;
        gradient_valid.assign(scene.nodes.size(),0);native_tabs.assign(scene.nodes.size(),0);
        for (size_t i=0;i<scene.nodes.size();i++) {
            const auto &n=scene.nodes[i];const char *id=scene.str(n.id);
            native_tabs[i]=(n.parent>=0 && native_tabs[n.parent]) ||
                (!std::strncmp(id,"Tab",3) && id[3]>='1' && id[3]<='6' && !id[4]);
        }
        for (size_t i=1;i<count;i++) workers.emplace_back([this,i] {
            uint64_t seen=0;
            while (true) {
                std::unique_lock<std::mutex> lock(job_mutex);
                job_start.wait(lock,[&]{return stopping || generation!=seen;});
                if (stopping) return;
                seen=generation;lock.unlock();run_bands(*job_scene,job_view,job_text,i);
                lock.lock();if (--remaining==0) job_done.notify_one();
            }
        });
        return true;
    }
    void texture(SDL_Surface *target,const SDL_Surface *image,const Matrix &matrix,float opacity,const SDL_Rect &clip,uint32_t tint=0xffffffffu,int mode=Normal) {
        if (!image || opacity<=0) return;
        const Inverse inverse(matrix);if (!inverse.valid) return;
        SDL_Rect area,box=bounds(matrix,float(image->w),float(image->h));
        if (!SDL_IntersectRect(&box,&clip,&area)) return;
        for (int y=area.y;y<area.y+area.h;y++) {
            auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(target->pixels)+y*target->pitch);
            for (int x=area.x;x<area.x+area.w;x++) {
                const auto p=inverse.point(float(x)+.5f,float(y)+.5f);
                if (p.x<0 || p.y<0 || p.x>=image->w || p.y>=image->h) continue;
                uint32_t color=bilinear(image,p.x-.5f,p.y-.5f);
                if (tint!=0xffffffffu) {
                    uint32_t modulated=0;
                    for (int shift=0;shift<32;shift+=8) modulated|=uint32_t((((color>>shift)&255)*((tint>>shift)&255)+127)/255)<<shift;
                    color=modulated;
                }
                row[x]=blend(row[x],color,opacity,mode);
            }
        }
    }
    void bind_font(const std::string &alias,const std::string &path) {font_aliases[alias]=path;std::fill(text_keys.begin(),text_keys.end(),std::string{});}
    // Loader transfers ownership of ARGB8888 surfaces. Dynamic changes are
    // resolved before worker dispatch; no I/O or SDL_ttf calls race painting.
    void bind_bitmaps(std::function<SDL_Surface *(const std::string &)> loader) {bitmap_loader=std::move(loader);std::fill(bitmap_tokens.begin(),bitmap_tokens.end(),~0u);}
    void bind_bitmap(const std::string &name,SDL_Surface *surface) {
        auto found=bitmaps.find(name);if (found!=bitmaps.end()) SDL_FreeSurface(found->second);
        bitmaps[name]=surface;std::fill(bitmap_tokens.begin(),bitmap_tokens.end(),~0u);
    }
    void run_bands(Scene &scene,const Matrix &view,bool text,size_t worker) {
        for (int band;(band=next_band.fetch_add(1))<band_count;)
            render_strip(scene,view,text,worker,height*band/band_count,height*(band+1)/band_count);
    }
    void render_strip(Scene &scene,const Matrix &view,bool text,size_t strip,int top,int bottom) {
        auto &work=scratch[strip];auto &clips=work.clips;
        const SDL_Rect viewport{0,top,width,bottom-top};
        for (size_t i=0;i<scene.nodes.size();i++) {
            const Node &n=scene.nodes[i];const auto &w=scene.world[i];
            clips[i]=n.parent>=0?clips[n.parent]:viewport;
            if (n.parent>=0 && scene.nodes[n.parent].clip) {
                const auto &parent=scene.world[n.parent];const auto box=bounds(view*parent.matrix,parent.width,parent.height);
                SDL_Rect intersection{};SDL_IntersectRect(&clips[i],&box,&intersection);clips[i]=intersection;
            }
            if (!w.visible || clips[i].w<=0 || clips[i].h<=0) continue;
            // Dashboard containers are native PS5 content; their scene state remains evaluated.
            if (native_tabs[i]) continue;
            if (n.kind==1) {figure(scene,int(i),view,canvas,clips[i],work);stroke(scene,int(i),view,clips[i],work);}
            else if (text && (n.kind==2 || n.kind==5) && text_surfaces[i]) {
                const auto *p=scene.state[i].data();const float scale=p[TextScale].v[0]>0?p[TextScale].v[0]:1;
                const auto box=bounds(view*w.matrix,w.width,w.height);SDL_Rect clip{};
                if (!SDL_IntersectRect(&box,&clips[i],&clip)) continue;
                const auto origin=text_origins[i];const auto matrix=view*w.matrix*Matrix::translate(origin.x,origin.y);
                const auto raster=Matrix::scale(scale/text_oversample,scale/text_oversample);
                if ((int(p[TextStyle].v[0])&TextShadow) && p[ShadowColor].token>>24)
                    texture(canvas,text_surfaces[i],matrix*Matrix::translate(1,1)*raster,w.opacity,clip,p[ShadowColor].token,w.blend);
                texture(canvas,text_surfaces[i],matrix*raster,w.opacity,clip,p[TextColor].token,w.blend);
            } else if (n.kind==6 && nine_surfaces[i]) {
                const uint32_t factor=scene.state[i][ShadowColor].token;
                texture(canvas,nine_surfaces[i],view*w.matrix*Matrix::scale(1/nine_oversample,1/nine_oversample),w.opacity,clips[i],(factor>>24)?factor:0xffffffffu,w.blend);
            } else if ((n.kind==3 || n.kind==4) && bitmap_surfaces[i]) {
                const auto *image=bitmap_surfaces[i];
                const auto placement=image_placement(w.width,w.height,float(image->w),float(image->h),unsigned(scene.state[i][ImageMode].v[0]));
                const auto matrix=view*w.matrix*Matrix::translate(placement.x,placement.y)*Matrix::scale(placement.width/image->w,placement.height/image->h);
                SDL_Rect clip=clips[i];
                if (placement.clip) {const auto box=bounds(view*w.matrix,w.width,w.height);SDL_Rect intersection{};SDL_IntersectRect(&box,&clip,&intersection);clip=intersection;}
                // Images take a colour factor (XUIB 8 skins) in their otherwise unused shadow slot.
                const uint32_t factor=scene.state[i][ShadowColor].token;
                texture(canvas,image,matrix,w.opacity,clip,(factor>>24)?factor:0xffffffffu,w.blend);
            }
        }
    }
    void draw(Scene &scene,const Matrix &view,bool text=true,SDL_Surface *backdrop=nullptr) {
        if (!canvas || !scene.valid) return;
        scene.evaluate();
        // Offline component conversion can retain an existing opaque material
        // under original rails. This is not a transparent-layer compositor;
        // ordinary scene callers keep their unchanged default clear color.
        if (backdrop && backdrop->w==width && backdrop->h==height) {
            SDL_BlitSurface(backdrop,nullptr,canvas,nullptr);
        } else {
            SDL_FillRect(canvas,nullptr,0xffbfc2c4u);
        }
        figures_drawn=0;
        for (size_t i=0;i<scene.nodes.size();i++) {
            const auto &n=scene.nodes[i];
            if (!n.stop_count || native_tabs[i] || !scene.world[i].visible) continue;
            const Stop *current=scene.stops.data()+n.stop_first;Stop *cached=cached_stops.data()+n.stop_first;
            if (gradient_valid[i] && std::memcmp(current,cached,size_t(n.stop_count)*sizeof(Stop))==0) continue;
            auto &table=gradient_tables[i];
            for (size_t k=0;k<table.size();k++) table[k]=gradient(scene,n,float(k)/4096);
            std::copy(current,current+n.stop_count,cached);gradient_valid[i]=1;
        }
        update_resources(scene);
        {
            std::lock_guard<std::mutex> lock(job_mutex);job_scene=&scene;job_view=view;job_text=text;
            remaining=workers.size();++generation;
            band_count=scratch.size()>1?int(scratch.size())*4:1;next_band=0;
        }
        job_start.notify_all();run_bands(scene,view,text,0);
        std::unique_lock<std::mutex> lock(job_mutex);job_done.wait(lock,[&]{return remaining==0;});
        for (size_t i=0;i<scene.nodes.size();i++) if (scene.nodes[i].kind==1 && scene.world[i].visible) figures_drawn++;
    }
};
}
