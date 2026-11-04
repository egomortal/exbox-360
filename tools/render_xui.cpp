#include "../xui_renderer.hpp"
#include "../xui_viewport.hpp"
#include <chrono>
#include <cstdlib>
int main(int argc,char **argv) {
    if (argc<5) {std::fprintf(stderr,"pack font frame output.bmp [loop-frame] [workers]\n");return 2;}
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) || TTF_Init()) return 3;
    xui::Scene scene;
    if (!scene.load(argv[1])) {std::fprintf(stderr,"%s\n",scene.error.c_str());return 4;}
    xui::Renderer renderer;
    if (!renderer.prepare(scene,1920,1080,argv[2],argc>6?unsigned(std::atoi(argv[6])):0)) return 5;
    scene.advance(argc>5?float(std::atof(argv[5])):0.f);scene.seek(0,float(std::atof(argv[3])));
    const auto start=std::chrono::steady_clock::now();
    const auto view=xui::viewport(1920,1080);
    renderer.draw(scene,view);
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    const int result=SDL_SaveBMP(renderer.canvas,argv[4]);
    std::printf("%zu nodes, %zu tracks, %llu visible figures, %lld us\n",scene.nodes.size(),scene.tracks.size(),(unsigned long long)renderer.figures_drawn,(long long)us);
    renderer.clear();TTF_Quit();SDL_Quit();return result!=0;
}
