#include "slot.hpp"
#include <cassert>
using namespace avatar_slots;
int main() {
 AvatarSlot s;
 assert(resolve(s,true,true)==Resolution::ExistingFallback);
 s.who=Who::SignedInUser;
 assert(resolve(s,true,false)==Resolution::SavedFullBody);
 assert(resolve(s,false,true)==Resolution::ExistingFallback);
 s.image=Image::GamerPicture;
 assert(resolve(s,false,true)==Resolution::SavedGamerPicture);
 assert(resolve(s,true,false)==Resolution::ExistingFallback);
 s.pose="wave";
 assert(resolve(s,true,true)==Resolution::ExistingFallback);
 s.pose.clear();s.who=Who::OtherProfile;
 assert(resolve(s,true,true)==Resolution::ExistingFallback);
 assert(!s.camera.verified);
}
