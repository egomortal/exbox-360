#pragma once
#include <memory>
namespace rex::input { class InputSystem; }
namespace av2::native {
// Own on the frontend thread, before the runtime, and destroy after it.
// SDL calls happen only in initialize/poll/destruction on that same thread.
// Guest input calls read a locked snapshot and never pump OS events.
class NativePad {
 public:
  NativePad();
  ~NativePad();
  NativePad(const NativePad&)=delete;
  NativePad& operator=(const NativePad&)=delete;
  bool initialize();
  void poll();
  std::unique_ptr<rex::input::InputSystem> make_input_system();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
