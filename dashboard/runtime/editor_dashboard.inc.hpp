// Ex-Box addition: consume original editor saves without changing NXAK.
namespace edited_avatar {
SDL_Surface *fullbody=nullptr,*gamerpic=nullptr;
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
std::filesystem::path data(){return fs_prefix()+"/data/nxe";}
SDL_Rect contain(SDL_Surface* image,SDL_Rect box){const float scale=std::min(float(box.w)/image->w,float(box.h)/image->h);const int w=int(image->w*scale),h=int(image->h*scale);return {box.x+(box.w-w)/2,box.y+box.h-h,w,h};}
void close(){if(fullbody)SDL_FreeSurface(fullbody);if(gamerpic)SDL_FreeSurface(gamerpic);fullbody=gamerpic=nullptr;signature.clear();}
void refresh(bool force=false){
 const auto now=SDL_GetTicks();if(!force&&now-checked<1000)return;checked=now;
 const auto images=editor_dashboard::preferred_images(data()/"avatar/editor",fs_prefix()+look3d::look_path);
 if(!force&&signature==images.signature)return;
 SDL_Surface* body=images.fullbody.empty()?nullptr:load_png(images.fullbody);
 SDL_Surface* head=images.gamerpic.empty()?nullptr:load_png(images.gamerpic);
 close();fullbody=body;gamerpic=head;signature=images.signature;
}
bool consume(){
 std::string reason;const bool restored=editor_dashboard::consume(data(),model,std::time(nullptr),reason);
 if(!reason.empty())log(reason.c_str());
 refresh(true);return restored;
}
}
