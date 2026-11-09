#pragma once
// Only called after the diagnostic worker is joined. No guest, renderer,
// runtime, pad, or save destructor is allowed on this path.
extern "C" [[noreturn]] void av2_exit_with_live_guests(int status);
