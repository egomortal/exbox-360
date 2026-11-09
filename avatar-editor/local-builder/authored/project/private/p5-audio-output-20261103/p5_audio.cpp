#include "silent_audio.h"
#include "boot_log.h"
#include "p5_pcm_queue.h"
#include <rex/audio/audio_system.h>
#include <rex/audio/audio_driver.h>
#include <rex/audio/conversion.h>
#include <rex/thread.h>
#include <SDL2/SDL.h>
#include <atomic>
#include <mutex>
#include <new>
namespace {std::atomic<unsigned long long> underruns{0},failures{0};}
extern "C" unsigned long long av2_audio_underruns(){return underruns.load(std::memory_order_relaxed);}
extern "C" unsigned long long av2_audio_failures(){return failures.load(std::memory_order_relaxed);}
namespace av2::native {
using rex::X_STATUS;
namespace {
class OutputDriver final:public rex::audio::AudioDriver {
 public:
  OutputDriver(rex::memory::Memory* memory,rex::thread::Semaphore* semaphore):AudioDriver(memory),semaphore_(semaphore){}
  bool initialize() {
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) {av2_boot_log("audio-error","SDL audio init: %s",SDL_GetError());return false;}
    initialized_=true;
    SDL_AudioSpec want{},got{};want.freq=48000;want.format=AUDIO_F32SYS;want.channels=2;want.samples=256;want.callback=callback;want.userdata=this;
    device_=SDL_OpenAudioDevice(nullptr,0,&want,&got,0);
    if(!device_ || got.freq!=48000 || got.format!=AUDIO_F32SYS || got.channels!=2 || !got.samples || 3u*got.samples>PcmQueue::capacity*PcmQueue::samples) {
      av2_boot_log("audio-error","PCM endpoint unavailable or incompatible: %s",SDL_GetError());return false;
    }
    av2_boot_log("audio-summary","PCM stereo 48000 Hz period=%u ring=%zu samples startup=768",unsigned(got.samples),PcmQueue::capacity*PcmQueue::samples);
    SDL_PauseAudioDevice(device_,0);return true;
  }
  ~OutputDriver() override {
    if(device_) SDL_CloseAudioDevice(device_);
    if(initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  void SubmitFrame(std::uint32_t address) override {
    PcmQueue::Frame frame;
    rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(frame.data(),memory_->TranslateVirtual<float*>(address),PcmQueue::samples,rex::audio::GetStereoFold(),rex::audio::GetOutputGain());
    bool accepted;
    {std::lock_guard lock(mutex_);accepted=queue_.push(frame);}
    if(!accepted) {++failures;av2_boot_log("audio-error","PCM ring overflow");av2_boot_request_stop("audio-overflow");}
  }
 private:
  static void callback(void* context,Uint8* bytes,int length) {
    auto& self=*static_cast<OutputDriver*>(context);
    if(length<=0) return;
    if(length%int(sizeof(float)*2)!=0) {std::memset(bytes,0,length);++failures;return;}
    PcmQueue::Read read;
    {std::lock_guard lock(self.mutex_);read=self.queue_.render(reinterpret_cast<float*>(bytes),std::size_t(length)/(sizeof(float)*2));}
    if(read.underrun) ++underruns;
    if(read.consumed && !self.semaphore_->Release(read.consumed,nullptr)) ++failures;
  }
  rex::thread::Semaphore* semaphore_;
  PcmQueue queue_;
  std::mutex mutex_;
  SDL_AudioDeviceID device_=0;
  bool initialized_=false;
};
class OutputSystem final:public rex::audio::AudioSystem {
 public: explicit OutputSystem(rex::runtime::FunctionDispatcher* dispatcher):AudioSystem(dispatcher){}
 protected:
  rex::X_STATUS CreateDriver(std::size_t,rex::thread::Semaphore* semaphore,rex::audio::AudioDriver** out) override {
    *out=nullptr;
    try {auto driver=std::make_unique<OutputDriver>(memory(),semaphore);if(!driver->initialize())return X_STATUS_UNSUCCESSFUL;*out=driver.release();}
    catch(const std::bad_alloc&) {return X_STATUS_NO_MEMORY;}
    return X_STATUS_SUCCESS;
  }
  void DestroyDriver(rex::audio::AudioDriver* driver) override {delete driver;}
};
}
// Retain the factory ABI so no main/frontend code needs changing.
std::unique_ptr<rex::audio::AudioSystem> create_silent_audio(rex::runtime::FunctionDispatcher* dispatcher) {return std::make_unique<OutputSystem>(dispatcher);}
}

