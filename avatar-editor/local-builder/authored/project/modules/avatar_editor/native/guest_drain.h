#pragma once
#include <chrono>
namespace rex::system { class XThread; }
// Called by the private native overlay at OnThreadExecute, before guest code.
void av2_native_track_guest(rex::system::XThread*);
namespace av2::native {
// Native frontend only. Retains thread objects until their host handles signal,
// even when the original TerminateTitle removes them from its registry.
bool drain_guest_threads(std::chrono::milliseconds timeout);
}
