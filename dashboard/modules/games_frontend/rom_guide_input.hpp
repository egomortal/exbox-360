// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../xbox_input.hpp"
#include "rom_game_guide.hpp"
namespace exbox_rom_guide {
inline Button from_xbox(xb::Button b){switch(b){case xb::Up:return Button::Up;case xb::Down:return Button::Down;
 case xb::Left:return Button::Left;case xb::Right:return Button::Right;case xb::A:return Button::A;
 case xb::B:return Button::B;case xb::Guide:return Button::Guide;default:return Button::None;}}
inline Result press(Model&m,xb::Button b){return m.press(from_xbox(b));}
} // namespace exbox_rom_guide
