#include "../xui_renderer.hpp"
#include "../xui_viewport.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>
int main(int argc,char **argv) {
    if (argc<3)return 2;
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)||TTF_Init())return 3;
    xui::Scene scene;if(!scene.load(argv[1]))return 4;
    xui::Renderer painter;if(!painter.prepare(scene,1920,1080,argv[2]))return 5;
    const auto pack=std::filesystem::path(argv[1]);const auto base=pack.parent_path()/"resources";
    std::ifstream manifest(base/(pack.stem().string()+".tsv"));std::map<std::string,std::string> resources;std::string line;
    while(std::getline(manifest,line)) {const auto tab=line.find('\t');if(tab!=std::string::npos) resources[line.substr(0,tab)]=(base/line.substr(tab+1)).string();}
    painter.bind_bitmaps([&](const std::string &key)->SDL_Surface * {
        const auto found=resources.find(key);if(found==resources.end())return nullptr;
        auto *raw=IMG_Load(found->second.c_str());if(!raw)return nullptr;
        auto *result=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);return result;
    });
    std::vector<double> times;times.reserve(120);
    const auto view=xui::viewport(1920,1080);
    for(int frame=0;frame<130;frame++) {
        scene.seek(0,float(1+frame%30));scene.advance(1);
        auto start=std::chrono::steady_clock::now();painter.draw(scene,view);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(frame>=10) times.push_back(ms);
    }
    std::sort(times.begin(),times.end());
    printf("{\"frames\":%zu,\"minimum_ms\":%.3f,\"median_ms\":%.3f,\"p95_ms\":%.3f,\"maximum_ms\":%.3f}\n",times.size(),times.front(),times[times.size()/2],times[size_t(times.size()*.95)],times.back());
    painter.clear();TTF_Quit();SDL_Quit();
}
