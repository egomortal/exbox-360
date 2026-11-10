#pragma once
#include "xui_runtime.hpp"
#include "publication_registry.hpp"
#include "xui_text.hpp"
#include "xui_image.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <functional>
#include <map>
#include <atomic>
#include <limits>
#include <stdexcept>
namespace xui {
class GpuResources {
 uint64_t serial=0;
 inline static std::atomic<uint64_t> next_owner{1};
 uint64_t next_generation(){if(serial==UINT64_MAX)throw std::overflow_error("resource generation exhausted");return ++serial;}
public:
 const uint64_t owner=next_owner.fetch_add(1);
 std::vector<SDL_Surface*> text_surfaces,bitmap_surfaces;
 std::vector<Point> text_origins;
 std::vector<std::string> text_keys;
 std::vector<uint32_t> bitmap_tokens;
 std::vector<uint64_t> text_generations,bitmap_generations,gradient_generations;
 std::vector<uint8_t> native_tabs,gradient_valid;
 std::vector<std::array<uint32_t,4097>> gradient_tables;
 std::vector<std::vector<Stop>> cached_stops;
 std::map<std::pair<std::string,int>,TTF_Font*> fonts;
 std::map<std::string,std::string> font_aliases;
 std::map<std::string,SDL_Surface*> bitmaps;
 std::function<SDL_Surface*(const std::string&)> bitmap_loader;
 std::string default_font;
 static constexpr float text_oversample=8;
 GpuResources()=default;
 GpuResources(const GpuResources&)=delete;
 GpuResources& operator=(const GpuResources&)=delete;
 ~GpuResources(){clear();}
 void clear(){
  x21source::generations.retire_owner(owner|(uint64_t(1)<<63));
  for(auto*s:text_surfaces)SDL_FreeSurface(s);
  for(auto&e:fonts)if(e.second)TTF_CloseFont(e.second);
  for(auto&e:bitmaps)SDL_FreeSurface(e.second);
  text_surfaces.clear();bitmap_surfaces.clear();fonts.clear();bitmaps.clear();text_keys.clear();text_origins.clear();bitmap_tokens.clear();native_tabs.clear();
  text_generations.clear();bitmap_generations.clear();gradient_generations.clear();gradient_tables.clear();gradient_valid.clear();cached_stops.clear();
 }
 bool prepare(const Scene&s,const char*font){
  clear();default_font=font?font:"";size_t n=s.nodes.size();
  text_surfaces.assign(n,nullptr);bitmap_surfaces.assign(n,nullptr);text_origins.resize(n);text_keys.resize(n);bitmap_tokens.assign(n,~0u);
  text_generations.assign(n,0);bitmap_generations.assign(n,0);gradient_generations.assign(n,0);gradient_tables.resize(n);gradient_valid.assign(n,0);cached_stops.resize(n);native_tabs.assign(n,0);
  for(size_t i=0;i<n;i++){const auto&node=s.nodes[i];const char*id=s.str(node.id);native_tabs[i]=(node.parent>=0&&native_tabs[node.parent])||(!std::strncmp(id,"Tab",3)&&id[3]>='1'&&id[3]<='6'&&!id[4]);}
  return true;
 }
 void bind_bitmap(const std::string& name,SDL_Surface* surface){
  auto it=bitmaps.find(name);if(it!=bitmaps.end())SDL_FreeSurface(it->second);
  bitmaps[name]=surface;std::fill(bitmap_surfaces.begin(),bitmap_surfaces.end(),nullptr);std::fill(bitmap_tokens.begin(),bitmap_tokens.end(),~0u);
 }
 void bind_font(const std::string&alias,const std::string&path){font_aliases[alias]=path;std::fill(text_keys.begin(),text_keys.end(),std::string{});}
 void bind_bitmaps(std::function<SDL_Surface*(const std::string&)>loader){
  for(auto&e:bitmaps)SDL_FreeSurface(e.second);bitmaps.clear();
  std::fill(bitmap_surfaces.begin(),bitmap_surfaces.end(),nullptr);std::fill(bitmap_tokens.begin(),bitmap_tokens.end(),~0u);bitmap_loader=std::move(loader);
 }
 const std::array<uint32_t,4097>& gradient_table(const Scene&s,size_t i){
  const auto&n=s.nodes.at(i);const Stop*now=s.stops.data()+n.stop_first;auto&cached=cached_stops[i];
  if(!gradient_valid[i]||cached.size()!=size_t(n.stop_count)||std::memcmp(now,cached.data(),size_t(n.stop_count)*sizeof(Stop))){
   for(size_t k=0;k<4097;k++)gradient_tables[i][k]=gradient(s,n,float(k)/4096);
   cached.assign(now,now+n.stop_count);gradient_valid[i]=1;gradient_generations[i]=next_generation();
  }
  return gradient_tables[i];
 }
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
    static SDL_Rect bounds(const Matrix &m,float w,float h) {
        auto lo=Point{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};auto hi=Point{-lo.x,-lo.y};
        for (auto p:{m.point(0,0),m.point(w,0),m.point(w,h),m.point(0,h)}) {lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
        const int left=int(std::floor(lo.x)),top=int(std::floor(lo.y));
        return {left,top,int(std::ceil(hi.x))-left,int(std::ceil(hi.y))-top};
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
    void update_resources(const Scene &scene) {
        for (size_t i=0;i<scene.nodes.size();i++) {
            const auto &n=scene.nodes[i];const auto &w=scene.world[i];const auto *p=scene.state[i].data();
            if (!w.visible || native_tabs[i]) continue;
            if (n.kind==3 || n.kind==4 || n.kind==1 || n.kind==6) {
                uint32_t token=p[Texture].token;
                if (n.kind==4 && !*scene.str(token)) for (int parent=n.parent;parent>=0;parent=scene.nodes[parent].parent)
                    if (*scene.str(scene.state[parent][Texture].token)) {token=scene.state[parent][Texture].token;break;}
                if (bitmap_tokens[i]!=token) {
                    bitmap_generations[i]=next_generation();bitmap_tokens[i]=token;const std::string name=scene.str(token);
                    auto found=bitmaps.find(name);
                    if (!name.empty() && found==bitmaps.end() && bitmap_loader) found=bitmaps.emplace(name,bitmap_loader(name)).first;
                    bitmap_surfaces[i]=found==bitmaps.end()?nullptr:found->second;
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
            text_generations[i]=next_generation();text_keys[i]=key;SDL_FreeSurface(text_surfaces[i]);text_surfaces[i]=nullptr;
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
};
}
