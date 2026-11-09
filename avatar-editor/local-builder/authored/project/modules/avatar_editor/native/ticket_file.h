#pragma once
#include "return_ticket.h"
#include <filesystem>
namespace av2::native {
// Ex-Box addition. The launcher owns these paths; never derive them from
// dashboard_state. The caller distinguishes publication from power-loss safety.
enum class TicketWrite { failed, published, durable };
// If requested, atomically preserve the previous bytes as <path>.bak first.
TicketWrite write_atomic_bytes(const std::filesystem::path&,std::string_view,bool backup=false);
TicketWrite write_return_ticket(const std::filesystem::path&,const ReturnTicket&);
// Only bounded regular files are accepted. Invalid input leaves output intact.
bool read_return_ticket(const std::filesystem::path&,ReturnTicket&);
}
