#include "p6_input_latency.h"
#include <time.h>
namespace { av2::input_latency::Collector p6_collector; }
extern "C" void av2_input_present(uint64_t now){p6_collector.present(now);}
extern "C" void av2_input_latency_snapshot(av2::input_latency::Snapshot* out){if(out)*out=p6_collector.snapshot();}
#include "native_pad.h"
#include "pad_keys.h"
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/device_assignment.h>
#include <rex/chrono/clock.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
namespace av2::native {
namespace {
using namespace rex;
using namespace rex::input;
struct Snapshot {
  std::mutex mutex;
  DeviceId id=DeviceId::kInvalid;
  X_INPUT_STATE state{};
  PadRepeat repeat{};
  std::string name;
};
class PadDriver final:public InputDriver {
 public:
  explicit PadDriver(std::shared_ptr<Snapshot> state):InputDriver(nullptr,0),state_(std::move(state)) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    std::lock_guard lock(state_->mutex);
    if(state_->id!=DeviceId::kInvalid) out.push_back({state_->id,0,state_->name,"av2-sdl2-pad",false});
  }
  X_RESULT GetDeviceState(DeviceId id,X_INPUT_STATE* out) override {
    if(!out) return X_ERROR_BAD_ARGUMENTS;
    std::lock_guard lock(state_->mutex);
    if(id==DeviceId::kInvalid || id!=state_->id) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out=state_->state;
    if(!is_active()) out->gamepad={};
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id,uint32_t,X_INPUT_CAPABILITIES* out) override {
    if(!out) return X_ERROR_BAD_ARGUMENTS;
    std::lock_guard lock(state_->mutex);
    if(id==DeviceId::kInvalid || id!=state_->id) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out={};out->type=1;out->sub_type=1;
    out->gamepad.buttons=0xF3FF;
    out->gamepad.left_trigger=out->gamepad.right_trigger=255;
    out->gamepad.thumb_lx=out->gamepad.thumb_ly=out->gamepad.thumb_rx=out->gamepad.thumb_ry=int16_t(-1);
    // Rumble/wireless capabilities are not claimed without backend validation.
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id,X_INPUT_VIBRATION*) override {
    std::lock_guard lock(state_->mutex);
    return id!=DeviceId::kInvalid && id==state_->id?X_ERROR_FUNCTION_FAILED:X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(DeviceId id,uint32_t,X_INPUT_KEYSTROKE* out) override {
    std::lock_guard lock(state_->mutex);
    if(id==DeviceId::kInvalid || id!=state_->id) return X_ERROR_DEVICE_NOT_CONNECTED;
    return next_pad_key(state_->state.gamepad,state_->repeat,
        rex::chrono::Clock::QueryGuestUptimeMillis(),is_active(),out);
  }
 private:
  std::shared_ptr<Snapshot> state_;
};
}
struct NativePad::Impl {
  std::shared_ptr<Snapshot> snapshot=std::make_shared<Snapshot>();
  SDL_GameController* controller=nullptr;
  uint64_t generation=0;
  bool initialized=false;
};
NativePad::NativePad():impl_(std::make_unique<Impl>()) {}
NativePad::~NativePad() {
  if(impl_->controller) SDL_GameControllerClose(impl_->controller);
  if(impl_->initialized) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS);
}
bool NativePad::initialize() {
  if(impl_->initialized) return true;
  if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0) {
    std::fprintf(stderr,"AV2 controller initialization failed: %s\n",SDL_GetError());
    return false;
  }
  impl_->initialized=true;poll();return true;
}
void NativePad::poll() {
  if(!impl_->initialized) return;
  SDL_Event event;
  while(SDL_PollEvent(&event)) {} // No window; drain joystick events, then snapshot.
  SDL_GameControllerUpdate();
  auto& shared=*impl_->snapshot;
  if(impl_->controller && !SDL_GameControllerGetAttached(impl_->controller)) {
    SDL_GameControllerClose(impl_->controller);impl_->controller=nullptr;
    std::lock_guard lock(shared.mutex);
    shared.id=DeviceId::kInvalid;shared.state={};shared.repeat={};shared.name.clear();
  }
  if(!impl_->controller) {
    for(int i=0;i<SDL_NumJoysticks();++i) {
      if(!SDL_IsGameController(i)) continue;
      impl_->controller=SDL_GameControllerOpen(i);
      if(impl_->controller) break;
    }
    if(!impl_->controller) return;
    std::lock_guard lock(shared.mutex);
    shared.id=static_cast<DeviceId>(++impl_->generation);shared.repeat={};shared.state={};
    const char* name=SDL_GameControllerName(impl_->controller);
    shared.name=name?name:"Controller";
    std::fprintf(stderr,"AV2 controller connected: %s\n",shared.name.c_str());
  }
  X_INPUT_GAMEPAD pad{};
  // SDL's standardized physical positions match the original SDK mapping:
  // Cross/A, Circle/B, Square/X, Triangle/Y. Guide stays with the system.
  static constexpr std::array<uint16_t,15> buttons={
    X_INPUT_GAMEPAD_A,X_INPUT_GAMEPAD_B,X_INPUT_GAMEPAD_X,X_INPUT_GAMEPAD_Y,
    X_INPUT_GAMEPAD_BACK,0,X_INPUT_GAMEPAD_START,
    X_INPUT_GAMEPAD_LEFT_THUMB,X_INPUT_GAMEPAD_RIGHT_THUMB,
    X_INPUT_GAMEPAD_LEFT_SHOULDER,X_INPUT_GAMEPAD_RIGHT_SHOULDER,
    X_INPUT_GAMEPAD_DPAD_UP,X_INPUT_GAMEPAD_DPAD_DOWN,
    X_INPUT_GAMEPAD_DPAD_LEFT,X_INPUT_GAMEPAD_DPAD_RIGHT};
  static_assert(SDL_CONTROLLER_BUTTON_A==0 && SDL_CONTROLLER_BUTTON_DPAD_RIGHT==14);
  uint16_t held=0;
  for(std::size_t i=0;i<buttons.size();++i)
    if(SDL_GameControllerGetButton(impl_->controller,static_cast<SDL_GameControllerButton>(i))) held|=buttons[i];
  pad.buttons=held;
  auto axis=[&](SDL_GameControllerAxis id){return SDL_GameControllerGetAxis(impl_->controller,id);};
  pad.thumb_lx=axis(SDL_CONTROLLER_AXIS_LEFTX);pad.thumb_ly=int16_t(~axis(SDL_CONTROLLER_AXIS_LEFTY));
  pad.thumb_rx=axis(SDL_CONTROLLER_AXIS_RIGHTX);pad.thumb_ry=int16_t(~axis(SDL_CONTROLLER_AXIS_RIGHTY));
  pad.left_trigger=uint8_t(std::max<int>(0,axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT))>>7);
  pad.right_trigger=uint8_t(std::max<int>(0,axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT))>>7);
  std::lock_guard lock(shared.mutex);
  if(std::memcmp(&pad,&shared.state.gamepad,sizeof(pad))!=0) {
    shared.state.gamepad=pad;shared.state.packet_number=uint32_t(shared.state.packet_number)+1;
    timespec observed{};const bool timed=clock_gettime(CLOCK_MONOTONIC,&observed)==0;
    p6_collector.changed(timed?uint64_t(observed.tv_sec)*1000000000ULL+uint64_t(observed.tv_nsec):0);
  }
}
std::unique_ptr<rex::input::InputSystem> NativePad::make_input_system() {
  if(!impl_->initialized) return nullptr;
  auto input=std::make_unique<rex::input::InputSystem>(nullptr);
  input->AddDriver(std::make_unique<PadDriver>(impl_->snapshot));
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  return input;
}
}
