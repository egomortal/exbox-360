// Renders the 1888 Guide pack (hudbkgnd + HUD scene + huduiskin) at a named
// state for the parity audit: pack font resource-dir state out.bmp [focus-id...]
#include "../xui_renderer.hpp"
#include <SDL2/SDL_image.h>
#include <cstdlib>
#include <string>
int main(int argc,char **argv) {
    if (argc<6) {std::fprintf(stderr,"pack font resources state out.bmp [focus-id ...]\n");return 2;}
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) || TTF_Init()) return 3;
    xui::Scene scene;
    if (!scene.load(argv[1])) {std::fprintf(stderr,"%s\n",scene.error.c_str());return 4;}
    xui::Renderer renderer;
    if (!renderer.prepare(scene,1920,1080,argv[2],0)) return 5;
    const std::string dir=argv[3];
    renderer.bind_bitmaps([dir](const std::string &key)->SDL_Surface * {
        std::string name=key;
        for (const char *prefix:{"sharedres://","file://","common://"}) if (name.rfind(prefix,0)==0) name=name.substr(std::strlen(prefix));
        if (name.empty()) return nullptr;
        SDL_Surface *raw=IMG_Load((dir+"/"+name).c_str());
        if (!raw) {std::fprintf(stderr,"missing bitmap %s\n",key.c_str());return nullptr;}
        auto *s=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);return s;
    });
    for (size_t i=0;i<scene.nodes.size();i++) {
        const std::string id=scene.str(scene.nodes[i].id);
        if (id.rfind("@visual:",0)==0) scene.seek_name(int(i),"Normal");
    }
    for (int a=6;a<argc;a++) {
        std::string arg=argv[a];
        if (const auto eq=arg.find('=');eq!=std::string::npos) {
            const int n=scene.find(arg.substr(0,eq).c_str());
            if (n<0 || !scene.seek_name(n,arg.substr(eq+1))) std::fprintf(stderr,"cannot seek %s\n",argv[a]);
            continue;
        }
        if (arg.rfind("hide:",0)==0) {scene.bind_show(scene.find(arg.substr(5).c_str()),false);continue;}
        if (arg.rfind("text:",0)==0) {
            const auto eq=arg.find('|');const int n=scene.find(arg.substr(5,eq-5).c_str());
            if (n>=0) scene.bind_string(n,xui::Text,arg.substr(eq+1));
            continue;
        }
        const int n=scene.find(argv[a]);
        if (n<0) {std::fprintf(stderr,"no node %s\n",argv[a]);continue;}
        for (size_t i=size_t(n)+1;i<scene.nodes.size();i++) if (scene.nodes[i].parent==n && std::string(scene.str(scene.nodes[i].id)).rfind("@visual:",0)==0) scene.seek_name(int(i),"Focus");
    }
    if (!scene.seek_name(0,argv[4],std::getenv("ADV")!=nullptr)) std::fprintf(stderr,"no state %s on root\n",argv[4]);
    scene.advance(std::getenv("ADV")?float(std::atof(std::getenv("ADV"))):0.f);scene.evaluate();
    const float k=std::getenv("GK")?float(std::atof(std::getenv("GK"))):1080.f/770;
    auto view=xui::Matrix::scale(k,k);
    view.a[0][3]=std::getenv("GX")?float(std::atof(std::getenv("GX"))):-86.5f;
    view.a[1][3]=std::getenv("GY")?float(std::atof(std::getenv("GY"))):0.f;
    scene.evaluate();
    if (const char *dump=std::getenv("DUMP")) for (size_t i=0;i<scene.nodes.size();i++) {
        const std::string id=scene.str(scene.nodes[i].id);
        if (id.find(dump)==std::string::npos) continue;
        const auto &w=scene.world[i];const auto pt=w.matrix.point(0,0);
        std::fprintf(stderr,"%zu %s parent=%d vis=%d op=%.2f at=%.1f,%.1f size=%.1fx%.1f show=%.0f\n",i,id.c_str(),scene.nodes[i].parent,w.visible,w.opacity,pt.x,pt.y,w.width,w.height,scene.state[i][xui::Show].v[0]);
    }
    SDL_FillRect(renderer.canvas,nullptr,0xff202020);
    renderer.draw(scene,view);
    const int result=SDL_SaveBMP(renderer.canvas,argv[5]);
    renderer.clear();TTF_Quit();SDL_Quit();return result!=0;
}
