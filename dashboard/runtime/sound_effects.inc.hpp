// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
#include <atomic>
namespace sfx {
struct Clip { std::vector<Sint16> pcm; };
struct Voice { const Clip *clip=nullptr; size_t at=0; };
std::map<dash::Sound,Clip> clips;
std::vector<Clip> blade_switches;
size_t next_blade_switch=0;
int loaded_build=-1;
Clip toast_clip, done_clip, boot_clip;
Voice voices[4];
// One SDL sfx device serializes mix; close joins callbacks before reopen.
int mix_scratch[8192]{}; // Persistent32KiB, not an audio-thread stack allocation.
SDL_AudioDeviceID device=0;
std::atomic<int> music_mix_volume{205};
#ifdef EXBOX_NATIVE_TITLE
void endpoint_note(const char* message){exbox_native_note("%s\n",message);}
exbox::audio::Endpoint endpoint(exbox::audio::sdl_calls(),endpoint_note);
#endif
// mix() runs on SDL's audio thread with the device locked; play() takes the
// same lock (SDL_LockAudioDevice) to start a voice.
void mix(void *, Uint8 *stream, int len) {
    if(len<=0)return;
    std::memset(stream,0,size_t(len));
    if(len%4 || len>16384)return; // At most4096 complete stereo S16 frames.
    auto *out=reinterpret_cast<Sint16 *>(stream);
    const int n=len/2;
    int* acc=mix_scratch;std::fill_n(acc,size_t(n),0);
    for (auto &v:voices) {
        if (!v.clip) continue;
        for (int i=0;i<n && v.at<v.clip->pcm.size();i++) acc[size_t(i)]+=v.clip->pcm[v.at++]*7/10;
        if (v.at>=v.clip->pcm.size()) v.clip=nullptr;
    }
    music_player.mix(acc,size_t(n/2),music_mix_volume.load(std::memory_order_relaxed)); // stereo frames
#ifdef NXE_DIRECT_VIDEO
    direct_video.mix(acc,size_t(n/2));
#endif
    for (int i=0;i<n;i++) out[i]=Sint16(std::clamp(acc[size_t(i)],-32768,32767));
}
bool load_path(const std::string &path, Clip &clip) {
    SDL_AudioSpec spec{}; Uint8 *buf=nullptr; Uint32 len=0;
    if (!SDL_LoadWAV(path.c_str(),&spec,&buf,&len)) return false;
    SDL_AudioCVT cvt;
    bool ok=false;
    if (SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,2,48000)>=0) {
        std::vector<Uint8> work(size_t(len)*size_t(std::max(1,cvt.len_mult)));
        std::memcpy(work.data(),buf,len);
        cvt.buf=work.data(); cvt.len=int(len);
        if (cvt.needed==0 || SDL_ConvertAudio(&cvt)==0) {
            const int bytes=cvt.needed?cvt.len_cvt:int(len);
            clip.pcm.assign(reinterpret_cast<Sint16 *>(work.data()),reinterpret_cast<Sint16 *>(work.data())+bytes/2);
            ok=true;
        }
    }
    SDL_FreeWAV(buf);
    return ok;
}
bool load(const char *name, Clip &clip) {return load_path(std::string(root)+"private_art/sounds/"+name+".wav",clip);}
void use_era_set() {
    const int build=model.era==0?(model.dashboard_build>=5759?6770:0):
                    model.era==1?9199:model.era==2?13604:model.metro_v1()?15574:17559;
    if (loaded_build==build) return;
    const std::string directory=std::string(root)+"private_art/sounds/eras/"+std::to_string(build)+"/";
    struct stat info{};
    const bool has_set=build!=0 && stat(directory.c_str(),&info)==0 && S_ISDIR(info.st_mode);
    auto read=[&](const char *name, Clip &clip) {
        return has_set?load_path(directory+name+".wav",clip):load(std::strrchr(name,'/')+1,clip);
    };
    std::map<dash::Sound,Clip> next_clips;
    std::vector<Clip> next_switches;
    Clip next_toast, next_done, next_boot;
    using S=dash::Sound;
    const std::pair<S,const char *> names[]={
        // The 360 Guide's HUD skin.
        {S::Focus,"guide/btn_Focus"},{S::Blocked,"guide/btn_InactiveFocus"},
        {S::Select,build==9199?"dash/snd_buttonselect":"guide/btn_Select"},
        {S::Back,build==9199?"dash/snd_buttonback":"guide/btn_Back"},
        {S::Refused,"guide/btn_InactiveSelect"},{S::GuideOpen,has_set?"guide/HUD_open":"guide/btn_selectG"},
        {S::GuideClose,has_set?"guide/HUD_close":"guide/btn_backG"},
        {S::GuideFocus,"guide/btn_focusG"},{S::GuideSelect,"guide/btn_selectG"},
        // The 360 dashboard's own sounds (controlp).
        {S::ChannelUp,"dash/snd_channelup"},{S::ChannelDown,"dash/snd_channeldown"},{S::PaneLeft,"dash/snd_panelleft"},
        {S::PaneRight,"dash/snd_panelright"},{S::Unfold,"dash/snd_panelunfold"},{S::Fold,"dash/snd_panelfold"},
        {S::SetupComplete,"dash/celebrate_sound"},{S::Launch,"dash/snd_transitioninto"}};
    // An absent era launch clip is silent, including the Select stand-in.
    if (has_set) next_clips[S::Launch]=Clip{};
    for (const auto &n:names) {
        Clip c;
        bool ok=read(n.second,c);
        if (!ok && has_set && n.first==S::GuideFocus) ok=read("guide/btn_Focus",c);
        if (!ok && has_set && n.first==S::GuideSelect) ok=read("guide/btn_Select",c); // 6770's Guide has no btn_selectG
        if (ok) next_clips[n.first]=std::move(c);
    }
    // The Guide rotates its BladeSwitch variants in numerical order.
    if (has_set) for (int i=1;i<=4;i++) {
        Clip c;
        if (load_path(directory+"guide/BladeSwitch_"+std::to_string(i)+".wav",c)) next_switches.push_back(std::move(c));
    }
    if (next_switches.empty()) {
        Clip c;
        if (read("guide/tab_Switch",c)) next_clips[S::BladeSwitch]=std::move(c);
    }
    read("guide/NotifyPopup",next_toast);
    // An absent event is silent; do not silently borrow another revision.
    read("guide/dl_complete",next_done);
    read("dash/snd_transitionfrom",next_boot);
    if (device) SDL_LockAudioDevice(device);
    for (auto &v:voices) {
        if (v.clip==&toast_clip || v.clip==&done_clip || v.clip==&boot_clip) v.clip=nullptr;
        for (const auto &c:clips) if (v.clip==&c.second) v.clip=nullptr;
        for (const auto &c:blade_switches) if (v.clip==&c) v.clip=nullptr;
    }
    clips.swap(next_clips);blade_switches.swap(next_switches);next_blade_switch=0;
    // Toasts retain these Clip addresses across era changes.
    toast_clip.pcm.swap(next_toast.pcm);done_clip.pcm.swap(next_done.pcm);boot_clip.pcm.swap(next_boot.pcm);
    loaded_build=build;
    if (device) SDL_UnlockAudioDevice(device);
}
void open() {
    if (device) return; // opened even without the 360's sounds: music plays through it
    SDL_AudioSpec want{}, have{};
    want.freq=48000; want.format=AUDIO_S16SYS; want.channels=2; want.samples=1024; want.callback=mix;
#ifdef EXBOX_NATIVE_TITLE
    (void)have;device=endpoint.open(want);
#else
    device=SDL_OpenAudioDevice(nullptr,0,&want,&have,0);
    if (device) SDL_PauseAudioDevice(device,0);
    else log("sounds: no audio device");
#endif
}
void close() {
    if (!device) return;
#ifdef EXBOX_NATIVE_TITLE
    endpoint.close();
#else
    SDL_CloseAudioDevice(device);
#endif
    device=0;
    for (auto &v:voices) v.clip=nullptr;
}
void play(const Clip *clip) {
    if (!device || !clip || clip->pcm.empty() || !model.sounds) return;
    SDL_LockAudioDevice(device);
    Voice *slot=&voices[0];
    for (auto &v:voices) if (!v.clip) {slot=&v; break;}
    slot->clip=clip; slot->at=0;
    SDL_UnlockAudioDevice(device);
}
void play(dash::Sound sound) {
    using S=dash::Sound;
    if (sound==S::BladeSwitch && !blade_switches.empty()) {
        play(&blade_switches[next_blade_switch]);
        next_blade_switch=(next_blade_switch+1)%blade_switches.size();
        return;
    }
    auto it=clips.find(sound);
    if (it==clips.end()) {
        // Installs without the dashboard's own sounds use the Guide's.
        const S stand_in=sound==S::ChannelUp || sound==S::ChannelDown || sound==S::PaneLeft || sound==S::PaneRight?S::Focus:
                         sound==S::Unfold || sound==S::Launch?S::Select:sound==S::Fold?S::Back:S::None;
        it=clips.find(stand_in);
    }
    if (it!=clips.end()) play(&it->second);
}
}
