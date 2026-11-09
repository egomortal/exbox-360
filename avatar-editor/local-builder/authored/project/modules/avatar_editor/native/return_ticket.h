#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace av2::native {
// Ex-Box addition: the original editor has no dashboard return protocol.
// Dashboard state is opaque here: only the dashboard knows its full UI model.
// Carry its exact bytes back, rather than reconstructing a partial screen.
enum class ExitResult { saved, cancelled, failed };
struct ReturnTicket {
  std::string nonce; // 32 lowercase hexadecimal characters, created by launcher.
  std::uint32_t era=0;
  std::uint32_t revision=0;
  ExitResult result=ExitResult::cancelled;
  std::string dashboard_state;
};
constexpr std::size_t kMaxDashboardState=65536;
// Failures leave the caller's output untouched. Parsing never executes state.
bool encode_return_ticket(const ReturnTicket&, std::string& output);
bool decode_return_ticket(std::string_view bytes, ReturnTicket& output);
}
