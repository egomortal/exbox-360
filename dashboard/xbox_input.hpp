// Ex-Box 360: the DualSense read as an Xbox 360 controller. Pure, no SDL.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SDL's game-controller layer already names buttons by Xbox position (its A is
// the bottom face button, Cross on a DualSense). This header turns those indices
// and the analog axes into Xbox 360 buttons, and keeps the parts worth testing
// (thresholds, hold-to-repeat, the exit chord) away from SDL and the console.
//
// The PS button and Create cannot be read by a PS5 app: the system takes them.
// The touchpad click is therefore the Guide button. Whichever index the PS5
// SDL port reports it as (touchpad, back or misc1), it lands on Guide.
#pragma once

#include <cstdint>

namespace xb
{
enum Button : std::uint8_t
{
    A,
    B,
    X,
    Y,
    LB,
    RB,
    LT,
    RT,
    LS,
    RS,
    Back,
    Start,
    Guide,
    Up,
    Down,
    Left,
    Right,
    Count,
    None = 0xff,
};

constexpr std::uint32_t bit(Button button)
{
    return button < Count ? (1u << button) : 0u;
}

inline const char *name(Button button)
{
    static const char *names[Count] = {"A",  "B",  "X",    "Y",     "LB",    "RB", "LT",   "RT",   "LS",
                                       "RS", "Back", "Start", "Guide", "Up", "Down", "Left", "Right"};
    return button < Count ? names[button] : "?";
}

// What a DualSense owner presses for each Xbox button.
inline const char *ps5_name(Button button)
{
    static const char *names[Count] = {"Cross",    "Circle",   "Square",     "Triangle",  "L1",
                                       "R1",       "L2",       "R2",         "L3",        "R3",
                                       "Create",   "Options",  "Touchpad",   "D-pad up",  "D-pad down",
                                       "D-pad left", "D-pad right"};
    return button < Count ? names[button] : "?";
}

// SDL_GameControllerButton values, repeated here so this header needs no SDL.
namespace sdl
{
constexpr int kA = 0, kB = 1, kX = 2, kY = 3, kBack = 4, kGuide = 5, kStart = 6, kLeftStick = 7,
              kRightStick = 8, kLeftShoulder = 9, kRightShoulder = 10, kUp = 11, kDown = 12, kLeft = 13,
              kRight = 14, kMisc1 = 15, kTouchpad = 20;
// SDL_GameControllerAxis values.
constexpr int kAxisLeftX = 0, kAxisLeftY = 1, kAxisTriggerLeft = 4, kAxisTriggerRight = 5;
} // namespace sdl

inline Button from_sdl_button(int index)
{
    switch (index)
    {
    case sdl::kA: return A;
    case sdl::kB: return B;
    case sdl::kX: return X;
    case sdl::kY: return Y;
    case sdl::kLeftShoulder: return LB;
    case sdl::kRightShoulder: return RB;
    case sdl::kLeftStick: return LS;
    case sdl::kRightStick: return RS;
    case sdl::kStart: return Start;
    // The touchpad click, under any of the names an SDL mapping gives it, and
    // a Guide button should a pad ever report one.
    case sdl::kGuide:
    case sdl::kBack:
    case sdl::kMisc1:
    case sdl::kTouchpad: return Guide;
    case sdl::kUp: return Up;
    case sdl::kDown: return Down;
    case sdl::kLeft: return Left;
    case sdl::kRight: return Right;
    default: return None;
    }
}

// A desktop keyboard as the pad, for development builds. SDL keycodes are
// passed in as ints; letters are their lower-case ASCII values.
inline Button from_key(int keycode)
{
    constexpr int kReturn = '\r', kEscape = 27, kBackspace = 8, kTab = '\t';
    constexpr int kRightArrow = 0x4000004F, kLeftArrow = 0x40000050, kDownArrow = 0x40000051,
                  kUpArrow = 0x40000052, kPageUp = 0x4000004B, kPageDown = 0x4000004E, kHome = 0x4000004A,
                  kEnd = 0x4000004D;
    switch (keycode)
    {
    case kReturn:
    case ' ': return A;
    case kEscape:
    case kBackspace:
    case 'b': return B;
    case 'x': return X;
    case 'y': return Y;
    case 'q':
    case kPageUp: return LB;
    case 'e':
    case kPageDown: return RB;
    case kHome: return LT;
    case kEnd: return RT;
    case 'g': return Guide;
    case kTab:
    case 'm': return Start;
    case kUpArrow: return Up;
    case kDownArrow: return Down;
    case kLeftArrow: return Left;
    case kRightArrow: return Right;
    default: return None;
    }
}

// An analog input read as a button, with hysteresis so a trigger resting near
// the threshold does not chatter. Values are 0..1.
class Threshold
{
  public:
    constexpr Threshold(float press = 0.55f, float release = 0.35f) : press_(press), release_(release)
    {
    }
    // Returns +1 on press, -1 on release, 0 otherwise.
    int update(float value)
    {
        if (!down_ && value >= press_)
        {
            down_ = true;
            return 1;
        }
        if (down_ && value <= release_)
        {
            down_ = false;
            return -1;
        }
        return 0;
    }
    bool down() const
    {
        return down_;
    }

  private:
    float press_, release_;
    bool down_ = false;
};

// The left stick as a D-pad: the dominant axis past a threshold, with
// hysteresis on the way back. x and y are -1..1, right and down positive.
class StickDpad
{
  public:
    // Returns the direction that is now held (Up/Down/Left/Right) or None.
    Button update(float x, float y)
    {
        const float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        const float big = ax > ay ? ax : ay;
        if (held_ != None)
        {
            const float along = held_ == Left ? -x : held_ == Right ? x : held_ == Up ? -y : y;
            if (along >= kRelease && big - along < 0.25f)
                return held_;
            held_ = None;
        }
        if (big >= kPress)
            held_ = ax > ay ? (x > 0 ? Right : Left) : (y > 0 ? Down : Up);
        return held_;
    }
    Button held() const
    {
        return held_;
    }

  private:
    static constexpr float kPress = 0.6f, kRelease = 0.4f;
    Button held_ = None;
};

// Hold to repeat: one step on press, more after a pause, faster after a few.
// Times are milliseconds. Feed the button that is held (or None) every frame.
class Repeat
{
  public:
    unsigned delay = 380, interval = 120, fast_interval = 60, fast_after = 6;

    // Call on a fresh press.
    void press(Button button, std::uint32_t now)
    {
        button_ = button;
        next_ = now + delay;
        repeats_ = 0;
    }
    void release(Button button)
    {
        if (button == button_)
            button_ = None;
    }
    void clear()
    {
        button_ = None;
    }
    // Returns the button to step again, or None.
    Button poll(std::uint32_t now)
    {
        if (button_ == None || static_cast<std::int32_t>(now - next_) < 0)
            return None;
        ++repeats_;
        next_ = now + (repeats_ < fast_after ? interval : fast_interval);
        return button_;
    }
    Button held() const
    {
        return button_;
    }

  private:
    Button button_ = None;
    std::uint32_t next_ = 0;
    unsigned repeats_ = 0;
};

// Directions and the bumpers repeat while held; the rest fire once.
constexpr bool repeats(Button button)
{
    return button == Up || button == Down || button == Left || button == Right || button == LB || button == RB;
}

// Both sticks clicked together: the always-available way out to PS5 Home.
constexpr bool exit_chord(std::uint32_t held)
{
    return (held & bit(LS)) && (held & bit(RS));
}
} // namespace xb
