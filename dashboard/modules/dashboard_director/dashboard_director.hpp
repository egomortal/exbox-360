// Ex-Box 360: observer-only dashboard behavior director. Pure C++17.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace exbox::dashboard_director {

enum class Family { Blades, Nxe, Kinect, MetroV1, MetroV2 };
enum class Evidence { Authored, SafePolicy, Unsupported };
enum class Event { None, ViewEnter, ViewLeave, FocusChanged, GuideOpen, GuideClose,
                   NotificationShow, NotificationHide, PlaybackActive, PlaybackPaused,
                   LaunchStart, LaunchReturn, IdleDim, IdleWake };
enum class AvatarMoment { Startup, Return, EnterStage, Selection, FocusLook, Achievement, EditorSave };

struct Capabilities {bool guide_timeline=false,notification_timeline=false,equalizer_timeline=false;};
struct Observation {
    Family family=Family::Nxe;int build=9199,view=0,focus=0;
    bool guide=false,notification=false,playback=false,paused=false,launching=false,dimmed=false;
};
struct Action {Event event=Event::None;Evidence evidence=Evidence::Unsupported;std::string_view marker{};};
struct Plan {std::array<Action,8> actions{};std::size_t count=0;void push(Action action){if(count<actions.size())actions[count++]=action;}};
struct AvatarCapabilities {bool idle=false,wave=false,dance=false,look=false,focus_look_bound=false;};
struct AvatarCue {Evidence evidence=Evidence::Unsupported;std::string_view clip{};explicit operator bool()const{return !clip.empty();}};

inline AvatarCue avatar_cue(AvatarMoment moment,const AvatarCapabilities& capabilities) {
    switch(moment) {
    case AvatarMoment::Startup:
    case AvatarMoment::Return:
    case AvatarMoment::EnterStage:return capabilities.idle?AvatarCue{Evidence::SafePolicy,"idle"}:AvatarCue{};
    case AvatarMoment::Selection:return capabilities.wave?AvatarCue{Evidence::SafePolicy,"wave"}:AvatarCue{};
    case AvatarMoment::Achievement:
    case AvatarMoment::EditorSave:return capabilities.dance?AvatarCue{Evidence::SafePolicy,"dance"}:AvatarCue{};
    case AvatarMoment::FocusLook:return capabilities.look&&capabilities.focus_look_bound?AvatarCue{Evidence::Authored,"look"}:AvatarCue{};
    }
    return {};
}

class PromotionClock {
    std::uint32_t next_=0;
public:
    bool tick(std::uint32_t now,bool eligible,bool focused) {
        if(!eligible){next_=0;return false;}
        if(!next_){next_=now+90000u;return false;}
        if(std::int32_t(now-next_)<0)return false;
        if(focused){next_=now+5000u;return false;}
        next_=now+90000u;return true;
    }
};

inline Action binding(Event event,const Capabilities& capabilities) {
    switch(event) {
    case Event::GuideOpen:return {event,capabilities.guide_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.guide_timeline?"ClosedToHalf":""};
    case Event::GuideClose:return {event,capabilities.guide_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.guide_timeline?"HalfToClosed":""};
    case Event::NotificationShow:return {event,capabilities.notification_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.notification_timeline?"TransTo":""};
    case Event::NotificationHide:return {event,capabilities.notification_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.notification_timeline?"TransFrom":""};
    case Event::PlaybackActive:return {event,capabilities.equalizer_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.equalizer_timeline?"Normal":""};
    case Event::PlaybackPaused:return {event,capabilities.equalizer_timeline?Evidence::Authored:Evidence::Unsupported,capabilities.equalizer_timeline?"NormalDisable":""};
    default:return {event,Evidence::SafePolicy,{}};
    }
}

class Director {
    Observation previous_{};bool observed_=false;std::uint32_t next_fidget_=0;int previous_fidget_=-1;
public:
    Plan observe(const Observation& current,const Capabilities& capabilities) {
        Plan out;if(!observed_){previous_=current;observed_=true;out.push(binding(Event::ViewEnter,capabilities));return out;}
        if(current.view!=previous_.view){out.push(binding(Event::ViewLeave,capabilities));out.push(binding(Event::ViewEnter,capabilities));}
        if(current.focus!=previous_.focus)out.push(binding(Event::FocusChanged,capabilities));
        if(current.guide!=previous_.guide)out.push(binding(current.guide?Event::GuideOpen:Event::GuideClose,capabilities));
        if(current.notification!=previous_.notification)out.push(binding(current.notification?Event::NotificationShow:Event::NotificationHide,capabilities));
        if(current.playback!=previous_.playback||current.paused!=previous_.paused)out.push(binding(current.playback&&!current.paused?Event::PlaybackActive:Event::PlaybackPaused,capabilities));
        if(current.launching!=previous_.launching)out.push(binding(current.launching?Event::LaunchStart:Event::LaunchReturn,capabilities));
        if(current.dimmed!=previous_.dimmed)out.push(binding(current.dimmed?Event::IdleDim:Event::IdleWake,capabilities));
        previous_=current;return out;
    }
    // Availability comes from the active rig. Unsupported clips are never
    // substituted, and an available fidget cannot immediately repeat.
    std::string_view idle_fidget(std::uint32_t now,bool visible,const std::array<bool,2>& available) {
        if(!visible){next_fidget_=0;return {};}
        if(!next_fidget_){next_fidget_=now+20000u;return {};}
        if(std::int32_t(now-next_fidget_)<0)return {};
        constexpr std::array<std::string_view,2> names{"look","shift"};int chosen=-1;
        for(int step=0;step<2;++step){const int candidate=int((now/1000u+std::uint32_t(step))%2u);if(available[std::size_t(candidate)]&&candidate!=previous_fidget_){chosen=candidate;break;}}
        next_fidget_=now+25000u+(now*7919u)%20000u;if(chosen<0)return {};
        previous_fidget_=chosen;return names[std::size_t(chosen)];
    }
};

} // namespace exbox::dashboard_director
