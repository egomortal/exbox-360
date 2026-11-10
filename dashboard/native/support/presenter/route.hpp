// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>
namespace exbox::presenter {
enum class NativeStart {ready, failed_released, failed_ownership_unknown};
enum class Route {sdl, native, stop};
// A malformed/missing switch preserves the established SDL path.
constexpr bool enabled(std::string_view value) noexcept {return value=="1" || value=="1\n" || value=="1\r\n";}
// Native acquisition must occur BEFORE SDL video initialization, which itself
// opens VideoOut on this platform. Failed completion is not proof of release.
constexpr Route select(bool opt_in,NativeStart result) noexcept {
 if(!opt_in)return Route::sdl;
 switch(result){case NativeStart::ready:return Route::native;
 case NativeStart::failed_released:return Route::sdl;
 case NativeStart::failed_ownership_unknown:return Route::stop;}
 return Route::stop;
}
}
