#pragma once
// Ex-Box addition: dashboard-owned navigation and separate editor handoff.
#include "../../dash_model.hpp"
#include "../avatar_editor/native/ticket_file.h"
#include <cstdint>
#include <filesystem>
#include <string>
namespace editor_dashboard {
std::string screen_state(const dash::Model&,std::int64_t now);
bool restore_state(const std::string&,dash::Model&,std::int64_t now);
bool prepare(const std::filesystem::path& data,const dash::Model&,std::int64_t now,std::string& reason);
bool consume(const std::filesystem::path& data,dash::Model&,std::int64_t now,std::string& reason);
void abandon(const std::filesystem::path& data);
constexpr const char* title_id="PPSA99734";
constexpr const char* launch_command="LAUNCH\tPPSA99734\n";
struct Images{std::string fullbody,gamerpic,signature;};
Images preferred_images(const std::filesystem::path& folder,const std::filesystem::path& dashboard_look);
}
