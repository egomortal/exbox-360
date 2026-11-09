// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL2/SDL.h>
namespace exbox::audio {
struct Calls {
 int (*init)(Uint32);void (*quit)(Uint32);
 SDL_AudioDeviceID (*open)(const char*,int,const SDL_AudioSpec*,SDL_AudioSpec*,int);
 void (*close)(SDL_AudioDeviceID);void (*pause)(SDL_AudioDeviceID,int);
};
inline Calls sdl_calls(){return {SDL_InitSubSystem,SDL_QuitSubSystem,SDL_OpenAudioDevice,SDL_CloseAudioDevice,SDL_PauseAudioDevice};}
// Same SDL2 PS5 AudioOut backend as editor P5; no guest queue or decoder copy.
class Endpoint {
 Calls calls_;void(*note_)(const char*);SDL_AudioDeviceID device_=0;bool initialized_=false;
public:
 Endpoint(Calls calls,void(*note)(const char*)):calls_(calls),note_(note){}
 SDL_AudioDeviceID open(const SDL_AudioSpec& want){
  if(device_)return device_;
  if(calls_.init(SDL_INIT_AUDIO)!=0){if(note_)note_("audio: SDL AudioOut initialization failed; continuing silent");return 0;}
  initialized_=true;SDL_AudioSpec got{};device_=calls_.open(nullptr,0,&want,&got,0);
  if(!device_||got.freq!=want.freq||got.format!=want.format||got.channels!=want.channels||!got.samples||got.samples>4096){
   if(note_){note_("audio: unavailable/incompatible PCM endpoint; continuing silent");}close();return 0;
  }
  calls_.pause(device_,0);if(note_)note_("audio: shared SDL AudioOut endpoint active");return device_;
 }
 void close(){
  if(device_){calls_.close(device_);device_=0;if(note_)note_("audio: callbacks stopped and endpoint closed");}
  if(initialized_){calls_.quit(SDL_INIT_AUDIO);initialized_=false;}
 }
 SDL_AudioDeviceID device()const{return device_;}
};
}