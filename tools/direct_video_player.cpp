// Thin desktop presentation harness; decoding/audio/control live in the shared transport.
#include "../modules/direct_video/transport.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;SDL_Texture* texture=nullptr;int status=0;
    try {
        if(argc!=2)throw std::runtime_error("usage: direct-video-player LOCAL_FILE (Space pause, Left/Right seek, Escape close)");
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        exbox::video::Transport player;
        if(!player.open(argv[1]))throw std::runtime_error(player.error());
        window=SDL_CreateWindow("Ex-Box local video test",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,540,SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer)throw std::runtime_error(SDL_GetError());
        bool running=true;uint64_t generation=0;size_t presented=0;int width=0,height=0;
        while(running) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_QUIT)running=false;
                if(event.type!=SDL_KEYDOWN||event.key.repeat)continue;
                auto key=event.key.keysym.sym;
                if(key==SDLK_ESCAPE)running=false;
                if(key==SDLK_SPACE)player.pause(!player.paused());
                if(key==SDLK_LEFT||key==SDLK_RIGHT)player.seek(std::clamp(player.elapsed()+(key==SDLK_RIGHT?5:-5),0.,std::max(0.,player.duration()-.05)));
            }
            if(!player.tick())throw std::runtime_error(player.error());
            if(generation!=player.generation()) {
                generation=player.generation();const auto& frame=player.frame();
                if(!texture||width!=frame.width||height!=frame.height) {
                    if(texture)SDL_DestroyTexture(texture);
                    width=frame.width;height=frame.height;
                    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,width,height);
                    if(!texture)throw std::runtime_error(SDL_GetError());
                }
                if(SDL_UpdateTexture(texture,nullptr,frame.rgba.data(),width*4))throw std::runtime_error(SDL_GetError());
                int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);double scale=std::min(double(w)/width,double(h)/height);
                SDL_Rect box{0,0,int(width*scale),int(height*scale)};box.x=(w-box.w)/2;box.y=(h-box.h)/2;
                SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,&box);SDL_RenderPresent(renderer);++presented;
            }
            if(player.ended())break;
            SDL_Delay(1);
        }
        std::cout<<"presented="<<presented<<" direct_local_file=true shared_transport=true\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;}
    if(texture)SDL_DestroyTexture(texture);
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    SDL_Quit();return status;
}
