// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../games_frontend/rom_game_guide.hpp"

#include <cstdint>
#include <string>

namespace exbox_services::rom_guide_companion {

inline constexpr std::uint32_t advertisement_version=1;

struct Advertisement {
    std::uint32_t version=advertisement_version;
    std::string host_kind;
    std::string session_id;
    std::uint64_t generation=0;
    bool exclusive_overlay_owner=false;
    bool request_acknowledgements=false;
    bool request_deduplication=false;
    bool pause_resume=false;
    bool return_dashboard=false;
    bool safe_exit_ack=false;
    bool controller_map=false;
    bool music=false;
    bool settings=false;
};

struct Negotiation {
    bool accepted=false;
    exbox_rom_guide::Capabilities capabilities{};
    const char* reason="No cooperative companion advertisement";
};

// Transport-neutral capability source. The dashboard ships with no source;
// a future cooperative foreground owner may provide one without adding an
// endpoint, launcher or overlay owner to Ex-Box.
class AdvertisementAdapter {
public:
    virtual ~AdvertisementAdapter()=default;
    virtual bool read(Advertisement& advertisement)const=0;
};

inline bool safe_token(const std::string&token){
    if(token.empty()||token.size()>64)return false;
    for(unsigned char c:token)if(c<0x21||c>0x7e)return false;
    return true;
}

inline Negotiation negotiate(const Advertisement&ad){
    Negotiation out;
    if(ad.version!=advertisement_version){out.reason="Companion protocol version mismatch";return out;}
    if(ad.host_kind!="ps5x360-cooperative-v1"){out.reason="Unapproved companion kind";return out;}
    if(!safe_token(ad.session_id)||!ad.generation){out.reason="Invalid companion session identity";return out;}
    if(!ad.exclusive_overlay_owner){out.reason="Companion does not own the over-game surface";return out;}
    if(!ad.request_acknowledgements||!ad.request_deduplication){out.reason="Companion lacks safe request semantics";return out;}
    out.accepted=true;out.reason="Cooperative companion capabilities accepted";
    out.capabilities.cooperative_overlay=true;
    out.capabilities.pause_resume=ad.pause_resume;
    out.capabilities.return_dashboard=ad.return_dashboard&&ad.safe_exit_ack;
    out.capabilities.controller_map=ad.controller_map;
    out.capabilities.music=ad.music;
    out.capabilities.settings=ad.settings;
    return out;
}

inline Negotiation negotiate(const AdvertisementAdapter* source){
    if(!source)return{};
    Advertisement advertisement;
    if(!source->read(advertisement))return{};
    return negotiate(advertisement);
}

inline Negotiation apply(exbox_rom_guide::Model& model,const AdvertisementAdapter* source){
    Negotiation result=negotiate(source);
    model.set_capabilities(result.capabilities);
    return result;
}

} // namespace exbox_services::rom_guide_companion
