#include "../xui_renderer.hpp"
#include <cstdlib>
int main(int argc,char **argv) {
    if (argc!=8) {std::fprintf(stderr,"pack font frame content-width content-height padding output.bmp\n");return 2;}
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) || TTF_Init()) return 3;
    xui::Scene scene;
    if (!scene.load(argv[1])) {std::fprintf(stderr,"%s\n",scene.error.c_str());return 4;}
    const int width=std::atoi(argv[4]),height=std::atoi(argv[5]),padding=std::atoi(argv[6]);
    if (width<=0 || height<=0 || padding<0 || scene.nodes.empty()) return 5;
    const float design_width=scene.state[0][xui::Width].v[0],design_height=scene.state[0][xui::Height].v[0];
    if (design_width<=0 || design_height<=0) return 6;
    xui::Renderer renderer;
    if (!renderer.prepare(scene,width+padding*2,height+padding*2,argv[2],1)) return 7;
    scene.advance(0);
    scene.seek(0,float(std::atof(argv[3])));
    const auto view=xui::Matrix::translate(float(padding),float(padding))*xui::Matrix::scale(width/design_width,height/design_height);
    renderer.draw(scene,view,false);
    for (size_t i=0;i<scene.nodes.size();i++) if (scene.nodes[i].kind==1) {
        const auto &n=scene.nodes[i];const auto &w=scene.world[i];const auto *p=scene.state[i].data();
        std::printf("figure %zu %s visible=%d opacity=%.6f size=%.6f,%.6f fill=%08x stops=%d brush=%.6f,%.6f,%.6f,%.6f\n",i,scene.str(n.id),int(w.visible),w.opacity,w.width,w.height,p[xui::FillColor].token,n.stop_count,p[xui::FillScale].v[0],p[xui::FillScale].v[1],p[xui::FillTranslation].v[0],p[xui::FillTranslation].v[1]);
    }
    const int result=SDL_SaveBMP(renderer.canvas,argv[7]);
    std::printf("%zu nodes, %llu figures\n",scene.nodes.size(),(unsigned long long)renderer.figures_drawn);
    renderer.clear();TTF_Quit();SDL_Quit();return result!=0;
}
