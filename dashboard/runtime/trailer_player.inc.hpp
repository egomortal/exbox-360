// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
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
#ifdef NXE_DIRECT_VIDEO
    // trailer_image is only a wrapper over the transport-owned pixels in this path.
    if(active_trailer==direct_trailer_index) {
        if(trailer_image){SDL_FreeSurface(trailer_image);trailer_image=nullptr;}
        direct_video.close();direct_seen_generation=0;
    }
#endif
    if (trailer_audio) {SDL_ClearQueuedAudio(trailer_audio);SDL_CloseAudioDevice(trailer_audio);trailer_audio=0;}
    if (trailer_audio_data) {SDL_FreeWAV(trailer_audio_data);trailer_audio_data=nullptr;trailer_audio_size=0;}
    if (trailer_image) {SDL_FreeSurface(trailer_image);trailer_image=nullptr;}
    if (trailer_scaled) {SDL_FreeSurface(trailer_scaled);trailer_scaled=nullptr;}
    if (trailer_frames) {fclose(trailer_frames);trailer_frames=nullptr;}
    trailer_offsets.clear();
    active_trailer=-1;
    active_frame=-1;
    trailer_paused=false;
}
// The sound from ms on: the WAV is PCM, so a seek is a byte offset.
void queue_trailer_audio(Uint32 from_ms=0) {
    if (trailer_audio && trailer_audio_data && !music_audible()) {
        SDL_ClearQueuedAudio(trailer_audio);
        const Uint32 frame=std::max(1u,unsigned(trailer_audio_spec.channels)*(SDL_AUDIO_BITSIZE(trailer_audio_spec.format)/8u));
        const Uint64 at=Uint64(from_ms)*unsigned(trailer_audio_spec.freq)/1000u*frame;
        if (at<trailer_audio_size) SDL_QueueAudio(trailer_audio,trailer_audio_data+at,Uint32(trailer_audio_size-at));
        SDL_PauseAudioDevice(trailer_audio,trailer_paused?1:0);
    }
}
Uint32 trailer_elapsed() {
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer>=0 && active_trailer==direct_trailer_index)return Uint32(std::max(0.,direct_video.elapsed())*1000.);
#endif
    return trailer_paused?trailer_paused_ms:SDL_GetTicks()-trailer_started;
}
Uint32 trailer_duration(int index) {
    if (index<0 || index>=int(trailers.size())) return 0;
    const auto &clip=trailers[size_t(index)];
    return Uint32((1000ull*unsigned(clip.frames))/unsigned(clip.fps));
}
// Game Videos' scrub: the next update_trailer shows the frame at ms.
void seek_trailer(Uint32 ms) {
    if (active_trailer<0) return;
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index) {
        if(!direct_video.seek(std::min(double(ms)/1000.,direct_video.duration())))return;
    }
#endif
    ms=std::min(ms,trailer_duration(active_trailer));
    trailer_started=SDL_GetTicks()-ms;
    trailer_paused_ms=ms;
    active_frame=-1;
    queue_trailer_audio(ms);
}
void pause_trailer(bool paused) {
    if (active_trailer<0 || paused==trailer_paused) return;
    if (paused) trailer_paused_ms=trailer_elapsed();
    else trailer_started=SDL_GetTicks()-trailer_paused_ms;
    trailer_paused=paused;
#ifdef NXE_DIRECT_VIDEO
    if(active_trailer==direct_trailer_index)direct_video.pause(paused);
#endif
    if (trailer_audio) SDL_PauseAudioDevice(trailer_audio,paused || music_audible()?1:0);
}
void start_trailer(int index) {
    stop_trailer();
    active_trailer=index;
    trailer_started=SDL_GetTicks();
    if (index<0) return;
#ifdef NXE_DIRECT_VIDEO
    if(index==direct_trailer_index) {
        if(!direct_video.open(direct_video_path,true))fprintf(stderr,"direct video: %s\n",direct_video.error().c_str());
        return;
    }
#endif
    const auto &clip=trailers[size_t(index)];
    std::string path=std::string(trailer_root)+"/packed/"+clip.folder+".frames";
    trailer_frames=fopen(path.c_str(),"rb");
    char magic[4]; Uint32 count=0;
    if (trailer_frames && fread(magic,1,4,trailer_frames)==4 &&
        memcmp(magic,"NXEF",4)==0 && fread(&count,4,1,trailer_frames)==1 &&
        count==unsigned(clip.frames)) {
        for (Uint32 i=0;i<count;i++) {
            long offset=ftell(trailer_frames);
            Uint32 bytes=0;
            if (fread(&bytes,4,1,trailer_frames)!=1 || bytes>10000000u ||
                fseek(trailer_frames,long(bytes),SEEK_CUR)!=0) break;
            trailer_offsets.push_back(offset);
        }
    }
    if (trailer_offsets.size()!=size_t(clip.frames)) {
        if (trailer_frames) fclose(trailer_frames);
        trailer_frames=nullptr;
        trailer_offsets.clear();
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return;
    path=std::string(trailer_root)+"/packed/"+clip.folder+".wav";
    if (!SDL_LoadWAV(path.c_str(),&trailer_audio_spec,&trailer_audio_data,&trailer_audio_size)) return;
    trailer_audio=SDL_OpenAudioDevice(nullptr,0,&trailer_audio_spec,nullptr,0);
    if (trailer_audio) queue_trailer_audio();
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
    if (!trailer_image || alpha<=0) return;
    shade(x-2,y-2,w+4,h+4,0x101510,Uint8(255*alpha));
    SDL_Rect r=video_rect({x,y,w,h});
#ifndef __PROSPERO__
    last_video_rect=r;last_video_alpha=alpha;
#endif
    if (!trailer_scaled || scaled_video_generation!=trailer_generation || trailer_scaled->w!=r.w || trailer_scaled->h!=r.h) {
        if (trailer_scaled) SDL_FreeSurface(trailer_scaled);
        trailer_scaled=resample_art(trailer_image,{0,0,trailer_image->w,trailer_image->h},r.w,r.h);
        scaled_video_generation=trailer_generation;
    }
    if (trailer_scaled) {
        SDL_SetSurfaceAlphaMod(trailer_scaled,Uint8(255*alpha));
        SDL_SetSurfaceBlendMode(trailer_scaled,SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(trailer_scaled,nullptr,screen,&r);
    }
}
void update_trailer(int match) {
    if (match!=active_trailer) start_trailer(match);
    if (match<0) return;
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
    if (trailer_frames && size_t(frame)<trailer_offsets.size() &&
        fseek(trailer_frames,trailer_offsets[size_t(frame)],SEEK_SET)==0) {
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
