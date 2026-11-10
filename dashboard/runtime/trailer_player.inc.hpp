// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
struct PreparedTrailerWav {SDL_AudioSpec spec{};Uint8* data=nullptr;Uint32 size=0;~PreparedTrailerWav(){if(data)SDL_FreeWAV(data);}};
std::unique_ptr<packed_prep::Ready> trailer_prepared;
bool trailer_preparing=false,trailer_guide_suspended=false,trailer_preparation_failed=false;
bool trailer_audio_queue_invalidated=false;
uint64_t trailer_request_generation=0;
packed_prep::Worker trailer_preparation([](const std::string& path,const packed_prep::Cancel& cancel){
    if(cancel())return packed_prep::Audio{};
    // This is a std::thread, so release SDL thread-local error storage explicitly.
    struct SdlThreadCleanup {~SdlThreadCleanup(){SDL_TLSCleanup();}} cleanup;
    auto wav=std::make_shared<PreparedTrailerWav>();
    if(!SDL_LoadWAV(path.c_str(),&wav->spec,&wav->data,&wav->size))return packed_prep::Audio{{},SDL_GetError()};
    return packed_prep::Audio{wav,{}};
});
bool excluded_trailer(const char* key,const char* folder,const char* name) {
    auto contains=[](const char* value,const char* needle) {
        std::string lower=value?value:"";
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
        return lower.find(needle)!=std::string::npos;
    };
    // Owner removal: keep any source files untouched, but never admit this
    // title into Ex-Box's catalog, metadata, playback or promotion paths.
    return contains(key,"bayonetta")||contains(folder,"bayonetta")||contains(name,"bayonetta");
}
void load_trailers() {
    trailers.clear();
    std::string path=std::string(trailer_root)+"/manifest.tsv";
    FILE *file=fopen(path.c_str(),"r");
    if (file) {
    char line[512];
    while (fgets(line,sizeof line,file)) {
        // key \t folder \t frames \t fps [\t display name]
        char key[192],folder[192],name[192]=""; int frames=0,fps=0,an=16,ad=9;
        if (sscanf(line,"%191[^\t]\t%191[^\t]\t%d\t%d\t%191[^\t\r\n]\t%d\t%d",key,folder,&frames,&fps,name,&an,&ad)<4) continue;
        if (frames<1 || fps<1 || frames>100000 || fps>120 || strchr(folder,'/') || strstr(folder,"..")) continue;
        if (excluded_trailer(key,folder,name)) continue;
        if (an<1 || ad<1 || an>100000 || ad>100000) {an=16;ad=9;}
        trailers.push_back({key,folder,frames,fps,name,an,ad});
    }
    fclose(file);
    }
#ifdef NXE_DIRECT_VIDEO
    direct_trailer_index=-1;direct_video_path.clear();
    // Explicit desktop fixture; existing library indices and saved choices stay intact.
    if (const char* path=getenv("NXE_DIRECT_VIDEO_FILE")) {
        exbox::video::Session probe;
        if (probe.open(path,false)) {
            const double seconds=probe.duration();
            if(seconds>0 && seconds<86400) {
                direct_trailer_index=int(trailers.size());direct_video_path=path;
                trailers.push_back({"local-direct-test","local-direct-test",int(std::ceil(seconds*1000)),1000,"Local video test",16,9});
            }
            probe.close();
        } else fprintf(stderr,"direct video: %s\n",probe.error().c_str());
    }
#endif
}
// UI checks playback readiness, not the storage format used by its backend.
bool trailer_stream_ready() {
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer>=0 && active_trailer==direct_trailer_index)return direct_video.error().empty();
#endif
    return trailer_frames!=nullptr;
}
void stop_trailer() {
    x21hook::committed_video.clear();
    trailer_preparation.cancel();
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index){if(trailer_image){SDL_FreeSurface(trailer_image);trailer_image=nullptr;}direct_video.close();direct_seen_generation=0;}
#endif
    if(trailer_audio){SDL_ClearQueuedAudio(trailer_audio);SDL_CloseAudioDevice(trailer_audio);trailer_audio=0;}
    // All aliases are borrowed from Ready; device closes before owner destruction.
    trailer_audio_data=nullptr;trailer_audio_size=0;trailer_frames=nullptr;trailer_offsets=nullptr;trailer_prepared.reset();
    if(trailer_image){SDL_FreeSurface(trailer_image);trailer_image=nullptr;}
    if(trailer_scaled){SDL_FreeSurface(trailer_scaled);trailer_scaled=nullptr;}
    if(video_source_argb){SDL_FreeSurface(video_source_argb);video_source_argb=nullptr;}
    scaled_video_opaque=false;active_trailer=-1;active_frame=-1;trailer_paused=false;trailer_paused_ms=0;trailer_started=0;
    trailer_preparing=false;trailer_preparation_failed=false;trailer_request_generation=0;trailer_guide_suspended=false;trailer_audio_queue_invalidated=false;
}
void invalidate_trailer_audio_for_music() {
    if(trailer_audio){SDL_ClearQueuedAudio(trailer_audio);SDL_PauseAudioDevice(trailer_audio,1);}
    trailer_audio_queue_invalidated=true;
}
void queue_trailer_audio(Uint32 from_ms=0) {
    if(!trailer_audio||!trailer_audio_data)return;
    if(music_audible()){invalidate_trailer_audio_for_music();return;}
    SDL_ClearQueuedAudio(trailer_audio);
    const Uint32 frame=std::max(1u,unsigned(trailer_audio_spec.channels)*(SDL_AUDIO_BITSIZE(trailer_audio_spec.format)/8u));
    const Uint64 at=Uint64(from_ms)*unsigned(trailer_audio_spec.freq)/1000u*frame;
    if(at<trailer_audio_size)SDL_QueueAudio(trailer_audio,trailer_audio_data+at,Uint32(trailer_audio_size-at));
    trailer_audio_queue_invalidated=false;
    SDL_PauseAudioDevice(trailer_audio,trailer_paused||trailer_guide_suspended?1:0);
}
Uint32 trailer_elapsed() {
    if(active_trailer<0)return 0;
    if(trailer_preparing||trailer_preparation_failed||trailer_paused||trailer_guide_suspended)return trailer_paused_ms;
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer>=0&&active_trailer==direct_trailer_index)return Uint32(std::max(0.,direct_video.elapsed())*1000.);
#endif
    return SDL_GetTicks()-trailer_started;
}
Uint32 trailer_duration(int index) {
    if(index<0||index>=int(trailers.size()))return 0;
    const auto& clip=trailers[size_t(index)];return Uint32((1000ull*unsigned(clip.frames))/unsigned(clip.fps));
}
void seek_trailer(Uint32 ms) {
    if(active_trailer<0)return;ms=std::min(ms,trailer_duration(active_trailer));
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index&&!direct_video.seek(std::min(double(ms)/1000.,direct_video.duration())))return;
#endif
    trailer_started=SDL_GetTicks()-ms;trailer_paused_ms=ms;active_frame=-1;
    if(!trailer_preparing)queue_trailer_audio(ms);
}
void pause_trailer(bool paused) {
    if(active_trailer<0||paused==trailer_paused)return;
    if(paused)trailer_paused_ms=trailer_elapsed();else if(!trailer_preparing&&!trailer_guide_suspended)trailer_started=SDL_GetTicks()-trailer_paused_ms;
    trailer_paused=paused;
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index)direct_video.pause(paused||trailer_guide_suspended);
#endif
    if(trailer_audio)SDL_PauseAudioDevice(trailer_audio,paused||trailer_guide_suspended||music_audible()?1:0);
}
void start_trailer(int index) {
    stop_trailer();active_trailer=index;if(index<0)return;
    if(index>=int(trailers.size())){trailer_preparation_failed=true;return;}
#ifdef NXE_DIRECT_VIDEO
    if(index==direct_trailer_index){trailer_started=SDL_GetTicks();if(!direct_video.open(direct_video_path,true))trailer_preparation_failed=true;return;}
#endif
    const auto& clip=trailers[size_t(index)];const std::string base=std::string(trailer_root)+"/packed/"+clip.folder;
    trailer_request_generation=trailer_preparation.request({clip.key,base+".frames",base+".wav",uint32_t(clip.frames),uint32_t(clip.fps),0});
    trailer_preparing=trailer_request_generation!=0;trailer_preparation_failed=!trailer_preparing;
}
void poll_trailer_preparation() {
    if(!trailer_preparing)return;auto ready=trailer_preparation.poll();
    if(!ready){const auto state=trailer_preparation.status();if(state.generation==trailer_request_generation&&state.state==packed_prep::State::Failed){trailer_preparing=false;trailer_preparation_failed=true;}return;}
    if(ready->generation!=trailer_request_generation||active_trailer<0)return;
    trailer_prepared=std::move(ready);trailer_frames=trailer_prepared->file.get();trailer_offsets=trailer_prepared->offsets.get();
    auto* wav=static_cast<PreparedTrailerWav*>(trailer_prepared->audio.owner.get());
    if(wav){trailer_audio_spec=wav->spec;trailer_audio_data=wav->data;trailer_audio_size=wav->size;if(SDL_WasInit(SDL_INIT_AUDIO))trailer_audio=SDL_OpenAudioDevice(nullptr,0,&trailer_audio_spec,nullptr,0);}
    trailer_preparing=false;trailer_preparation_failed=false;trailer_started=SDL_GetTicks()-trailer_paused_ms;active_frame=-1;queue_trailer_audio(trailer_paused_ms);
}
void suspend_trailer_for_guide() {
    if(active_trailer<0||trailer_guide_suspended)return;
    trailer_paused_ms=trailer_elapsed();trailer_guide_suspended=true;if(trailer_audio)SDL_PauseAudioDevice(trailer_audio,1);
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index)direct_video.pause(true);
#endif
}
bool resume_trailer_from_guide(bool same_video) {
    if(!trailer_guide_suspended)return false;if(!same_video){stop_trailer();return false;}
    trailer_guide_suspended=false;if(!trailer_paused&&!trailer_preparing)trailer_started=SDL_GetTicks()-trailer_paused_ms;
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index)direct_video.pause(trailer_paused);
#endif
    if(trailer_audio_queue_invalidated&&!music_audible()&&!trailer_preparing)queue_trailer_audio(trailer_paused_ms);
    else if(trailer_audio)SDL_PauseAudioDevice(trailer_audio,trailer_paused||music_audible()?1:0);
    return true;
}
// Only refill an explicitly music-invalidated queue, never an ordinary Guide hold.
void sync_trailer_audio_with_music() {
    if(!trailer_audio||!trailer_audio_data)return;
    if(music_audible()){if(!trailer_audio_queue_invalidated)invalidate_trailer_audio_for_music();}
    else if(trailer_audio_queue_invalidated&&!trailer_preparing&&!trailer_guide_suspended)queue_trailer_audio(trailer_elapsed());
}
// The trailer for a title: the owner's choice from Details, else by its name.
int trailer_for(const std::string &key, const std::string &title) {
    auto chosen=model.trailer_choice.find(key);
    const std::string want=chosen!=model.trailer_choice.end()?chosen->second:game_key(title);
    if (want.empty()) return -1;
    for (size_t i=0;i<trailers.size();++i) if (trailers[i].key==want) return int(i);
    return -1;
}
// Rest on a tile and the 360 spun its disc, then played the trailer.
constexpr Uint32 kSpinAt=600, kTrailerAt=1800;
std::string dwell_key;
Uint32 dwell_since=0;
int dwell_trailer=-1;
SDL_Surface *disc_spin=nullptr;
enum class Phase { Idle, Spin, Play };
Phase phase(Uint32 now) {
    if (dwell_trailer<0) return Phase::Idle;
    const Uint32 t=now-dwell_since;
    return t>=kTrailerAt?Phase::Play:t>=kSpinAt?Phase::Spin:Phase::Idle;
}
// The disc, turning, centred on (cx, cy).
void draw_spin(int cx, int cy, int size, Uint32 now, float alpha=1) {
    if (!disc_spin || alpha<=0) return;
    const int side=disc_spin->h, frames=std::max(1,disc_spin->w/side);
    const int f=int(((now-dwell_since)/33u)%unsigned(frames));
    SDL_Rect from{f*side,0,side,side}, to{cx-size/2,cy-size/2,size,size};
    SDL_SetSurfaceAlphaMod(disc_spin,Uint8(255*std::min(1.0f,alpha)));
    blit_quality(disc_spin,&from,screen,&to);
    SDL_SetSurfaceAlphaMod(disc_spin,255);
}
// How far the disc and the trailer have faded in over the title's own icon.
struct MediaFade { float spin=0, video=0; };
MediaFade media_fade(Uint32 now) {
    MediaFade f;
    if(trailer_guide_suspended||trailer_preparing)return f;
    const Phase p=phase(now);
    if (p!=Phase::Idle) f.spin=progress(now,dwell_since+kSpinAt,220);
    if (p==Phase::Play && trailer_image && active_trailer>=0) f.video=progress(now,trailer_started,380);
    return f;
}
MediaFade hub_media_fade(Uint32 now) {
    return model.info || model.viewer!=dash::Viewer::None || model.guide || !model.content_focused()?MediaFade{}:media_fade(now);
}
SDL_Rect contained_rect(int image_w, int image_h, const SDL_Rect &box) {
    if (image_w<1 || image_h<1 || box.w<1 || box.h<1) return {box.x,box.y,0,0};
    const double scale=std::min(double(box.w)/image_w,double(box.h)/image_h);
    const int w=std::max(1,int(image_w*scale)), h=std::max(1,int(image_h*scale));
    return {box.x+(box.w-w)/2,box.y+(box.h-h)/2,w,h};
}
void blit_contained(SDL_Surface *art, const SDL_Rect &box, const SDL_Rect *source=nullptr) {
    if (!art) return;
    const int w=source?source->w:art->w, h=source?source->h:art->h;
    SDL_Rect at=contained_rect(w,h,box);
    if (at.w>0 && at.h>0) blit_quality(art,source,screen,&at);
}
SDL_Rect video_rect(const SDL_Rect &box) {
    if (active_trailer>=0 && active_trailer<int(trailers.size())) {
        const auto &clip=trailers[size_t(active_trailer)];
        return contained_rect(clip.aspect_num,clip.aspect_den,box);
    }
    return contained_rect(trailer_image?trailer_image->w:16,trailer_image?trailer_image->h:9,box);
}
#ifndef __PROSPERO__
SDL_Rect last_video_rect{};
float last_video_alpha=0;
#endif
// The trailer's current frame in a box, faded in.
void draw_video(int x, int y, int w, int h, float alpha) {
    if (trailer_guide_suspended || !trailer_image || alpha<=0) return;
    // The fullscreen border lies wholly outside the visible viewport. Avoid
    // routing that expanded rectangle through SDL's slow clipped 1x1 scaler.
    // Partial clips and non-fullscreen placement retain the original geometry.
    const SDL_Rect clip=screen->clip_rect; // Physical bounds; main wraps SDL_GetClipRect in logical units.
    const int border_density=render_density::density(screen);
    const bool full_border=x==0&&y==0&&int64_t(w)*border_density==screen->w&&int64_t(h)*border_density==screen->h&&
        clip.x==0&&clip.y==0&&clip.w==screen->w&&clip.h==screen->h;
    if(full_border)shade(0,0,w,h,0x101510,Uint8(255*alpha));
    else shade(x-2,y-2,w+4,h+4,0x101510,Uint8(255*alpha));
    SDL_Rect r=video_rect({x,y,w,h});
#ifndef __PROSPERO__
    last_video_rect=r;last_video_alpha=alpha;
#endif
    const int density=render_density::density(screen);
    const int pixel_w=r.w*density,pixel_h=r.h*density;
    const bool gpu_video_recorded=x21hook::video_frame(trailer_image,trailer_generation,screen,render_density::pixel_rect(r,density),alpha);
    if(gpu_video_recorded&&!x21hook::audit_cpu())return;
    x21hook::Pause video_cpu_reference(gpu_video_recorded);
    if (!trailer_scaled || scaled_video_generation!=trailer_generation || trailer_scaled->w!=pixel_w || trailer_scaled->h!=pixel_h) {
        // Reuse both conversion and output storage across decoded video frames.
        if(trailer_scaled&&(trailer_scaled->w!=pixel_w||trailer_scaled->h!=pixel_h)){SDL_FreeSurface(trailer_scaled);trailer_scaled=nullptr;}
        if(!trailer_scaled)trailer_scaled=video_render_surface(pixel_w,pixel_h,density);
        SDL_Surface* source=trailer_image;
        Uint32 source_key=0;
        if(source->w<1||source->h<1||source->w>3840||source->h>2160||SDL_GetColorKey(source,&source_key)==0)source=nullptr;
        if(source&&source->format->format!=SDL_PIXELFORMAT_ARGB8888){
            if(video_source_argb&&(video_source_argb->w!=source->w||video_source_argb->h!=source->h)){SDL_FreeSurface(video_source_argb);video_source_argb=nullptr;}
            if(!video_source_argb)video_source_argb=SDL_CreateRGBSurfaceWithFormat(0,source->w,source->h,32,SDL_PIXELFORMAT_ARGB8888);
            SDL_BlendMode blend;Uint8 mod=255;SDL_GetSurfaceBlendMode(source,&blend);SDL_GetSurfaceAlphaMod(source,&mod);
            SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);SDL_SetSurfaceAlphaMod(source,255);
            const bool copied=video_source_argb&&SDL_UpperBlit(source,nullptr,video_source_argb,nullptr)==0;
            SDL_SetSurfaceBlendMode(source,blend);SDL_SetSurfaceAlphaMod(source,mod);source=copied?video_source_argb:nullptr;
        }
        surface_generations::invalidate(trailer_scaled); // mutable decoded-frame target, never an immutable art generation
        scaled_video_opaque=trailer_scaled&&video_scale_workspace.scale(source,trailer_scaled);
        if(!scaled_video_opaque){SDL_FreeSurface(trailer_scaled);trailer_scaled=render_density::tag(resample_art(trailer_image,{0,0,trailer_image->w,trailer_image->h},pixel_w,pixel_h),density);}
        surface_generations::invalidate(trailer_scaled);
        scaled_video_generation=trailer_generation;
    }
    if (trailer_scaled) {
        SDL_SetSurfaceAlphaMod(trailer_scaled,Uint8(255*alpha));
        SDL_SetSurfaceBlendMode(trailer_scaled,scaled_video_opaque&&alpha>=1?SDL_BLENDMODE_NONE:SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(trailer_scaled,nullptr,screen,&r);
    }
}
void update_trailer(int match) {
    if (match!=active_trailer) {
        start_trailer(match);
        // Let the caller record arrival seek/user-pause before first adoption.
        if(trailer_preparing)return;
    }
    poll_trailer_preparation();
    if (match<0||match>=int(trailers.size())||trailer_preparing||trailer_guide_suspended||trailer_preparation_failed) return;
#ifdef NXE_DIRECT_VIDEO
    if(match==direct_trailer_index) {
        direct_video.mute(music_audible());
        if(!direct_video.tick())return;
        if(direct_seen_generation!=direct_video.generation()) {
            direct_seen_generation=direct_video.generation();const auto& frame=direct_video.frame();
            if(trailer_image)SDL_FreeSurface(trailer_image);
            trailer_image=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(frame.rgba.data()),frame.width,frame.height,32,frame.width*4,SDL_PIXELFORMAT_RGBA32);
            trailers[size_t(match)].aspect_num=frame.width;trailers[size_t(match)].aspect_den=frame.height;
            ++trailer_generation;active_frame=int(direct_seen_generation);
        }
        if(direct_video.ended()) {
            if(trailer_hold_end){pause_trailer(true);}
            else {seek_trailer(0);}
        }
        return;
    }
#endif
    const auto &clip=trailers[size_t(match)];
    const Uint32 duration=trailer_duration(match);
    if (duration && trailer_elapsed()>=duration) {
        if (trailer_hold_end) {
            // The full-screen player stops on the last frame, as the 360's did.
            if (!trailer_paused) {trailer_paused_ms=duration;trailer_paused=true;}
            if (trailer_audio) SDL_PauseAudioDevice(trailer_audio,1);
        } else {
            trailer_started=SDL_GetTicks();
            trailer_paused=false;
            queue_trailer_audio();
            active_frame=-1;
        }
    }
    const Uint64 at=Uint64(trailer_elapsed())*unsigned(clip.fps)/1000u;
    const int frame=trailer_hold_end?int(std::min<Uint64>(at,Uint64(clip.frames-1))):int(at%unsigned(clip.frames));
    if (frame==active_frame) return;
    if (trailer_image) {SDL_FreeSurface(trailer_image);trailer_image=nullptr;}
    if (trailer_frames && trailer_offsets && size_t(frame)<trailer_offsets->size() &&
        fseek(trailer_frames,(*trailer_offsets)[size_t(frame)],SEEK_SET)==0) {
        Uint32 bytes=0;
        if (fread(&bytes,4,1,trailer_frames)==1 && bytes>0 && bytes<10000000u) {
            std::vector<unsigned char> data(bytes);
            if (fread(data.data(),1,bytes,trailer_frames)==bytes) {
                SDL_RWops *rw=SDL_RWFromConstMem(data.data(),int(bytes));
                if (rw) trailer_image=IMG_LoadJPG_RW(rw);
                if (rw) SDL_RWclose(rw);
            }
        }
    }
    active_frame=frame;
    ++trailer_generation;
}

