// SPDX-License-Identifier: GPL-3.0-or-later
#include "key_bindings.hpp"
#include "keystroke_delivery.hpp"
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <chrono>
#include <mutex>
#include <string>
namespace rex::kernel::xam {
u32 XamInputGetKeystrokeEx_entry(mapped_u32,u32,ppc_ptr_t<rex::input::X_INPUT_KEYSTROKE>);
}
namespace {
using Key=rex::ui::VirtualKey;
struct Binding {Key pad;const char* setting;};
constexpr Binding bindings[]={
 {Key::kXInputPadA,"keybind_a"},{Key::kXInputPadB,"keybind_b"},
 {Key::kXInputPadX,"keybind_x"},{Key::kXInputPadY,"keybind_y"},
 {Key::kXInputPadDpadUp,"keybind_dpad_up"},{Key::kXInputPadDpadDown,"keybind_dpad_down"},
 {Key::kXInputPadDpadLeft,"keybind_dpad_left"},{Key::kXInputPadDpadRight,"keybind_dpad_right"},
 {Key::kXInputPadStart,"keybind_start"},{Key::kXInputPadBack,"keybind_back"},
 {Key::kXInputPadLShoulder,"keybind_left_shoulder"},{Key::kXInputPadRShoulder,"keybind_right_shoulder"},
 {Key::kXInputPadLThumbUp,"keybind_lstick_up"},{Key::kXInputPadLThumbDown,"keybind_lstick_down"},
 {Key::kXInputPadLThumbLeft,"keybind_lstick_left"},{Key::kXInputPadLThumbRight,"keybind_lstick_right"}};
std::mutex input_mutex;
exbox::editor::KeystrokeQueue strokes;
uint64_t milliseconds() {
 return std::chrono::duration_cast<std::chrono::milliseconds>(
  std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
namespace ae_input {
// Compatibility entry point for the existing host window and scripted harness.
// The independent parser and queue do not depend on the title's implementation.
void OnHostKey(Key key,bool down,bool shift,bool ctrl,bool alt) {
 std::lock_guard lock(input_mutex);
 const auto source=static_cast<uint32_t>(key);
 if(!down){strokes.release(source);return;}
 const uint8_t mods=(shift?1:0)|(ctrl?2:0)|(alt?4:0);
 for(const auto& binding:bindings) {
  if(!rex::cvar::GetFlagInfo(binding.setting))continue;
  const auto spec=rex::cvar::Query<std::string>(binding.setting);
  if(exbox::editor::binding_matches(spec,mods,[key](auto name){return rex::ui::ParseVirtualKey(name)==key;}))
   strokes.press(static_cast<uint16_t>(binding.pad),source,milliseconds());
 }
}
}
extern "C" void __imp__XamInputGetKeystrokeEx(PPCContext& ctx,uint8_t*) {
 const uint32_t user_ea=ctx.r3.u32, flags=ctx.r4.u32, output_ea=ctx.r5.u32;
 auto* memory=rex::system::kernel_state()->memory();
 auto* user=user_ea?memory->TranslateVirtual<rex::be_u32*>(user_ea):nullptr;
 auto* output=output_ea?memory->TranslateVirtual<rex::input::X_INPUT_KEYSTROKE*>(output_ea):nullptr;
 // The SDK dereferences user unconditionally for valid gamepad queries. Reject
 // a null user before that dereference; this is a defensive invalid-call path.
 uint32_t result=87; // X_ERROR_BAD_ARGUMENTS
 if(user || !output || ((flags&255U)!=0 && (flags&1U)==0))
  result=rex::kernel::xam::XamInputGetKeystrokeEx_entry({user,user_ea},flags,{output,output_ea});
 ctx.r3.u64=result;
 if(!output)return;
 std::lock_guard lock(input_mutex);
 std::span<uint8_t> user_bytes=user?std::span(reinterpret_cast<uint8_t*>(user),4):std::span<uint8_t>{};
 if(exbox::editor::deliver_fallback(strokes,milliseconds(),result,flags,
     std::span(reinterpret_cast<uint8_t*>(output),8),user_bytes))ctx.r3.u64=0;
}
