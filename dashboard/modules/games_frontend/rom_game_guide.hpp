// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>

namespace exbox_rom_guide {
inline constexpr const char* preview_title="Experimental In-Game Guide";
inline constexpr const char* preview_fallback="Preview only - cooperative game host not connected";
enum class Button { Up, Down, Left, Right, A, B, Guide, None };
enum class Item { Resume, Home, ControllerMap, Music, Settings, Quit };
enum class Action { None, Close, OpenHome, OpenControllerMap, OpenMusic, OpenSettings, RequestExit };
struct Capabilities { bool cooperative_overlay=false, pause_resume=false, return_dashboard=false, controller_map=false, music=false, settings=false; };
struct Row { Item item; const char* label; bool enabled; const char* unavailable_reason; };
struct Result { Action action=Action::None; bool request_pause=false, request_resume=false; };

// Explicitly Experimental dashboard preview. It owns no display, process,
// transport or input hook. A separately owned cooperative host must advertise
// capabilities before any cross-title operation can become enabled.
class Model {
public:
    explicit Model(Capabilities caps={}) : caps_(caps) {}
    void set_capabilities(Capabilities caps){session_lost();caps_=caps;}
    Capabilities capabilities()const{return caps_;}
    bool open()const{return open_;} std::size_t selected()const{return selected_;}
    bool pause_confirmed()const{return pause_confirmed_;} bool pause_owned_by_guide()const{return pause_owned_;}
    std::vector<Row> rows()const{return {
        {Item::Resume,"Resume Game",caps_.cooperative_overlay,"Overlay host unavailable"},
        {Item::Home,"Xbox Home",caps_.return_dashboard,"Return-to-dashboard unsupported"},
        {Item::ControllerMap,"Controller Map",caps_.controller_map,"Controller map unsupported"},
        {Item::Music,"Music",caps_.music,"Game music controls unsupported"},
        {Item::Settings,"Settings",caps_.settings,"In-game settings unsupported"},
        {Item::Quit,"Quit to Dashboard",caps_.return_dashboard,"Safe exit unsupported"}};}
    Result press(Button b){
        if(b==Button::Guide){if(open_)return close();open_=true;selected_=0;return {Action::None,caps_.pause_resume,false};}
        if(!open_)return{};if(b==Button::B)return close();const auto list=rows();
        if(b==Button::Up)selected_=selected_==0?list.size()-1:selected_-1;
        if(b==Button::Down)selected_=(selected_+1)%list.size();
        if(b!=Button::A||!list[selected_].enabled)return{};
        switch(list[selected_].item){case Item::Resume:return close();case Item::Home:return{Action::OpenHome};
        case Item::ControllerMap:return{Action::OpenControllerMap};case Item::Music:return{Action::OpenMusic};
        case Item::Settings:return{Action::OpenSettings};case Item::Quit:return{Action::RequestExit};}return{};
    }
    void acknowledge_pause(bool applied){pause_confirmed_=applied;pause_owned_=applied;}
    void session_lost(){open_=pause_confirmed_=pause_owned_=false;selected_=0;}
private:
    Result close(){open_=false;const bool resume=pause_owned_;pause_confirmed_=pause_owned_=false;return{Action::Close,false,resume};}
    Capabilities caps_{};bool open_=false,pause_confirmed_=false,pause_owned_=false;std::size_t selected_=0;
};
} // namespace exbox_rom_guide
