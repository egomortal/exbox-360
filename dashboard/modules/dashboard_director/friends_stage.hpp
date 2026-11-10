// Ex-Box 360: evidence-gated Friends/Social avatar stage placement. Pure.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dashboard_director.hpp"
#include <string_view>
namespace exbox::friends_stage {
struct Rect {int x=0,y=0,w=0,h=0;};
enum class Status { Hidden, Ready, Blocked };
struct Route {Status status=Status::Hidden;Rect bounds{};std::string_view reason{};};
inline Route route(dashboard_director::Family family,bool social_visible) {
    if(!social_visible)return {};
    switch(family) {
    // Retail Social captures place the signed-in avatar immediately right of
    // the gamercard, followed by friends in one standing lineup.  This is a
    // stage in the open middle of the page, not a far-right portrait box.
    case dashboard_director::Family::MetroV1:return {Status::Ready,{685,295,310,590},{}};
    case dashboard_director::Family::MetroV2:return {Status::Ready,{690,295,300,590},{}};
    case dashboard_director::Family::Blades:
        return {Status::Blocked,{},"Blades Community is a list/gamercard blade; supplied signed-out scenes do not prove a standing-avatar stage."};
    case dashboard_director::Family::Nxe:
        return {Status::Blocked,{},"NXE Community is livepack-driven; only the static Add Friend slot is source-settled and friend subjects/transforms remain unresolved."};
    case dashboard_director::Family::Kinect:
        return {Status::Blocked,{},"Kinect Community is livepack-driven; friend subjects, host transforms and entrance bindings remain unresolved."};
    }
    return {Status::Blocked,{},"Unsupported dashboard family."};
}
} // namespace exbox::friends_stage
