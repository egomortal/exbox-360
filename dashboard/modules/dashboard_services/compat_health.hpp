// Ex-Box 360: bounded, truthful compatibility/health summaries.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace exbox_services::health {
struct Snapshot {
 bool ps5x360_installed=false,companion_advertised=false,dispatch_allowed=false;
 std::size_t xbox_titles=0,continue_playing=0;bool xbox_inventory_scanned=false,xbox_inventory_limited=false;std::size_t xbox_rejected=0;
 std::size_t artwork_mapped=0,artwork_unmapped=0;
 int soundtrack_playable=-1,soundtrack_albums=-1,soundtrack_recognized_unsupported=-1,soundtrack_containers=-1;
 int themes_complete=-1,themes_partial=-1,themes_rejected=-1,themes_missing=-1,themes_changed=-1;
 bool authored_theme_evidence=false;
 bool native_presenter_requested=false,native_presenter_active=false,gpu_dashboard_requested=false,gpu_dashboard_available=false,gpu_fallback=false;
 bool diagnostics_available=false;int missing_assets=-1;
};
struct Row{std::string label,value,description;};
inline std::string count_or_unknown(int value,const char*suffix=""){return value<0?"Not published":std::to_string(value)+suffix;}
inline std::vector<Row> rows(const Snapshot&s){
 std::vector<Row>out;out.reserve(9);
 const std::string player=s.ps5x360_installed?"Installed":"Not installed",companion=s.companion_advertised?"cooperative host advertised":"cooperative host absent";
 out.push_back({"PS5X360 & In-Game Guide",player+"; "+companion,s.companion_advertised?(s.dispatch_allowed?"The cooperative host advertised launch and return support.":"The host is visible, but game dispatch remains disabled because this dashboard has no launch authority."):"The dashboard preview is safe to use. Over-game actions remain disabled until PS5X360 or an approved companion advertises the cooperative contract."});
 out.push_back({"Xbox 360 Library",s.xbox_inventory_scanned?std::to_string(s.xbox_titles)+" supplied title"+(s.xbox_titles==1?"":"s"):"Not scanned",std::string("Supplied folder/title metadata only; ROM content is not read.")+(s.xbox_inventory_limited?" The bounded inventory is limited.":"")+(s.xbox_rejected?" Rejected unsafe/invalid records: "+std::to_string(s.xbox_rejected)+".":"")});
 out.push_back({"Continue Playing",std::to_string(s.continue_playing)+" saved identit"+(s.continue_playing==1?"y":"ies"),"PS5/homebrew entries restore stable focus. Xbox 360 entries remain inert until launch/return is hardware-proven."});
 out.push_back({"Game Soundtracks",count_or_unknown(s.soundtrack_playable," playable")+"; "+count_or_unknown(s.soundtrack_albums," game albums"),"Recognized unsupported audio: "+count_or_unknown(s.soundtrack_recognized_unsupported)+". Containers: "+count_or_unknown(s.soundtrack_containers)+". No mounting, decryption, extraction, or DRM bypass."});
 out.push_back({"Artwork Mapping",s.xbox_inventory_scanned?std::to_string(s.artwork_mapped)+" mapped; "+std::to_string(s.artwork_unmapped)+" need mapping":"Not scanned","Metadata references only. This page does not open, copy, authenticate, or export owner artwork."});
 out.push_back({"Themes",count_or_unknown(s.themes_complete," complete")+"; "+count_or_unknown(s.themes_partial," partial"),"Rejected: "+count_or_unknown(s.themes_rejected)+". Missing: "+count_or_unknown(s.themes_missing)+". Changed: "+count_or_unknown(s.themes_changed)+". "+(s.authored_theme_evidence?"Exact authored evidence is available.":"Premium roles retain Compatibility/default fallback until route, z-order, crop, and lighting evidence is complete.")});
 const std::string presenter=s.native_presenter_requested?(s.native_presenter_active?"native presenter active":"native presenter fell back"):"native presenter not requested";
 const std::string gpu=s.gpu_dashboard_requested?(s.gpu_dashboard_available?"GPU dashboard available":"GPU dashboard unavailable/fallback"):"GPU dashboard not requested";
 out.push_back({"Renderer",presenter+"; "+gpu,std::string("CPU rendering remains the safe fallback. ")+(s.gpu_fallback?"A requested GPU route fell back; inspect diagnostics before retrying.":"Native 4K60 still requires console frame logs and owner TV approval.")});
 out.push_back({"Developer Diagnostics",s.diagnostics_available?"Available":"Hidden (flag absent)","Read-only and off by default. Presenter-owned counters stay Not published until their owner supplies a synchronized snapshot."});
 out.push_back({"Missing Local Assets",count_or_unknown(s.missing_assets),s.missing_assets<0?"No authoritative missing-asset counter has been published. Zero is not assumed. Safe next action: enable read-only Developer Diagnostics or use the relevant local picker.":"A bounded published count; private paths and names are not shown. Safe next action: use the relevant local artwork or theme picker; no files are copied here."});
 return out;
}
} // namespace exbox_services::health
