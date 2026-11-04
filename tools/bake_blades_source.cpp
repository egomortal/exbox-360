// Offline source-scene conversion, never a PS5 runtime painter or asset redraw.
#include "../xui_renderer.hpp"
#include "../xui_navigation.hpp"
#include "../xui_viewport.hpp"
#include <filesystem>
#include <cstdio>
#include <cstdlib>

int main(int argc,char **argv) {
    if (argc!=5 && argc!=6) {
        std::fprintf(stderr,"pack font build output-directory [opaque-material-directory]\n");
        return 2;
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) || TTF_Init()) return 3;
    const int build=std::atoi(argv[3]);
    const bool early=build<5759;
    std::filesystem::create_directories(argv[4]);
    xui::Scene scene;
    xui::Renderer painter;
    xui::Navigation navigation;
    if (!scene.load(argv[1]) || !painter.prepare(scene,1920,1080,argv[2]) || !navigation.bind(scene,early,early?1:0)) return 4;
    const bool components=argc==6;
    if (components) {
        // Keep original side components and their expanded visual children.
        // The central animated background has unverified shader behavior;
        // retain the explicitly recreated calibrated material underneath.
        for (size_t i=1;i<scene.nodes.size();i++) {
            int root=int(i);
            while (scene.nodes[root].parent>0) root=scene.nodes[root].parent;
            const std::string id=scene.str(scene.nodes[root].id);
            const bool blade=id.rfind("blade",0)==0 &&
                (id.find("grey")!=std::string::npos || id.find("_top")!=std::string::npos || id.find("_txt")!=std::string::npos);
            const bool wing=id.rfind("wing_",0)==0;
            if (!blade && !wing) scene.nodes[i].kind=0;
        }
    }
    const auto view=xui::viewport(1920,1080);
    for (int active=early?1:0;active<5;active++) {
        if (!navigation.reset(active)) return 5;
        const std::string prefix=std::string(argv[4])+"/"+std::to_string(build)+"-"+std::to_string(active);
        SDL_Surface *backdrop=nullptr;
        if (components) {
            const std::string path=std::string(argv[5])+"/body-"+(early?"4":"5")+"-"+std::to_string(active)+".png";
            backdrop=IMG_Load(path.c_str());
            if (!backdrop || backdrop->w!=1920 || backdrop->h!=1080) return 9;
            SDL_SetSurfaceBlendMode(backdrop,SDL_BLENDMODE_NONE);
        }
        // The full original scene already suppresses native content tabs.
        // Both variants retain original contours, visual-wrapper transforms,
        // gradient data and settled animation pose. Source bytes stay intact.
        painter.draw(scene,view,false,backdrop);
        if (IMG_SavePNG(painter.canvas,(prefix+"-chrome.png").c_str())) return 6;
        painter.draw(scene,view,true,backdrop);
        if (IMG_SavePNG(painter.canvas,(prefix+"-with-text.png").c_str())) return 7;
        FILE *file=std::fopen((prefix+".json").c_str(),"w");
        if (!file) return 8;
        std::fprintf(file,"{\"build\":%d,\"active\":%d,\"settled_frame\":%.8f,\"labels\":[",build,active,navigation.frame());
        bool comma=false;
        for (size_t i=0;i<scene.nodes.size();i++) {
            const auto &node=scene.nodes[i];
            const char *id=scene.str(node.id);
            const auto &world=scene.world[i];
            const std::string name=id;
            if (name.rfind("blade_",0)!=0 || name.find("_txt")==std::string::npos || !world.visible) continue;
            const auto matrix=view*world.matrix;
            if (comma) std::fputc(',',file);
            comma=true;
            std::fprintf(file,"{\"id\":\"%s\",\"opacity\":%.8f,\"width\":%.8f,\"height\":%.8f,\"point_size\":%.8f,\"matrix\":[",id,world.opacity,world.width,world.height,scene.state[i][xui::PointSize].v[0]);
            for (int row=0;row<4;row++) {
                for (int column=0;column<4;column++) std::fprintf(file,"%s%.9g",row||column?",":"",matrix.a[row][column]);
            }
            std::fputs("]}",file);
        }
        std::fputs("]}\n",file);
        std::fclose(file);
        SDL_FreeSurface(backdrop);
    }
    painter.clear();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
