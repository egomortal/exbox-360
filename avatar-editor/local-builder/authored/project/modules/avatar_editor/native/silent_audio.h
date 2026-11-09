#pragma once
#include <memory>
namespace rex::runtime { class FunctionDispatcher; }
namespace rex::audio { class AudioSystem; }
namespace av2::native {
// Q7 deliberately emits no sound. XMA decoding and guest callbacks remain
// owned by the original AudioSystem; only PCM device output is replaced.
std::unique_ptr<rex::audio::AudioSystem> create_silent_audio(rex::runtime::FunctionDispatcher*);
}
