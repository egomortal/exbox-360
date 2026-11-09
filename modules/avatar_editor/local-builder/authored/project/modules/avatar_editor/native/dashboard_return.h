#pragma once
#include "return_ticket.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
namespace av2::native {
struct DashboardContext { bool available=false;ReturnTicket ticket; };
bool consume_dashboard_context(const std::filesystem::path&,DashboardContext&);
bool publish_dashboard_return(const DashboardContext&,ExitResult,const std::filesystem::path&);
// Port/timeout arguments support a local test server; production always uses
// 127.0.0.1:7090. No DNS, remote-host configuration or console connection here.
bool request_companion_launch(std::uint16_t port=7090,std::chrono::milliseconds timeout=std::chrono::seconds(2));
void prepare_dashboard_return();
void set_dashboard_exit_result(ExitResult);
void return_to_dashboard(int exit_status) noexcept;
}
