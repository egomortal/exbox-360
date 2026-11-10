// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dashboard_layout.hpp"
#include <algorithm>
#include <cstdint>
namespace avatar_placement {
using Rect=dash::TileBounds;
enum class Layer { NxeAfterPanesBeforeReflection, KinectAfterTilesBeforeIdentity, MetroAfterTilesBeforeCaption, FullBodyAfterBackdropBeforeControls };
enum class Fit { ExactRect, ContainBottom };
enum class Source { None, AcceptedEditorImage, LiveCommittedKit };
struct Identity {bool accepted_image=false,live_ready=false,resolved_matches_editor=false;uint64_t committed_generation=0;};
struct Packet {Rect destination{},clip{};Fit fit=Fit::ExactRect;float opacity=0;Layer layer=Layer::FullBodyAfterBackdropBeforeControls;Source source=Source::None;uint64_t generation=0;bool reflect=false;int cadence_ms=66;};
inline Source choose(Identity a){return a.accepted_image&&!a.resolved_matches_editor?Source::AcceptedEditorImage:a.live_ready?Source::LiveCommittedKit:a.accepted_image?Source::AcceptedEditorImage:Source::None;}
inline Rect scaled(Rect r,int density){return {r.x*density,r.y*density,r.w*density,r.h*density};}
inline Packet make(Rect d,Rect clip,float alpha,Layer layer,Identity identity,int density=1){Packet p;if(density!=1&&density!=2)return p;p.destination=scaled(d,density);p.clip=scaled(clip,density);p.opacity=std::clamp(alpha,0.f,1.f);p.layer=layer;p.source=choose(identity);p.generation=identity.committed_generation;p.fit=p.source==Source::AcceptedEditorImage?Fit::ContainBottom:Fit::ExactRect;return p;}
// Caller supplies existing channel/card fade product; this does not reschedule animation.
inline Packet nxe(float shown,Identity id,int density=1){auto p=make({440,275,400,660},{0,0,1920,1080},shown,Layer::NxeAfterPanesBeforeReflection,id,density);p.reflect=true;return p;}
inline Packet kinect(Rect tile,Identity id,int density=1){float k=tile.w/567.f;return make({tile.x+int(257*k),tile.y-int(61*k),int(340*k),int(540*k)},{0,0,1920,1080},tile.w>0?1.f:0.f,Layer::KinectAfterTilesBeforeIdentity,id,density);}
inline Rect metro_tile(bool v1,bool focused){Rect t=dash::metro_avatar_bounds(v1);if(focused&&!v1){int w=t.w+std::min(40,t.w/10),h=t.h+std::min(24,t.h*9/100);t={t.x+(t.w-w)/2,t.y+(t.h-h)/2+6,w,h};}return t;}
inline Packet metro(bool v1,bool focused,Identity id,int density=1){Rect t=metro_tile(v1,focused),body{t.x+8,t.y+6,t.w-16,t.h-55};auto p=make(body,body,1,Layer::MetroAfterTilesBeforeCaption,id,density);p.fit=Fit::ContainBottom;return p;}
inline Packet avatar(Identity id,int density=1){Rect box=choose(id)==Source::AcceptedEditorImage?Rect{0,100,1920,860}:Rect{670,40,580,920};auto p=make(box,{0,0,1920,1080},1,Layer::FullBodyAfterBackdropBeforeControls,id,density);p.cadence_ms=33;return p;}
inline Packet profile(Identity id,int density=1){auto p=make({195,535,280,440},{195,535,280,440},1,Layer::FullBodyAfterBackdropBeforeControls,id,density);p.cadence_ms=33;p.fit=Fit::ContainBottom;return p;}
inline Rect content_rect(const Packet& p,int width,int height){if(width<=0||height<=0)return {};if(p.fit==Fit::ExactRect)return p.destination;const float k=std::min(float(p.destination.w)/width,float(p.destination.h)/height);int w=int(width*k),h=int(height*k);return {p.destination.x+(p.destination.w-w)/2,p.destination.y+p.destination.h-h,w,h};}
// Packets describe placement, not an uploaded CPU-rendered avatar or resolved-part adapter.
}
