#pragma once
// Reconstructed chrome measured from reference frames; icons load separately.
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace era_chrome {
struct Palette { unsigned dark, mid, light, high, ink; };
inline constexpr Palette palettes[] = {
    {0x873c24,0xbe6442,0xe3a67b,0xf2c8a2,0x472817},
    {0x936218,0xd69c30,0xe9c65c,0xf5dea1,0x554116},
    {0x2b6b2a,0x61b640,0x9bce70,0xb8df8b,0x23431c},
    {0x163f8b,0x2b72d0,0x77a5e0,0xa6c9ed,0x193f75},
    {0x604775,0x9173b0,0xb8a6ce,0xd7c9e5,0x40334e}
};
inline SDL_Surface *blades[5]{};
inline SDL_Surface *blades_early[5]{};
inline std::string chrome_directory;
inline int cached_revision[2]{};
inline bool source_converted[2][5]{};
inline SDL_Surface *guide = nullptr;
inline SDL_Surface *row_material[2]{}, *profile_material=nullptr, *row_shine=nullptr;
inline SDL_Surface *tray_material[2][5][2]{}, *tray_shine[2][5]{};
struct Glint { float x=0,alpha=0; };
inline Glint row_glint(Uint32 elapsed) {
    const float frame=28+float(elapsed%3767)*60/1000;
    if (frame<45 || frame>=93) return {};
    if (frame<69) {const float t=(frame-45)/24;return {-130+142.166668f*t,t};}
    const float t=(frame-69)/24;return {12.166668f+68.16666f*t,1-t};
}
inline Glint tray_glint(Uint32 elapsed) {
    const float frame=20+float(elapsed%4667)*60/1000;
    if (frame<62) {const float t=(frame-20)/42;return {23+163.5f*t,t};}
    if (frame<103) {const float t=(frame-62)/41;return {186.5f+177.5f*t,1-t};}
    return {};
}
// Content and legend anchors measured on the original four-blade captures.
// A fifth blade occupies another 50px on the left; the right anchors stay put.
// Keep these shared by content and prompts: changing labels must not move slots.
struct BladesLayout { int content, right, edge, prompt_left, prompt_right; };
struct RailLabel { int center,top,left,right,notch; };
inline RailLabel rail_label(int active,int blade,bool early) {
    // Face bounds and notch heights measured in the preloaded chrome.
    constexpr int bounds[5][5][2]={
        {{118,195},{1473,1544},{1544,1627},{1627,1700},{1700,1778}},
        {{99,185},{185,251},{1537,1615},{1615,1687},{1687,1761}},
        {{113,182},{182,259},{253,326},{1609,1688},{1688,1757}},
        {{99,173},{173,244},{244,317},{331,401},{1687,1770}},
        {{99,174},{174,247},{247,318},{318,388},{406,482}}
    };
    constexpr int notches[5][5]={
        {167,221,238,252,266},
        {221,171,225,240,255},
        {218,187,171,224,241},
        {239,219,204,166,218},
        {254,236,223,204,167}
    };
    const int shift=early && blade<=active?50:0;
    const int left=bounds[active][blade][0]-shift,right=bounds[active][blade][1]-shift;
    const int notch=notches[active][blade];
    return {(left+right+1)/2+(blade==active?0:3),
        notch+(blade==active?33:28),left,right,notch};
}
inline BladesLayout layout(int active, bool early) {
    const int shift=early?0:50;
    return {253+(active-1)*80+shift,875+(active-1)*80,
            1498+(active-1)*74,268+(active-1)*80+shift,1427+(active-1)*80};
}
inline unsigned mix(unsigned a, unsigned b, float t) {
    t=std::clamp(t,0.0f,1.0f);
    unsigned out=0;
    for (int shift:{0,8,16}) out|=unsigned(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
    return out;
}
inline float curve(float y) {
    constexpr float points[][2]={{0,0},{112,11},{154,-18},{330,-11},{650,-5},{748,0},{842,34},{920,44},{1080,14}};
    for (int i=1;i<9;i++) if (y<=points[i][0]) {
        float t=(y-points[i-1][0])/(points[i][0]-points[i-1][0]);
        t=t*t*(3-2*t);
        return points[i-1][1]*(1-t)+points[i][1]*t;
    }
    return 14;
}
inline void pixel(SDL_Surface *s,int x,int y,unsigned color,float alpha=1) {
    if (x<0 || x>=s->w || y<0 || y>=s->h) return;
    auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(s->pixels)+y*s->pitch);
    alpha=std::clamp(alpha,0.0f,1.0f);
    if ((row[x]>>24)==255) row[x]=0xff000000u|mix(row[x],color,alpha);
    else row[x]=(unsigned(255*alpha)<<24)|(color&0xffffffu);
}
inline unsigned metal(float d, float y) {
    float shine=0.5f+0.29f*std::cos((d-23)*0.077f)+0.04f*std::sin(y*0.006f);
    unsigned c=mix(0x6f7475,0xeeeeea,shine);
    if (d<5) c=mix(0x323b3a,0xf9fbf4,std::clamp(d/4,0.0f,1.0f));
    if (d>56) c=mix(c,0x444e48,std::min(0.9f,(d-56)/9));
    return c;
}
inline SDL_Surface *make_blades(int active,int first=0) {
    auto *s=SDL_CreateRGBSurfaceWithFormat(0,1920,1080,32,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    const auto &p=palettes[active];
    for (int y=0;y<1080;y++) {
        const float bend=curve(float(y));
        const float left=112+(active-first)*64+first*32+bend, right=1480+active*64-first*48-bend;
        for (int x=0;x<1920;x++) {
            const float outside=std::min(float(x),float(1919-x));
            unsigned c=mix(0x666d6a,0xd6d9d3,std::clamp(outside/130,0.0f,1.0f));
            for (int i=first;i<active;i++) {
                const float d=x-(112+(i-first)*64+first*32+bend);
                if (d>=-1 && d<66) c=mix(c,metal(d,float(y)),std::min(d+1,1.0f));
            }
            for (int i=4;i>active;i--) {
                const float edge=right+(i-active-1)*64;
                const float d=x-(edge-bend*0.12f);
                if (d>=-1 && d<68) c=mix(c,metal(64-d,float(y)),std::min(d+1,1.0f));
            }
            if (x>=left-1 && x<=right+1) {
                float across=(x-left)/std::max(1.0f,right-left);
                unsigned face=mix(p.mid,p.light,0.35f+0.25f*std::sin(across*3.14159f));
                face=mix(face,p.dark,0.12f+0.13f*std::sin(float(y)/1080*3.14159f));
                if (active==2) {
                    const float glow=std::exp(-std::pow((across-0.83f)/0.30f,2)-std::pow((y-450)/380.0f,2));
                    face=mix(0x398c30,0x60df42,glow);
                }
                if (y<132) face=mix(p.dark,p.mid,float(y)/132*0.65f);
                if (y>895) face=mix(p.dark,p.mid,float(y-895)/185);
                if (y>=132 && y<895) {
                    const float glass=std::exp(-std::pow((y-180)/95.0f,2))*0.18f;
                    face=mix(face,p.high,glass);
                    if (y<137) face=mix(face,p.dark,0.15f);
                }
                float edge=std::min(x-left,right-x);
                if (edge<10) face=mix(p.dark,face,std::clamp(edge/10,0.0f,1.0f));
                if (x<left+64) {
                    float d=x-left;
                    face=mix(p.dark,p.light,0.3f+0.3f*std::sin(d/64*3.14159f));
                    if (d>57) face=mix(face,p.dark,(d-57)/8);
                }
                c=mix(c,face,std::clamp(edge+1,0.0f,1.0f));
            }
            pixel(s,x,y,c);
        }
    }
    return s;
}
inline SDL_Surface *load_legacy_chrome(int active,bool early) {
    const std::string path=chrome_directory+"/"+(early?"4":"5")+"-"+std::to_string(active)+".png";
    SDL_Surface *image=IMG_Load(path.c_str());
    if (image && (image->w!=1920 || image->h!=1080)) {
        SDL_FreeSurface(image);
        image=nullptr;
    }
    return image?image:make_blades(active,early);
}

inline void select_revision(int build) {
    const bool early=build<5759;
    const int family=early?1:0;
    if (cached_revision[family]==build) return;
    SDL_Surface *(&slots)[5]=early?blades_early:blades;
    // The original conversion replaces this family's old surfaces. Backgrounds
    // are raw-blitted, so no scaled-art cache holds references to these slots.
    // Keep the existing ceiling of four early plus five later retained frames.
    for (auto *&image:slots) {
        SDL_FreeSurface(image);
        image=nullptr;
    }
    std::string source_root=chrome_directory+"/source-chrome";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_SOURCE_CHROME_ROOT")) source_root=override;
#endif
    for (int active=early?1:0;active<5;active++) {
        const std::string path=source_root+"/"+std::to_string(build)+"-"+std::to_string(active)+".png";
        auto *image=IMG_Load(path.c_str());
        if (image && (image->w!=1920 || image->h!=1080)) {
            SDL_FreeSurface(image);
            image=nullptr;
        }
        source_converted[family][active]=image!=nullptr;
        if (image) SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_NONE);
        slots[active]=image?image:load_legacy_chrome(active,early);
    }
    cached_revision[family]=build;
}

inline bool has_source_labels(int build,int active) {
    return source_converted[build<5759?1:0][active];
}

inline int converted_slot_count(int build) {
    int count=0;
    for (bool converted:source_converted[build<5759?1:0]) count+=converted;
    return count;
}

inline size_t retained_chrome_bytes() {
    size_t bytes=0;
    for (auto *image:blades) if (image) bytes+=size_t(image->pitch)*image->h;
    for (auto *image:blades_early) if (image) bytes+=size_t(image->pitch)*image->h;
    return bytes;
}

inline int retained_chrome_count() {
    int count=0;
    for (auto *image:blades) count+=image!=nullptr;
    for (auto *image:blades_early) count+=image!=nullptr;
    return count;
}

inline void prepare(const char *directory) {
    chrome_directory=directory;
    auto material=[&](const char *name) {
        char path[1024];snprintf(path,sizeof path,"%s/materials/%s.png",directory,name);
        SDL_Surface *image=IMG_Load(path);
        if (image) SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
        return image;
    };
    row_material[0]=material("row-normal");row_material[1]=material("row-focus");
    profile_material=material("profile");row_shine=material("row-shine");
    for (int early=0;early<2;early++) for (int active=0;active<5;active++) {
        if (early && !active) continue;
        char name[64];
        for (int focused=0;focused<2;focused++) {
            snprintf(name,sizeof name,"tray-%d-%d-%s",early?4:5,active,focused?"focus":"normal");
            tray_material[early][active][focused]=material(name);
        }
        snprintf(name,sizeof name,"tray-%d-%d-shine",early?4:5,active);
        tray_shine[early][active]=material(name);
    }
    for (int i=0;i<5;i++) blades[i]=load_legacy_chrome(i,false);
    for (int i=1;i<5;i++) blades_early[i]=load_legacy_chrome(i,true);
    guide=SDL_CreateRGBSurfaceWithFormat(0,860,1080,32,SDL_PIXELFORMAT_ARGB8888);
    if (!guide) return;
    SDL_FillRect(guide,nullptr,0);
    // The Blades Guide, measured off the designer capture at 1080p. Rows are
    // uniform across the panel, so each band is a row profile (4 px steps):
    // the olive top band to y 144, the brushed face, the dark foot from 855.
    // Right of the face: a dark groove, the gamertag spine (y 132-868) laid
    // over a thin twin-line rail that runs the full height.
    static const unsigned char top_rows[37][3]={
        {61,66,53},{62,67,54},{65,71,58},{68,75,61},{71,77,64},{76,82,70},{78,85,73},{81,88,76},{85,92,80},{91,95,84},
        {96,99,89},{98,101,92},{100,103,95},{106,109,101},{109,112,104},{113,116,109},{115,118,111},{119,122,115},{123,126,119},{126,128,122},
        {126,129,123},{125,128,122},{122,124,119},{120,121,116},{117,118,113},{115,117,112},{113,115,110},{112,112,109},{109,109,106},{106,107,103},
        {105,105,103},{103,103,101},{99,99,99},{89,89,89},{77,77,77},{59,59,59},{169,169,169}};
    static const unsigned char foot_rows[58][3]={
        {173,173,170},{182,182,180},{70,70,68},{64,64,61},{63,63,60},{66,67,63},{69,70,66},{70,72,67},{74,76,71},{75,77,72},
        {77,79,74},{80,82,77},{81,83,78},{84,86,81},{87,89,84},{89,91,86},{92,94,89},{93,95,90},{95,98,91},{98,100,94},
        {100,103,96},{101,104,97},{104,107,100},{107,109,102},{108,111,104},{111,113,106},{113,116,109},{115,118,111},{117,119,113},{118,121,115},
        {121,123,118},{123,126,120},{124,127,120},{127,129,123},{128,131,125},{127,130,123},{126,128,122},{124,127,120},{123,126,119},{123,126,119},
        {123,126,119},{120,123,116},{120,123,116},{120,122,116},{118,121,114},{116,119,112},{115,118,111},{114,117,110},{113,116,109},{111,114,107},
        {110,113,104},{110,113,105},{110,113,104},{108,111,102},{107,110,101},{106,109,100},{105,108,99},{103,106,97}};
    auto table=[](const unsigned char (*rows)[3],int n,float at) {
        at=std::clamp(at,0.0f,float(n-1));
        const int i=std::min(n-2,int(at));const float t=at-i;
        unsigned out=0;
        for (int c=0;c<3;c++) out=(out<<8)|unsigned(rows[i][c]*(1-t)+rows[i+1][c]*t+0.5f);
        return out;
    };
    auto smooth=[](const float (*p)[2],int n,float y) {
        if (y<=p[0][0]) return p[0][1];
        for (int i=1;i<n;i++) if (y<=p[i][0]) {
            float t=(y-p[i-1][0])/(p[i][0]-p[i-1][0]);t=t*t*(3-2*t);
            return p[i-1][1]*(1-t)+p[i][1]*t;
        }
        return p[n-1][1];
    };
    constexpr float face_rows[][2]={{142,170},{180,167},{220,162},{260,157},{740,157},{780,162},{820,166},{846,172}};
    // Face edge, then a dark groove, then one continuous metal band (twin lit
    // lines down its left side) that widens into the gamertag spine between
    // y 128 and ~860 and narrows again to the foot.
    constexpr float edge_points[][2]={{0,782},{30,780},{120,764},{145,762},{300,750},{450,745},{700,742},{850,752},{950,768},{1050,776},{1080,778}};
    constexpr float band_left[][2]={{0,795},{120,785},{128,771},{300,759},{450,754},{700,751},{820,760},{870,774},{950,779},{1050,788},{1080,791}};
    constexpr float band_right[][2]={{0,832},{120,818},{128,846},{300,838},{450,831},{700,825},{820,815},{870,811},{950,815},{1050,828},{1080,831}};
    auto lerp=[](const float (*p)[2],int n,float y) {
        if (y<=p[0][0]) return p[0][1];
        for (int i=1;i<n;i++) if (y<=p[i][0]) return p[i-1][1]+(p[i][1]-p[i-1][1])*(y-p[i-1][0])/(p[i][0]-p[i-1][0]);
        return p[n-1][1];
    };
    auto hash=[](unsigned v) {v^=v>>16;v*=0x7feb352dU;v^=v>>15;v*=0x846ca68bU;v^=v>>16;return v;};
    for (int y=0;y<1080;y++) {
        const float edge=smooth(edge_points,11,float(y));
        float L=y<128?smooth(band_left,2,float(y)):lerp(band_left,11,float(y));
        float R=y<128?smooth(band_right,2,float(y)):lerp(band_right,11,float(y));
        if (y>=120 && y<128) {L=787;R=818;}
        const float streak=float(int(hash(unsigned(y))%7)-3);
        unsigned base;
        if (y<144) base=table(top_rows,37,y/4.0f);
        else if (y<848) {
            const unsigned g=unsigned(std::clamp(smooth(face_rows,8,float(y))+streak*0.7f,0.0f,255.0f));
            base=(g<<16)|(g<<8)|g;
        } else base=table(foot_rows,58,(y-848)/4.0f);
        for (int x=0;x<860;x++) {
            unsigned face=base;float alpha=1;
            const float d=float(x)-edge;
            if (d>-26 && d<0) face=mix(face,0x3a3c38,std::pow((d+26)/26,3.0f)*0.55f);   // rolled edge
            if (d>=0) face=0x1d1f1b;
            if (x>=L && x<R+1) {
                const float bx=float(x)-L,toR=R-float(x);
                float v=156+streak*0.6f;
                const float spine_mix=std::clamp((R-L-38)/10,0.0f,1.0f);
                float rv;
                {
                    const float u=bx/std::max(1.0f,R-L);
                    constexpr float rail[]={50,105,155,110,140,128,108,141,108,150,60};
                    const float k=std::clamp(u*10,0.0f,9.999f);const int i=int(k);
                    rv=rail[i]+(rail[i+1]-rail[i])*(k-i)+streak*0.4f;
                }
                if (spine_mix>0) {
                    // spine: plain brushed grey, a dark rolled left side, lit right rim
                    if (bx<8) v=std::min(v,60+bx*12);
                    if (toR<2.5f) v=std::min(v,104.0f); else if (toR<5) v=std::max(v,188.0f);
                }
                v=rv*(1-spine_mix)+v*spine_mix;   // the rail's lines fade into the spine
                // the spine's rounded upper corner
                if (y>=128 && y<142 && x>818) {
                    const float cx=846-14,cy=128+14,dd=std::sqrt((x-cx)*(x-cx)+(y-cy)*(y-cy));
                    if (x>cx && y<cy && dd>14) {alpha=std::clamp(15-dd,0.0f,1.0f);if (alpha<=0) {continue;}}
                    if (y<130) v=std::max(v,210.0f);
                }
                if (y>=128 && y<130 && x>818) v=210;
                const unsigned g=unsigned(std::clamp(v,0.0f,255.0f));
                face=(g<<16)|(g<<8)|g;
            } else if (x>=R+1) continue;
            pixel(guide,x,y,face,alpha);
        }
    }
    SDL_SetSurfaceBlendMode(guide,SDL_BLENDMODE_BLEND);
}
inline void free_all() {
    for (int family=0;family<2;family++) {
        cached_revision[family]=0;
        for (bool &converted:source_converted[family]) converted=false;
    }
    for (auto *&s:row_material) {SDL_FreeSurface(s);s=nullptr;}
    SDL_FreeSurface(profile_material);profile_material=nullptr;
    SDL_FreeSurface(row_shine);row_shine=nullptr;
    for (auto &family:tray_material) for (auto &blade:family) for (auto *&s:blade) {SDL_FreeSurface(s);s=nullptr;}
    for (auto &family:tray_shine) for (auto *&s:family) {SDL_FreeSurface(s);s=nullptr;}
    for (auto *&s:blades) {SDL_FreeSurface(s);s=nullptr;}
    for (auto *&s:blades_early) {SDL_FreeSurface(s);s=nullptr;}
    SDL_FreeSurface(guide);guide=nullptr;
}
}
