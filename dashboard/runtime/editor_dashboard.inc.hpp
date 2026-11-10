#include "prepared_avatar.inc.hpp"
// Ex-Box addition: consume original editor saves without changing NXAK.
namespace edited_avatar {
SDL_Surface *fullbody=nullptr,*gamerpic=nullptr;
std::array<SDL_Surface*,3> friend_fullbody{};
std::string signature;Uint32 checked=0;
// A single selection policy for all scene shells. Returned surfaces are owned
// by this cache; callers must not free them or change the original PNG pixels.
SDL_Surface* image(const avatar_slots::AvatarSlot& slot) {
 switch(avatar_slots::resolve(slot,fullbody!=nullptr,gamerpic!=nullptr)) {
 case avatar_slots::Resolution::SavedFullBody:return fullbody;
 case avatar_slots::Resolution::SavedGamerPicture:return gamerpic;
 default:return nullptr;
 }
}
SDL_Surface* owner_still(avatar_slots::Image kind) {
 avatar_slots::AvatarSlot slot;slot.who=avatar_slots::Who::SignedInUser;slot.image=kind;
 return image(slot);
}
SDL_Surface* friend_still(int index) {
 if(auto*live=prepared_avatar::frame(index+1,SDL_GetTicks()))return live;
 return index>=0&&index<int(friend_fullbody.size())?friend_fullbody[std::size_t(index)]:nullptr;
}
// A live actor wins only for its exact committed editor generation. A newer
// save without matching resolved resources immediately keeps the accepted
// editor image; never replace it with an unrelated older kit.
SDL_Surface* live_or_still(Uint32 now,bool exact=false,SDL_Surface* fallback=nullptr) {
 if(auto*live=prepared_avatar::frame(0,now))return live;
 if(SDL_Surface* still=owner_still(avatar_slots::Image::FullBody))return still;
 if(look3d::active())if(SDL_Surface* live=look3d::hub_frame(now,exact))return live;
 return fallback;
}
std::filesystem::path data(){return fs_prefix()+"/data/nxe";}
SDL_Rect contain(SDL_Surface* image,SDL_Rect box){const float scale=prepared_avatar::find(image)?float(box.h)/image->h:std::min(float(box.w)/image->w,float(box.h)/image->h);const int w=int(image->w*scale),h=int(image->h*scale);return {box.x+(box.w-w)/2,box.y+box.h-h,w,h};}
void close(){
 if(fullbody)SDL_FreeSurface(fullbody);if(gamerpic)SDL_FreeSurface(gamerpic);
 for(auto*&body:friend_fullbody){if(body)SDL_FreeSurface(body);body=nullptr;}
 fullbody=gamerpic=nullptr;signature.clear();
}
void refresh(bool force=false){
 prepared_avatar::refresh(force);
 const auto now=SDL_GetTicks();if(!force&&now-checked<1000)return;checked=now;
 const auto images=editor_dashboard::preferred_images(data()/"avatar/editor",fs_prefix()+look3d::look_path);
 std::array<editor_dashboard::Images,3> friends;
 std::string combined=images.signature;
 for(int i=0;i<3;++i){friends[std::size_t(i)]=editor_dashboard::preferred_images(data()/"avatar/friends"/std::to_string(i+1),fs_prefix()+look3d::look_path);combined+='|'+friends[std::size_t(i)].signature;}
 if(!force&&signature==combined)return;
 SDL_Surface* body=images.fullbody.empty()?nullptr:load_png(images.fullbody);
 SDL_Surface* head=images.gamerpic.empty()?nullptr:load_png(images.gamerpic);
 std::array<SDL_Surface*,3> friend_bodies{};
 for(int i=0;i<3;++i)if(!friends[std::size_t(i)].fullbody.empty())friend_bodies[std::size_t(i)]=load_png(friends[std::size_t(i)].fullbody);
 close();fullbody=body;gamerpic=head;friend_fullbody=friend_bodies;signature=combined;
 if(fullbody)avatar_alpha::clean(fullbody);if(gamerpic)avatar_alpha::clean(gamerpic);
 for(auto* friend_body:friend_fullbody)if(friend_body)avatar_alpha::clean(friend_body);
}
bool consume(){
 std::string reason;const bool restored=editor_dashboard::consume(data(),model,std::time(nullptr),reason);
 if(!reason.empty())log(reason.c_str());
 refresh(true);return restored;
}
}
