#pragma once
#include <string>

// Platform-independent scene request. Coordinates are in the caller's space;
// the source scene and parent transform remain responsible for that space.
namespace avatar_slots {
enum class Who { SignedInUser, OtherProfile, Unresolved };
enum class Image { FullBody, GamerPicture };
struct Bounds { float x=0, y=0, width=0, height=0; };
struct Camera {
    std::string source_id;
    bool verified=false; // An empty camera is unknown, never an invented default.
};
struct AvatarSlot {
    Who who=Who::Unresolved;
    Image image=Image::FullBody;
    std::string pose; // Empty requests the saved still; other ids need an export.
    Bounds size;
    Camera camera;
};
enum class Resolution { SavedFullBody, SavedGamerPicture, ExistingFallback };
inline Resolution resolve(const AvatarSlot& slot, bool body, bool picture) {
    // Owner PNGs cannot stand in for friends/party members. Do not present a
    // still as an unsupported emote; the existing renderer remains the fallback.
    if (slot.who!=Who::SignedInUser || !slot.pose.empty())
        return Resolution::ExistingFallback;
    if (slot.image==Image::FullBody && body) return Resolution::SavedFullBody;
    if (slot.image==Image::GamerPicture && picture) return Resolution::SavedGamerPicture;
    return Resolution::ExistingFallback;
}
} // namespace avatar_slots
