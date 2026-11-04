// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
namespace avatar {
constexpr int kFps=15;
SpritePack idle, wave, turn;
bool waving=false;
Uint32 wave_at=0, next_emote=0;
void load() {
    const std::string dir=std::string(root)+"private_art/avatar/";
    idle.open(dir+"avatar_idle.frames");
    wave.open(dir+"avatar_wave.frames");
    turn.open(dir+"avatar_turn.frames");
    model.avatar_angles=turn.count();
    if (idle.count()) {
        char line[96];
        std::snprintf(line,sizeof line,"avatar: %d idle, %d wave, %d turn frames",idle.count(),wave.count(),turn.count());
        log(line);
    }
}
bool installed() {return idle.count()>0;}
void start_wave(Uint32 now) {
    if (wave.count()) {waving=true; wave_at=now;}
}
// The wave runs once, then the idle loop carries on.
SDL_Surface *wave_frame(Uint32 now) {
    if (!waving) return nullptr;
    const int f=int((now-wave_at)*Uint32(kFps)/1000u);
    if (f>=wave.count()) {waving=false; return nullptr;}
    return wave.frame(f);
}
// On My Xbox: breathing, with a wave now and then as the NXE avatars did.
SDL_Surface *hub_frame(Uint32 now) {
    if (!installed()) return nullptr;
    if (!waving && Sint32(now-next_emote)>=0 && next_emote) start_wave(now);
    if (!next_emote || waving) next_emote=now+25000+(now*7919u)%20000u;
    if (SDL_Surface *w=wave_frame(now)) return w;
    return idle.frame(int(now*Uint32(kFps)/1000u));
}
void close() {idle.close(); wave.close(); turn.close();}
}
