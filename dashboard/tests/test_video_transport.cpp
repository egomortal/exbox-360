#include "../modules/direct_video/transport.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){
    int status=0;
    try {
        check(argc==2,"fixture required");check(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER),SDL_GetError());
        exbox::video::Transport player;check(player.open(argv[1]),player.error().c_str());
        check(player.tick(),player.error().c_str());
        player.pause(true);double frozen=player.elapsed();auto generation=player.generation();
        SDL_Delay(30);check(player.tick(),"paused tick");check(player.elapsed()==frozen&&generation==player.generation(),"pause changed frame/clock");
        check(player.seek(.5),"paused seek failed");check(player.paused(),"seek resumed playback");
        player.pause(false);check(player.tick(),"seek tick failed");
        SDL_Delay(40);check(player.tick(),"seek decode failed");check(player.frame().seconds>=.5,"seek showed preroll");
        player.mute(true);check(player.tick(),"mute failed");player.mute(false);check(player.tick(),"unmute failed");
        check(!player.seek(-1),"negative seek accepted");
        player.close();check(!player.tick(),"closed tick succeeded");
        check(player.open(argv[1]),"reopen failed");check(player.tick(),"reopen tick failed");player.close();
        SDL_AudioSpec spec{};spec.freq=48000;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
        auto occupied=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);check(occupied!=0,"fixture device failed");
        check(player.open(argv[1],true),"external mixer tried opening another device");
        check(player.tick(),"external mixer tick failed");
        int mixed[2048]{};player.mix(mixed,1024);bool audible=false;for(int sample:mixed)audible|=sample!=0;
        check(audible,"no PCM delivered to dashboard mixer");
        player.pause(true);std::fill(std::begin(mixed),std::end(mixed),0);player.mix(mixed,1024);
        for(int sample:mixed)check(sample==0,"paused mixer emitted PCM");
        player.close();SDL_CloseAudioDevice(occupied);
        std::cout<<"PASS pause/seek/mute/reopen/shared transport\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';status=1;}
    SDL_Quit();return status;
}
