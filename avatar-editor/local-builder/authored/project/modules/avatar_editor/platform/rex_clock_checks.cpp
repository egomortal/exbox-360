// Compile all SDK-supported clock pairs on both toolchains. Actual time scaling
// is still the upstream implementation; this checks the compatibility dispatch.
#include <rex/chrono/chrono_steady_cast.h>
namespace av2::compat {
void compile_clock_pairs(rex::chrono::WinSystemClock::time_point host,
                         rex::chrono::XSystemClock::time_point guest,
                         std::chrono::steady_clock::time_point steady) {
  using namespace rex::chrono;
  (void)rex::chrono_compat::clock_cast<WinSystemClock>(guest);
  (void)rex::chrono_compat::clock_cast<XSystemClock>(host);
  (void)rex::chrono_compat::clock_cast<WinSystemClock>(steady);
  (void)rex::chrono_compat::clock_cast<std::chrono::steady_clock>(host);
}
}
