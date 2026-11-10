// Host tests for the Xbox mapping and dashboard model. Build with `make test`.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../dash_model.hpp"
#include "../xbox_input.hpp"
#include "../subtitles.hpp"
#include "../music_metadata.hpp"
#include "../recorder_runtime.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <deque>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

using namespace dash;

static Model with_apps(int n)
{
    Model m;
    m.welcome_visible=false; // Existing library fixtures keep Welcome hidden; T11 tests cover both states.
    std::vector<App> apps;
    for (int i = 0; i < n; ++i)
        apps.push_back({"App " + std::to_string(i), "/data/homebrew/App" + std::to_string(i) + "/eboot.elf", "", ""});
    m.set_apps(apps);
    return m;
}

static bool has_hint(const Model &m, xb::Button b, const std::string &label)
{
    for (const auto &h : m.hints())
        if (h.button == b && h.label == label)
            return true;
    return false;
}
static bool has_button(const Model &m, xb::Button b)
{
    for (const auto &h : m.hints())
        if (h.button == b)
            return true;
    return false;
}

static void dualsense_reads_as_xbox()
{
    using namespace xb;
    CHECK(from_sdl_button(sdl::kA) == A); // Cross
    CHECK(from_sdl_button(sdl::kB) == B); // Circle
    CHECK(from_sdl_button(sdl::kX) == X); // Square
    CHECK(from_sdl_button(sdl::kY) == Y); // Triangle
    CHECK(from_sdl_button(sdl::kLeftShoulder) == LB);
    CHECK(from_sdl_button(sdl::kRightShoulder) == RB);
    CHECK(from_sdl_button(sdl::kLeftStick) == LS);
    CHECK(from_sdl_button(sdl::kRightStick) == RS);
    CHECK(from_sdl_button(sdl::kStart) == Start); // Options
    CHECK(from_sdl_button(sdl::kUp) == Up && from_sdl_button(sdl::kDown) == Down);
    CHECK(from_sdl_button(sdl::kLeft) == Left && from_sdl_button(sdl::kRight) == Right);
    // The touchpad is the Guide button under every index SDL might report.
    for (int index : {sdl::kTouchpad, sdl::kBack, sdl::kMisc1, sdl::kGuide})
        CHECK(from_sdl_button(index) == Guide);
    CHECK(from_sdl_button(16) == None); // paddles: not on a DualSense
    CHECK(from_sdl_button(-1) == None);
    CHECK(std::string(ps5_name(A)) == "Cross" && std::string(ps5_name(Guide)) == "Touchpad");
    CHECK(std::string(ps5_name(LT)) == "L2" && std::string(name(RB)) == "RB");
}

static void keyboard_reads_as_xbox()
{
    using namespace xb;
    CHECK(from_key('\r') == A);
    CHECK(from_key(27) == B && from_key(8) == B && from_key('b') == B);
    CHECK(from_key('g') == Guide && from_key('\t') == Start);
    CHECK(from_key(0x4000004B) == LB && from_key(0x4000004E) == RB); // PageUp / PageDown
    CHECK(from_key(0x40000052) == Up && from_key(0x4000004F) == Right);
    CHECK(from_key('z') == None);
}

static void triggers_have_hysteresis()
{
    xb::Threshold t;
    CHECK(t.update(0.30f) == 0 && !t.down());
    CHECK(t.update(0.60f) == 1 && t.down());
    CHECK(t.update(0.45f) == 0 && t.down()); // between thresholds: still held
    CHECK(t.update(0.70f) == 0);             // no second press
    CHECK(t.update(0.30f) == -1 && !t.down());
}

static void stick_acts_as_dpad()
{
    xb::StickDpad s;
    CHECK(s.update(0.1f, 0.1f) == xb::None);  // deadzone
    CHECK(s.update(0.8f, 0.1f) == xb::Right); // dominant axis
    CHECK(s.update(0.5f, 0.2f) == xb::Right); // held through hysteresis
    CHECK(s.update(0.3f, 0.0f) == xb::None);  // released
    CHECK(s.update(-0.1f, -0.9f) == xb::Up);
    CHECK(s.update(0.05f, 0.95f) == xb::Down);
    CHECK(s.update(-0.7f, 0.65f) == xb::Down); // a near-diagonal keeps the held direction
    CHECK(s.update(0.0f, 0.0f) == xb::None);
    CHECK(s.update(-0.7f, 0.65f) == xb::Left); // from rest, the bigger axis wins
    CHECK(s.update(0.9f, 0.0f) == xb::Right);  // a flick across does not stick on Left
}

static void held_buttons_repeat()
{
    xb::Repeat r;
    r.press(xb::Right, 1000);
    CHECK(r.poll(1000) == xb::None);
    CHECK(r.poll(1379) == xb::None);
    CHECK(r.poll(1380) == xb::Right); // after the delay
    CHECK(r.poll(1450) == xb::None);
    CHECK(r.poll(1500) == xb::Right); // then every interval
    for (std::uint32_t t = 1620; t <= 2100; t += 120)
        r.poll(t);
    // After fast_after repeats the pace picks up.
    const std::uint32_t base = 2160;
    CHECK(r.poll(base) == xb::Right);
    CHECK(r.poll(base + 60) == xb::Right);
    r.release(xb::Left); // a different button does not stop it
    CHECK(r.held() == xb::Right);
    r.release(xb::Right);
    CHECK(r.poll(9000) == xb::None);
    CHECK(xb::repeats(xb::LB) && xb::repeats(xb::Down) && !xb::repeats(xb::A) && !xb::repeats(xb::Guide));
}

static void sticks_together_exit()
{
    CHECK(xb::exit_chord(xb::bit(xb::LS) | xb::bit(xb::RS)));
    CHECK(!xb::exit_chord(xb::bit(xb::LS)));
    CHECK(!xb::exit_chord(xb::bit(xb::Start)));
}

static void starts_on_my_xbox()
{
    Model m;
    CHECK(m.channel == MyXbox && m.current() == 0);
    CHECK(m.current_card().title == "Your Homebrew");
    // NXE showed no B prompt at the hub root.
    CHECK(!has_button(m, xb::B));
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::Guide, "Guide"));
}

static void channels_remember_their_place()
{
    Model m = with_apps(12);
    m.press(xb::Up); // Apps
    CHECK(m.channel == Apps);
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.current() == 2);
    m.press(xb::Up); // Games
    CHECK(m.channel == Games && m.current() == 0);
    m.press(xb::Down); // back to Apps
    CHECK(m.channel == Apps && m.current() == 2);
    m.press(xb::Down);
    m.press(xb::Down); // cannot pass My Xbox
    CHECK(m.channel == MyXbox);
    for (int i = 0; i < 10; ++i)
        m.press(xb::Up);
    CHECK(m.channel == Friends);
}

static void bumpers_and_triggers_page()
{
    Model m = with_apps(12);
    m.press(xb::Up);
    m.press(xb::RB);
    CHECK(m.current() == kPageStep);
    m.press(xb::RB);
    m.press(xb::RB);
    CHECK(m.current() == 11); // clamped to the last
    m.press(xb::LB);
    CHECK(m.current() == 6);
    m.press(xb::LT);
    CHECK(m.current() == 0);
    m.press(xb::RT);
    CHECK(m.current() == 11);
    m.press(xb::Left);
    CHECK(m.current() == 10);
    // NXE's legend lists only the slot's own actions; paging still works.
    CHECK(!has_hint(m, xb::RB, "Page") && has_hint(m, xb::A, "Open"));
}

static void a_opens_and_launches()
{
    Model m = with_apps(3);
    Effect e = m.press(xb::A); // Your Homebrew -> Apps
    CHECK(e.command == Command::None && m.channel == Apps);
    m.press(xb::Right);
    e = m.press(xb::A);
    CHECK(e.command == Command::Launch && e.app == 1);
    CHECK(has_hint(m, xb::A, "Open") && has_hint(m, xb::B, "My Xbox"));
    m.press(xb::B);
    CHECK(m.channel == MyXbox);
    m.press(xb::B); // nothing to go back from at the root
    CHECK(m.channel == MyXbox && !m.guide && !m.details);
}

static void my_xbox_cards_go_somewhere()
{
    // NXE My Xbox: the disc slot (homebrew), then the 9199 channel's slots.
    Model m;
    m.press(xb::Right);
    CHECK(m.current_card().title == kBladesGamercard);
    m.press(xb::Right);
    CHECK(m.current_card().title == "Game Library");
    m.press(xb::A);
    CHECK(m.viewer == Viewer::Library);
    m.press(xb::B);
    m.press(xb::RT); // last: System Settings
    CHECK(m.current_card().title == "System Settings");
    m.press(xb::A);
    // NXE's consoles/SystemScene: Console Settings, Family Settings, Memory ...
    CHECK(!m.guide && m.viewer == Viewer::SystemSettings && m.viewer_count() == 10);
    CHECK(m.system_entries()[1].label == "Family Settings" && m.system_entries()[6].label == "Initial Setup" &&
          m.system_entries()[7].label == "Ex-Box Settings" && m.system_entries()[8].label == "Compatibility & Health" &&
          m.system_entries()[9].label == "Account Management");
    CHECK(m.system_meta("Memory").rfind("Move or delete saved games", 0) == 0);
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::B, "Back"));
    m.press(xb::A); // Console Settings
    CHECK(m.system_page == 1 && m.viewer_item == 0 && m.system_entries()[2].label == "Language and Locale");
    for (int i = 0; i < 6; ++i) m.press(xb::Down);
    m.press(xb::A); // Remote Control: the controller map, over the page
    CHECK(m.controller_map && m.viewer == Viewer::SystemSettings);
    m.controller_map = false;
    m.press(xb::B);
    CHECK(m.system_page == 0 && m.viewer == Viewer::SystemSettings);
    m.press(xb::B);
    CHECK(m.viewer == Viewer::None);
    m.era = 2;
    CHECK(m.system_entries()[1].label == "Kinect Settings");
    m.era = 1;
    m.controller_map = true;
    m.press(xb::A); // lights up, does not leave
    m.press(xb::Guide);
    CHECK(m.controller_map && !m.guide);
    m.press(xb::B);
    CHECK(!m.controller_map);
}

static void friends_and_media_are_honest()
{
    Model m;
    m.press(xb::Up); // Welcome is now between My Xbox and Apps.
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up);
    CHECK(m.channel == Friends && m.count() == 2);
    CHECK(m.current_card().icon == IconFriends);
    m.press(xb::Right);
    CHECK(m.current_card().title == "Messages" && m.current_card().icon == IconMessages);
    m.press(xb::Left);
    m.press(xb::A);
    CHECK(m.details && m.details_text == "Not available on PS5 yet.");
    CHECK(has_hint(m, xb::B, "Back"));
    m.press(xb::B);
    CHECK(!m.details);
    CHECK(m.press(xb::X).command == Command::None); // nothing to refresh here
    m.press(xb::Down);
    CHECK(m.channel == Media && m.count() == 3 && m.current_card().title == "Music");
}

// Open Details (Y) and choose one of its actions.
static Effect via_details(Model &m, InfoAction action)
{
    m.press(xb::Y);
    if (!m.info)
        return {};
    const auto entries = m.info_entries();
    int at = -1;
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].action == action)
            at = int(i);
    if (at < 0)
    {
        m.press(xb::B);
        return {};
    }
    while (m.info_item < at)
        m.press(xb::Down);
    while (m.info_item > at)
        m.press(xb::Up);
    Effect e = m.press(xb::A);
    if (m.info)
        m.press(xb::B);
    return e;
}

static void attach_and_detach()
{
    Model m = with_apps(3);
    m.press(xb::Up); // Apps
    m.press(xb::Right);
    CHECK(has_hint(m, xb::Y, "Details"));
    Effect e = via_details(m, InfoAction::Attach);
    CHECK(e.command == Command::SavePins && m.pins.size() == 1 && !m.info);
    CHECK(m.pins[0] == "/data/homebrew/App1/eboot.elf");
    m.press(xb::Down); // My Xbox: NXE keeps the disc slot and Gamer Card first
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.current_card().kind == Kind::Pinned && m.current_card().title == "App 1");
    CHECK(m.current_is_title());
    e = m.press(xb::A);
    CHECK(e.command == Command::Launch && e.app == 1);
    e = via_details(m, InfoAction::Detach);
    CHECK(e.command == Command::SavePins && m.pins.empty() && !m.info);
    CHECK(m.current() <= m.count() - 1);
}

static void missing_attachments_stay_visible()
{
    Model m;
    m.load_pins({"/data/homebrew/Gone/eboot.elf", "/etc/passwd", "/data/homebrew/Gone/eboot.elf"});
    CHECK(m.pins.size() == 1); // unsafe and duplicate lines dropped
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.current_card().kind == Kind::Missing && m.current_card().title == "Unavailable App");
    CHECK(!m.current_is_title());
    m.press(xb::A);
    CHECK(m.details);
    m.press(xb::B);
    m.press(xb::Y);
    CHECK(m.info && m.info_entries().size() == 1 && m.info_entries()[0].action == InfoAction::Detach);
    CHECK(m.info_card().title == "Unavailable App");
    CHECK(m.press(xb::A).command == Command::SavePins && m.pins.empty() && !m.info);
}

static void attachments_are_capped()
{
    Model m = with_apps(12);
    m.press(xb::Up);
    for (int i = 0; i < 12; ++i)
    {
        via_details(m, InfoAction::Attach);
        m.press(xb::Right);
    }
    CHECK(m.pins.size() == kMaxPins);
}

static int find_entry(const Model &m, const std::string &label)
{
    const auto entries = m.blade_entries(m.guide_blade);
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].label == label)
            return int(i);
    return -1;
}

static int find_action(const Model &m, int blade, GuideAction action, int app = -999)
{
    const auto rows = m.blade_entries(blade);
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].action == action && (app == -999 || rows[i].app == app))
            return int(i);
    return -1;
}

static void guide_opens_on_the_home_blade()
{
    Model m = with_apps(2);
    m.player = "Celestials";
    m.press(xb::Guide);
    CHECK(m.guide && m.guide_blade == BladeHome && m.guide_item[BladeHome] == 0);
    CHECK(m.blade_title(BladeHome) == "Celestials");
    CHECK(m.blade_title(BladeApps) == "Marketplace" && m.blade_title(BladeSettings) == "Settings");
    // GuideMain authors A/B/Y; no signed-in tab overrides them.
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::B, "Back") && has_hint(m, xb::Y, "Xbox Dashboard"));
    CHECK(!has_button(m, xb::X));
    m.press(xb::Up); // stops at the top, as the Guide list did
    CHECK(m.guide_item[BladeHome] == 0);
    m.press(xb::RB);m.press(xb::RB); // Settings: authored shutdown row
    for (int i = 0,stop=find_entry(m,"Turn Off Console"); i < stop; ++i)m.press(xb::Down);
    CHECK(m.guide_item[BladeSettings] == find_entry(m, "Turn Off Console"));
    Effect e = m.press(xb::A);
    CHECK(e.command == Command::ExitToPs5 && !m.guide);
}

static void bumpers_flip_blades()
{
    Model m = with_apps(3);
    m.press(xb::Start); // Options opens the ordinary Guide too
    m.press(xb::RB);
    CHECK(m.guide_blade == BladeMedia);
    m.press(xb::RB);
    m.press(xb::RB); // stops at the last blade
    CHECK(m.guide_blade == BladeSettings);
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::ChangeGamerpic);
    CHECK(m.guide_item[BladeSettings] >= 0);
    m.press(xb::LB);
    m.press(xb::Left); // the D-pad flips blades too
    CHECK(m.guide_blade == BladeHome);
    m.press(xb::RB);
    m.press(xb::RB);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Change Gamerpic");
    CHECK(m.press(xb::A).command == Command::NextGamerpic);
    CHECK(m.guide && m.guide_blade == BladeSettings); // stays open to show the new picture
    CHECK(m.press(xb::A).command == Command::NextGamerpic);
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::ToggleSounds);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Sounds: On");
    CHECK(m.press(xb::A).command == Command::SaveSettings && !m.sounds && m.guide);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Sounds: Off");
    m.press(xb::A);
    CHECK(m.sounds);
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::CycleEra);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Dashboard: NXE >");
    CHECK(m.press(xb::A).command==Command::None && !m.guide && m.viewer==Viewer::DashboardEras);
    CHECK(m.viewer_item==1 && m.viewer_count()==5);
    m.press(xb::B);
    CHECK(m.guide && m.guide_blade==BladeSettings);
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::CycleButtonStyle);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Button Style: Glossy");
    for (int i = 1; i <= 3; ++i)
    {
        CHECK(m.press(xb::A).command == Command::SaveSettings && m.guide);
        CHECK(m.button_style == i % 3);
        constexpr const char *styles[]={"Glossy", "Retail Mix", "Simple"};
        CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == std::string("Button Style: ") + styles[i % 3]);
    }
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Button Style: Glossy");
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::ToggleDimming);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Screen Dimming: On");
    CHECK(m.press(xb::A).command == Command::SaveSettings && !m.dimming && m.guide);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Screen Dimming: Off");
    m.press(xb::A);
    m.guide_item[BladeSettings] = find_action(m, BladeSettings, GuideAction::Refresh);
    CHECK(m.press(xb::A).command == Command::Refresh && !m.guide);
    m.press(xb::Guide); // reopening starts over on Home
    CHECK(m.guide_blade == BladeHome && m.guide_item[BladeSettings] == 0);
    for (int i = 0; i < 6; ++i)
        m.press(xb::LB);
    CHECK(m.guide_blade == BladeApps);
    m.press(xb::Guide); // the same button closes it
    CHECK(!m.guide && m.channel == MyXbox);
}

static void blades_launch_and_navigate()
{
    Model m = with_apps(3);
    m.press(xb::Guide);m.guide_blade=BladeApps;
    const auto apps_blade = m.host_blade_entries(BladeApps);
    CHECK(apps_blade.size() == 4 && apps_blade[0].label == "Apps Channel" && apps_blade[3].label == "App 2");
    m.guide_item[BladeApps]=find_action(m,BladeApps,GuideAction::Launch,1);
    Effect e = m.press(xb::A);
    CHECK(e.command == Command::Launch && e.app == 1 && !m.guide);

    // Homebrew listed as a game moves from the Apps blade to the Games blade.
    m.load_categories({"/data/homebrew/App2/eboot.elf", "/etc/x"});
    const auto games = m.host_blade_entries(BladeGames);
    CHECK(games.size() == 5 && games[1].label == "App 2" && games[1].app == 2); // refresh + the PS5's two game apps
    CHECK(m.host_blade_entries(BladeApps).size() == 3);

    m.press(xb::Guide);m.guide_blade=BladeMedia;
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::Music);
    CHECK(m.press(xb::A).command==Command::GuideMusicRefresh);
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::GuideMusicOpenFull);
    e = m.press(xb::A); // Open Full Music
    CHECK(!m.guide && m.viewer == Viewer::Music && e.command == Command::ScanMusic);
    m.press(xb::B);

    m.press(xb::Guide);
    m.guide_item[BladeHome]=find_action(m,BladeHome,GuideAction::Games);
    m.press(xb::A); // Home blade: Games
    CHECK(m.channel == Games);
    m.press(xb::Guide);
    m.press(xb::Y); // Xbox Dashboard
    CHECK(!m.guide && m.channel == MyXbox);

    Model empty;
    const auto none = empty.host_blade_entries(BladeApps);
    CHECK(none.size() == 2 && none[1].action == GuideAction::Refresh);
}

static void details_hand_over_to_guide()
{
    Model m;
    m.press(xb::Up); // Welcome is now between My Xbox and Apps.
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::A);
    CHECK(m.details);
    m.press(xb::Guide);
    CHECK(!m.details && m.guide);
}

static void empty_app_channel()
{
    Model m;
    m.press(xb::Up); // Welcome is now between My Xbox and Apps.
    m.press(xb::Up);
    CHECK(m.channel == Apps && m.count() == 1 && m.current_card().kind == Kind::Empty);
    CHECK(!m.current_is_title());
    CHECK(m.press(xb::A).command == Command::None && m.details);
    m.press(xb::B);
    CHECK(m.press(xb::Y).command == Command::None);
    CHECK(m.press(xb::X).command == Command::Refresh);
}

static void refresh_keeps_selection_in_range()
{
    Model m = with_apps(8);
    m.press(xb::Up);
    m.press(xb::RT);
    CHECK(m.current() == 7);
    m.set_apps({{"Only", "/data/homebrew/Only/eboot.elf", "", ""}});
    CHECK(m.current() == 0 && m.current_card().title == "Only");
    m.set_apps({});
    CHECK(m.current() == 0 && m.current_card().kind == Kind::Empty);
}


static std::vector<App> some_games(int n)
{
    std::vector<App> out;
    static const char *ids[] = {"PPSA01949", "CUSA12345", "PPSA04607", "PPSA00001"};
    for (int i = 0; i < n; ++i)
        out.push_back({"Game " + std::to_string(i), "", ids[i % 4], "/user/appmeta/" + std::string(ids[i % 4]) + "/icon0.png"});
    return out;
}

static void console_games_fill_the_games_channel()
{
    Model m = with_apps(3);
    m.set_games(some_games(2));
    m.press(xb::Up);
    m.press(xb::Up); // Games
    CHECK(m.channel == Games && m.count() == 2);
    const Card c = m.current_card();
    CHECK(c.kind == Kind::Game && c.title == "Game 0" && c.image == "/user/appmeta/PPSA01949/icon0.png");
    CHECK(m.current_is_title());
    CHECK(has_hint(m, xb::A, "Play") && has_hint(m, xb::Y, "Details"));
    m.press(xb::Right);
    Effect e = m.press(xb::A);
    CHECK(e.command == Command::LaunchTitle && e.app == 1);
    // Apps still lists homebrew.
    m.press(xb::Down);
    CHECK(m.channel == Apps && m.current_card().kind == Kind::App && m.current_card().image.empty());
}

static void games_attach_to_my_xbox()
{
    Model m = with_apps(2);
    m.set_games(some_games(2));
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Y);
    CHECK(m.info && m.info_card().title == "Game 0" && m.info_entries()[0].label == "Play");
    CHECK(m.info_entries().size() == 7); // trailer and cover actions, no channel choice
    m.press(xb::B);
    CHECK(via_details(m, InfoAction::Attach).command == Command::SavePins);
    CHECK(m.pins.size() == 1 && m.pins[0] == "title:PPSA01949");
    m.press(xb::Down);
    m.press(xb::Down); // My Xbox
    m.press(xb::Right);
    m.press(xb::Right); // past the Gamer Card
    Card c = m.current_card();
    CHECK(c.kind == Kind::Pinned && c.game == 0 && c.title == "Game 0");
    CHECK(has_hint(m, xb::A, "Play"));
    CHECK(m.press(xb::A).command == Command::LaunchTitle);
    // A title that is no longer installed stays visible, and says so.
    m.set_games({});
    c = m.current_card();
    CHECK(c.kind == Kind::Missing && c.title == "Unavailable Game");
    m.load_pins({"title:PPSA01949", "title:nope", "title:PPSA0194", "/data/homebrew/App0/eboot.elf"});
    CHECK(m.pins.size() == 2); // bad title ids are dropped
}

static void game_artwork_source_can_be_selected()
{
    Model m = with_apps(0);
    auto games = some_games(1);
    games[0].playstation_icon = games[0].icon;
    games[0].xbox_cover = "/data/homebrew/ExBox360/game_art/PPSA01949.jpg";
    games[0].icon = games[0].xbox_cover;
    m.set_games(games);
    m.press(xb::Up);
    m.press(xb::Up);
    CHECK(m.current_card().image == games[0].xbox_cover);
    CHECK(via_details(m, InfoAction::DownloadCover).command == Command::RequestCover);
    CHECK(via_details(m, InfoAction::PlayTrailer).command == Command::PlayTrailer);
    CHECK(via_details(m, InfoAction::RefreshMedia).command == Command::RefreshMedia);
    CHECK(via_details(m, InfoAction::CycleArtwork).command == Command::SaveArtwork);
    CHECK(m.current_card().image == games[0].playstation_icon);
    CHECK(via_details(m, InfoAction::CycleArtwork).command == Command::SaveArtwork);
    CHECK(m.current_card().image == games[0].xbox_cover);
    CHECK(via_details(m, InfoAction::CycleArtwork).command == Command::SaveArtwork);
    CHECK(m.artwork_choice.empty());
    games[0].xbox_cover.clear();
    games[0].icon = games[0].playstation_icon;
    m.set_games(games);
    CHECK(via_details(m, InfoAction::CycleArtwork).command == Command::None);
    CHECK(m.current_card().image == games[0].playstation_icon);
    CHECK(via_details(m, InfoAction::GameUpdates).command == Command::ExitToPs5);
}

static void guide_games_blade_lists_console_games()
{
    Model m = with_apps(2);
    m.set_games(some_games(3));
    m.press(xb::Guide);m.guide_blade=BladeGames;
    const auto entries = m.host_blade_entries(BladeGames);
    CHECK(entries.size() == 7 && entries[3].action == GuideAction::LaunchTitle && entries[3].app == 2);
    CHECK(entries[4].action == GuideAction::Refresh);
    CHECK(entries[5].label == "Game Library" && entries[5].action == GuideAction::LaunchSystem && entries[5].app == 0);
    CHECK(entries[6].label == "PlayStation Store" && entries[6].app == 1);
    m.guide_item[BladeGames]=find_action(m,BladeGames,GuideAction::LaunchTitle,0);
    Effect e = m.press(xb::A);
    CHECK(e.command == Command::LaunchTitle && e.app == 0 && !m.guide);
}

static Model picture_library()
{
    Model m = with_apps(1);
    m.perform_guide_entry({"Pictures", GuideAction::Pictures});
    const std::string root = "/fs/user/av_contents/photo/";
    std::vector<PictureFile> found;
    for (const char *path : {"PPSA01949/PPSA01949/1/20261018_101010_00000001.jpg", "PPSA04321/PPSA04321/1/20261025_071500_00000005.jpg",
                             "PPSA01949/PPSA01949/1/20261020_213045_00000003.jpg", "NPXS40087/NPXS40087/1/20261001_120000_00000007.png",
                             "odd.png"})
    {
        PictureFile f = picture_file(root + path);
        f.game = f.title_id == "PPSA01949" ? "astro Bot" : f.title_id == "PPSA04321" ? "Gran Turismo 7" : "";
        found.push_back(f);
    }
    m.set_pictures(found);
    return m;
}

static std::vector<std::string> picture_names(const Model &m)
{
    std::vector<std::string> out;
    for (int i : m.picture_list())
        out.push_back(m.picture_files[size_t(i)].taken.substr(0, 8));
    return out;
}

static void pictures_read_the_ps5_path()
{
    const PictureFile f = picture_file("/user/av_contents/photo/PPSA01949/PPSA01949/1/20261017_200850_00357409.jpg");
    CHECK(f.title_id == "PPSA01949" && f.taken == "20261017_200850" && f.label() == "PPSA01949");
    CHECK(picture_date(f.taken) == "2026-10-17  20:08");
    const PictureFile g = picture_file("/user/av_contents/photo/shot.png");
    CHECK(g.title_id.empty() && g.taken == "shot.png" && g.label() == "Other" && picture_date(g.taken) == "shot.png");
}

static void pictures_sort_and_group_by_game()
{
    Model m = picture_library();
    CHECK(m.viewer == Viewer::Pictures && m.viewer_count() == 5 && m.pictures == 5);
    CHECK((picture_names(m) == std::vector<std::string>{"20261025", "20261020", "20261018", "20261001", "odd.png"}));
    CHECK(has_hint(m, xb::X, "Sort") && has_hint(m, xb::RS, "Refresh") && has_hint(m, xb::A, "Select"));
    CHECK(m.press(xb::RS).command == Command::ScanPictures);
    // X: Oldest, keeping the same picture selected.
    m.press(xb::Right); // 20261020
    Effect e = m.press(xb::X);
    CHECK(e.command == Command::SaveSettings && m.picture_order == 1);
    CHECK((picture_names(m) == std::vector<std::string>{"odd.png", "20261001", "20261018", "20261020", "20261025"}));
    CHECK(m.picture()->taken == "20261020_213045" && m.viewer_item == 3);
    // Name: by game A-Z (case folded), newest first within one.
    m.press(xb::X);
    CHECK(m.picture_order == 2);
    CHECK((picture_names(m) == std::vector<std::string>{"20261020", "20261018", "20261025", "20261001", "odd.png"}));
    CHECK(m.picture()->taken == "20261020_213045" && m.viewer_item == 0);
    // Games: one tile per game, on this picture's game; A opens it, B comes back.
    Model before = m;
    m.press(xb::X);
    CHECK(m.picture_order == kPictureGames && m.picture_groups() && m.viewer_count() == 4);
    CHECK((m.picture_games() == std::vector<std::string>{"astro Bot", "Gran Turismo 7", "NPXS40087", "Other"}));
    CHECK(m.viewer_item == 0 && has_hint(m, xb::A, "Select") && m.picture()->taken == "20261020_213045");
    m.press(xb::Right);
    CHECK(m.picture()->game == "Gran Turismo 7");
    m.press(xb::Left);
    before = m;
    e = m.press(xb::A);
    CHECK(m.picture_group == 0 && m.viewer_count() == 2 && m.viewer_item == 0 && !m.viewer_full);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Select);
    CHECK((picture_names(m) == std::vector<std::string>{"20261020", "20261018"}));
    m.press(xb::Right);
    CHECK(m.picture()->taken == "20261018_101010");
    m.press(xb::A);
    CHECK(m.viewer_full);
    m.press(xb::RB); // stays inside the game
    CHECK(m.viewer_item == 1);
    m.press(xb::B);
    before = m;
    e = m.press(xb::B);
    CHECK(m.picture_group == -1 && m.viewer_item == 0 && m.viewer == Viewer::Pictures);
    CHECK(sound_for(before, m, xb::B, e) == Sound::Back);
    // A rescan keeps the place; a game that is gone closes.
    m.press(xb::A);
    m.press(xb::Right);
    std::vector<PictureFile> more = m.picture_files;
    more.push_back(picture_file("/fs/user/av_contents/photo/PPSA01949/PPSA01949/1/20261026_000000_00000010.jpg"));
    more.back().game = "astro Bot";
    m.set_pictures(more);
    CHECK(m.picture_group == 0 && m.viewer_count() == 3 && m.picture()->taken == "20261018_101010" && m.viewer_item == 2);
    m.set_pictures({more[1]});
    CHECK(m.picture_group == -1 && m.picture_groups() && m.viewer_count() == 1);
    // From Games, X comes round to Newest; opening the viewer again keeps the sort.
    m.press(xb::X);
    CHECK(m.picture_order == 0 && !m.picture_groups());
    m.picture_order = 3;
    m.picture_group = 0;
    m.press(xb::B);
    m.press(xb::B);
    CHECK(m.viewer == Viewer::None);
    m.perform_guide_entry({"Pictures", GuideAction::Pictures});
    CHECK(m.picture_order == 3 && m.picture_group == -1);
}

static void picture_full_screen_zoom_pan_slideshow()
{
    Model m = picture_library();
    m.press(xb::A);
    CHECK(m.viewer_full && m.picture_zoom == 0 && !m.picture_slideshow && !m.picture_info);
    CHECK(has_hint(m, xb::A, "Play") && has_hint(m, xb::Up, "Previous / Next") && has_hint(m, xb::RT, "Zoom") &&
          has_hint(m, xb::RS, "Info") && has_hint(m, xb::B, "Back"));
    // Left / Right step while the whole picture shows.
    m.press(xb::Right);
    CHECK(m.viewer_item == 1);
    m.press(xb::Left);
    // RT zooms in, then the D-pad (or stick) pans instead of stepping.
    Model before = m;
    m.press(xb::RT);
    CHECK(m.picture_zoom == 1 && sound_for(before, m, xb::RT, Effect{}) == Sound::Focus);
    CHECK(has_hint(m, xb::Up, "Pan") && has_hint(m, xb::LB, "Previous / Next"));
    m.press(xb::RT);
    CHECK(m.picture_zoom == 2);
    before = m;
    m.press(xb::Right);
    CHECK(m.viewer_item == 0 && m.picture_pan_x == 200 && sound_for(before, m, xb::Right, Effect{}) == Sound::None);
    m.press(xb::Up);
    CHECK(m.picture_pan_y == -200);
    for (int i = 0; i < 9; ++i)
        m.press(xb::Right);
    CHECK(m.picture_pan_x == kPicturePan);
    for (int i = 0; i < 9; ++i)
        m.press(xb::RT);
    CHECK(m.picture_zoom == kPictureZoomCount - 1);
    for (int i = 0; i < 9; ++i)
        m.press(xb::LT);
    CHECK(m.picture_zoom == 0 && m.picture_pan_x == 0 && m.picture_pan_y == 0);
    // LB / RB step at any zoom, and each picture opens whole.
    m.press(xb::RT);
    m.press(xb::RB);
    CHECK(m.viewer_item == 1 && m.picture_zoom == 0);
    // RS: the info panel stays up from picture to picture.
    m.press(xb::RS);
    CHECK(m.picture_info && has_hint(m, xb::RS, "Hide Info"));
    m.press(xb::Right);
    CHECK(m.picture_info);
    // X (or A) runs the slideshow; it goes round from the last to the first.
    before = m;
    m.press(xb::X);
    CHECK(m.picture_slideshow && has_hint(m, xb::A, "Pause") && sound_for(before, m, xb::X, Effect{}) == Sound::Select);
    CHECK(m.slideshow_step() && m.viewer_item == 3);
    CHECK(m.slideshow_step() && m.slideshow_step() && m.viewer_item == 0);
    m.press(xb::RT); // zooming stops the show
    CHECK(!m.picture_slideshow && !m.slideshow_step());
    m.press(xb::A);
    CHECK(m.picture_slideshow && m.picture_zoom == 0);
    m.press(xb::Guide);
    CHECK(!m.slideshow_step()); // not behind the Guide
    m.press(xb::B);
    m.press(xb::A);
    CHECK(!m.picture_slideshow);
    // B leaves full screen and puts everything back.
    m.press(xb::X);
    m.press(xb::RS);
    m.press(xb::B);
    CHECK(!m.viewer_full && !m.picture_slideshow && !m.picture_info && !m.slideshow_step());
    // One picture: no slideshow to run.
    m.set_pictures({m.picture_files[0]});
    m.press(xb::A);
    CHECK(!has_button(m, xb::A) && (m.press(xb::X), !m.picture_slideshow));
}

static void picture_transport_rotation_and_order()
{
    Model m=picture_library();m.press(xb::A);
    CHECK(m.picture_control==-1 && m.picture_rotation==0 && m.picture_repeat && !m.picture_shuffle);
    m.press(xb::Down);CHECK(m.picture_control==0 && has_hint(m,xb::A,"Select"));
    m.press(xb::A);CHECK(m.picture_slideshow && std::string(m.picture_control_help())=="Pause");
    m.press(xb::A);CHECK(!m.picture_slideshow && std::string(m.picture_control_help())=="Play");
    m.press(xb::Left);CHECK(m.picture_control==7);
    CHECK(std::string(m.picture_control_help())=="Rotate Image Counter-Clockwise");
    const auto files=m.picture_files;
    m.press(xb::A);CHECK(m.picture_rotation==3);
    m.press(xb::Left);CHECK(m.picture_control==6);
    for (int i=0;i<5;++i) m.press(xb::A);
    CHECK(m.picture_rotation==0 && !m.picture_slideshow);
    m.press(xb::A);CHECK(m.picture_rotation==1);
    m.press(xb::Up);m.press(xb::RT);m.press(xb::Right);
    CHECK(m.picture_rotation==1 && m.picture_zoom==1 && m.picture_pan_x>0);
    m.press(xb::RB);CHECK(m.picture_rotation==0 && m.picture_zoom==0 && m.viewer_item==1);
    for (size_t i=0;i<files.size();++i) CHECK(files[i].path==m.picture_files[i].path);
    m.press(xb::Down);
    for (int i=0;i<4;++i) m.press(xb::Right);
    CHECK(m.picture_control==4 && std::string(m.picture_control_help())=="Turn Slideshow Shuffle On");
    CHECK(m.press(xb::A).command==Command::SaveSettings && m.picture_shuffle);
    CHECK(std::string(m.picture_control_help())=="Turn Slideshow Shuffle Off");
    m.press(xb::Right);
    CHECK(m.press(xb::A).command==Command::SaveSettings && !m.picture_repeat);
    CHECK(std::string(m.picture_control_help())=="Turn Slideshow Repeat On");
    m.press(xb::Up);m.press(xb::X);
    const int n=m.viewer_count();std::set<int> seen{m.viewer_item};
    for (int i=1;i<n;++i) {CHECK(m.slideshow_step());CHECK(seen.insert(m.viewer_item).second);}
    CHECK(int(seen.size())==n && !m.slideshow_step() && !m.picture_slideshow);
    m.press(xb::X);CHECK(m.picture_slideshow && m.slideshow_step()); // restart an exhausted shuffle
    m.picture_repeat=true;
    m.picture_visited.clear();
    seen={m.viewer_item};
    for (int i=1;i<n;++i) {CHECK(m.slideshow_step());CHECK(seen.insert(m.viewer_item).second);}
    seen.clear();
    for (int i=0;i<n;++i) {CHECK(m.slideshow_step());CHECK(seen.insert(m.viewer_item).second);}
    CHECK(int(seen.size())==n);
    m.details=true;CHECK(!m.slideshow_step());m.details=false;
    for (int i=0;i<3*n;++i) {const int previous=m.viewer_item;CHECK(m.slideshow_step() && m.viewer_item!=previous);}
    m.press(xb::Down);m.press(xb::Right);m.press(xb::Right); // Stop
    m.press(xb::A);CHECK(!m.picture_slideshow && m.picture_visited.empty());
    m.press(xb::Right);m.press(xb::A);CHECK(m.picture_rotation==0); // Next
    m.press(xb::Left);m.press(xb::Left);m.press(xb::A); // Previous
    m.press(xb::B);CHECK(m.viewer_full && m.picture_control==-1);
    m.press(xb::B);CHECK(!m.viewer_full && m.picture_rotation==0);
    // Sequential repeat off holds the final picture; Play starts again at the first.
    m.picture_shuffle=false;m.picture_repeat=false;m.press(xb::A);m.viewer_item=0;m.press(xb::A);
    for (int i=1;i<n;++i) CHECK(m.slideshow_step() && m.viewer_item==i);
    CHECK(!m.slideshow_step() && !m.picture_slideshow && m.viewer_item==n-1);
    m.press(xb::A);CHECK(m.viewer_item==0 && m.picture_slideshow);
    m.set_pictures({});CHECK(!m.viewer_full && !m.slideshow_step() && m.picture_visited.empty());
}

static void picture_background_confirmation_and_settings()
{
    for (int era=0;era<4;++era) {
        Model m=picture_library();m.era=era;m.press(xb::A);
        const bool supported=era==1 || era==2;
        CHECK(m.picture_background_available()==supported);
        m.press(xb::Y);
        if (!supported) {CHECK(m.details && !m.picture_background_confirm && m.picture_background==0);continue;}
        CHECK(m.picture_background_confirm && !m.picture_background_yes && !m.picture_background_pending.empty());
        const int item=m.viewer_item;m.press(xb::RB);CHECK(m.viewer_item==item);
        CHECK(m.press(xb::A).command==Command::None && !m.picture_background_confirm && m.picture_background_pending.empty());
        m.press(xb::X);m.press(xb::Y);CHECK(!m.slideshow_step());
        m.press(xb::Left);m.press(xb::B);
        CHECK(!m.picture_background_confirm && m.picture_background_pending.empty() && m.picture_background==0);
        m.press(xb::Y);m.press(xb::Up);
        const std::string path=m.picture()->path;
        CHECK(m.press(xb::A).command==Command::ApplyPictureBackground);
        CHECK(!m.picture_background_confirm && m.picture_background_pending==path && m.picture_background==0);
        // Runtime commits the slot only after a successful copy; then settings round-trip.
        m.picture_background=2;m.picture_repeat=false;m.picture_shuffle=true;m.picture_order=1;
        Model restored;
        for (const auto &line:m.saved_picture_settings()) restored.load_picture_setting(line+"\r\n");
        CHECK(restored.picture_background==2 && !restored.picture_repeat && restored.picture_shuffle && restored.picture_order==1);
        CHECK(restored.picture_rotation==0 && restored.picture_control==-1 && !restored.picture_background_confirm);
        for (const char *line:{"picture_background=-1","picture_background=3","picture_background=/capture.jpg",
                              "picture_background=1garbage","picture_repeat=2","picture_shuffle=9","picture_order=99"})
            restored.load_picture_setting(line);
        CHECK(restored.picture_background==2 && !restored.picture_repeat && restored.picture_shuffle && restored.picture_order==1);
        CHECK(m.perform_guide_entry({"Change Theme",GuideAction::SelectTheme}).command==Command::None);
        CHECK(m.viewer==Viewer::Themes && m.picture_background==2);
        m.press(xb::Up);
        {const auto effect=m.press(xb::A);CHECK(effect.command==Command::ApplyInstalledTheme && effect.app==-1 && !m.picture_background_enabled && m.picture_background==2);}
        m.press(xb::B);
        restored.era=3;CHECK(!restored.picture_background_available() && restored.picture_background==2);
        restored.era=1;CHECK(restored.picture_background_available() && restored.picture_background==2);
        restored.load_picture_setting("picture_background=0");CHECK(restored.picture_background==0);
        m.press(xb::Y);m.set_pictures({});CHECK(!m.picture_background_confirm && m.picture_background_pending.empty());
    }
    Model defaults;CHECK(defaults.picture_background==0 && defaults.picture_repeat && !defaults.picture_shuffle);
    Model grid=picture_library();grid.era=1;
    CHECK(has_hint(grid,xb::Y,"Apply as Background") && has_hint(grid,xb::RS,"Refresh"));
    grid.press(xb::Y);CHECK(grid.picture_background_confirm && !grid.viewer_full);
    grid.press(xb::Right);grid.press(xb::A);CHECK(!grid.picture_background_confirm && !grid.viewer_full);
    grid.press(xb::Y);grid.press(xb::Left);
    CHECK(grid.press(xb::A).command==Command::ApplyPictureBackground && !grid.viewer_full);
    grid.picture_order=kPictureGames;grid.picture_group=-1;
    CHECK(!has_hint(grid,xb::Y,"Apply as Background"));
    grid.press(xb::Y);CHECK(!grid.picture_background_confirm);
}

static void media_viewer_pictures_and_videos()
{
    Model m = with_apps(1);
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up); // Media
    CHECK(m.channel == Media);
    m.press(xb::Right); // Pictures
    Model before = m;
    Effect e = m.press(xb::A);
    CHECK(e.command == Command::ScanPictures && m.viewer == Viewer::Pictures && m.viewer_item == 0);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Unfold);
    // Nothing found: A does nothing, RS refreshes the capture scan.
    CHECK(m.press(xb::A).command == Command::None && !m.viewer_full);
    CHECK(m.press(xb::RS).command == Command::ScanPictures);
    CHECK(has_hint(m, xb::B, "Back") && has_hint(m, xb::RS, "Refresh") && has_hint(m, xb::X, "Sort") && !has_button(m, xb::A));
    // Fourteen found: a 4-wide grid.
    m.pictures = 14;
    m.press(xb::Down);
    CHECK(m.viewer_item == 4);
    m.press(xb::Down);
    m.press(xb::Down); // 12, then the short last row
    CHECK(m.viewer_item == 12);
    m.press(xb::Down);
    CHECK(m.viewer_item == 12);
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.viewer_item == 13); // stops at the last
    m.press(xb::LT);
    CHECK(m.viewer_item == 0);
    m.press(xb::RB);
    CHECK(m.viewer_item == 12);
    m.press(xb::LB);
    m.press(xb::Up);
    CHECK(m.viewer_item == 0);
    before = m;
    e = m.press(xb::A);
    CHECK(m.viewer_full && sound_for(before, m, xb::A, e) == Sound::Select);
    CHECK(has_hint(m, xb::Up, "Previous / Next"));
    m.press(xb::Right);
    m.press(xb::RB);
    CHECK(m.viewer_item == 2 && m.viewer_full);
    m.press(xb::B);
    CHECK(!m.viewer_full && m.viewer == Viewer::Pictures);
    // The Guide opens over the viewer and B comes back to it.
    m.press(xb::Guide);
    CHECK(m.guide);
    m.press(xb::B);
    CHECK(!m.guide && m.viewer == Viewer::Pictures);
    before = m;
    e = m.press(xb::B);
    CHECK(m.viewer == Viewer::None && sound_for(before, m, xb::B, e) == Sound::Fold);
    // Game Videos: every trailer on the console.
    m.press(xb::Right); // Videos
    m.trailers = {"halo 3", "gears of war", "crackdown"};
    m.press(xb::A);
    CHECK(m.viewer == Viewer::Videos && m.viewer_count() == 3 && has_hint(m, xb::A, "Select"));
    // Going to a channel from the Guide closes the viewer.
    m.press(xb::Guide);
    m.press(xb::A); // My Xbox on the Home blade
    CHECK(m.viewer == Viewer::None && m.channel == MyXbox);
    // The ordinary Media blade retains the media and PS5 app shortcuts.
    m.press(xb::Guide);m.guide_blade=BladeMedia;
    const auto media = m.blade_entries(BladeMedia);
    CHECK(find_action(m,BladeMedia,GuideAction::Pictures)>=0 && find_action(m,BladeMedia,GuideAction::Videos)>=0);
    CHECK(find_action(m,BladeMedia,GuideAction::LaunchSystem,2)>=0);
    CHECK(find_action(m,BladeMedia,GuideAction::LaunchSystem,3)>=0);
    CHECK(find_action(m,BladeMedia,GuideAction::RecordScreen)>=0);
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::Videos);
    CHECK(m.press(xb::A).command == Command::None && m.viewer == Viewer::Videos && !m.guide);
    m.press(xb::B);
    m.press(xb::Guide);m.guide_blade=BladeMedia;
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::RecordScreen);
    e = m.press(xb::A);
    CHECK(e.command == Command::None && !m.guide && m.details && m.details_title == "Record Screen");
    CHECK(m.details_text.find("Create button") != std::string::npos && m.details_text.find("4K recording") != std::string::npos);
    m.press(xb::B);
    m.press(xb::Guide);m.guide_blade=BladeMedia;
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::LaunchSystem,2);
    e = m.press(xb::A);
    CHECK(e.command == Command::LaunchSystem && e.app == 2 && std::string(kSystemApps[e.app].title_id) == "NPXS40075");

    // Every era's ordinary Media blade retains recording as a fallback.
    for (int era=0;era<4;++era) {
        Model guide;guide.era=era;guide.dashboard_build=era==0?6770:era==1?9199:era==2?13604:17559;
        const auto rows=guide.blade_entries(BladeMedia);
        CHECK(std::any_of(rows.begin(),rows.end(),[](const GuideEntry&row){return row.action==GuideAction::RecordScreen;}));
    }
}

static void theme_picker_navigation_and_migration()
{
    for (int era=0;era<4;++era) {
        Model m;m.era=era;m.dashboard_build=era==0?6770:era==1?9199:era==2?13604:17559;
        const auto settings_rows=m.blade_entries(BladeSettings);
        CHECK(std::any_of(settings_rows.begin(),settings_rows.end(),[](const GuideEntry &entry){return entry.action==GuideAction::SelectTheme;}));
        CHECK(m.theme_entries()==std::vector<std::string>{"Xbox 360"});
        m.picture_background=2;
        const bool pictures=era==1 || era==2;
        CHECK(m.theme_entries().size()==std::size_t(pictures?2:1));
        m.viewer=Viewer::Profile;m.viewer_item=2;
        Model before=m;
        const Effect opened=m.press(xb::A);
        CHECK(opened.command==Command::None && m.viewer==Viewer::Themes);
        CHECK(sound_for(before,m,xb::A,opened)==Sound::Select);
        CHECK(m.viewer_item==(pictures?1:0) && has_hint(m,xb::A,"Select") && has_hint(m,xb::B,"Back"));
        m.press(xb::Down);CHECK(m.viewer_item==0); // wraps at the last theme
        m.press(xb::Up);CHECK(m.viewer_item==(pictures?1:0));
        m.press(xb::Down);CHECK(m.viewer_item==0);
        // Back without A cancels; merely moving focus never changes the theme.
        m.press(xb::B);CHECK(m.viewer==Viewer::Profile && m.viewer_item==2 && m.picture_background_enabled);
        m.press(xb::A);m.press(xb::Up);
        {const auto effect=m.press(xb::A);CHECK(effect.command==Command::ApplyInstalledTheme && effect.app==-1);}
        CHECK(m.theme==0 && !m.picture_background_enabled && m.picture_background==2);
        CHECK(m.viewer==Viewer::Themes);
        m.press(xb::Down);
        {const auto effect=m.press(xb::A);CHECK(effect.command==Command::ApplyInstalledTheme && effect.app==(pictures?-2:-1) && m.picture_background_enabled==pictures);}
        Model restored;
        for (const auto &line:m.saved_picture_settings()) restored.load_picture_setting(line);
        CHECK(restored.picture_background==2 && restored.picture_background_enabled==pictures);
        restored.load_picture_setting("picture_background_enabled=2");
        restored.load_picture_setting("picture_background_enabled=-1");
        CHECK(restored.picture_background_enabled==pictures);
        // Legacy integer values, including invalid indices, all resolve to the installed default.
        for (int old:{-10,0,1,2,3,4,99}) {
            m.theme=old;m.load_theme(old);
            CHECK(m.theme==0 && m.picture_background==2 && m.picture_background_enabled==pictures);
        }
        m.press(xb::B);m.press(xb::B);CHECK(m.viewer==Viewer::None);
        m.viewer=Viewer::Music;m.viewer_item=7;m.viewer_full=true;m.open_guide();
        m.perform_guide_entry({"Themes",GuideAction::SelectTheme});
        m.press(xb::Guide);m.press(xb::B);CHECK(!m.guide && m.viewer==Viewer::Themes);
        m.press(xb::B);
        CHECK(m.guide && m.viewer==Viewer::Music && m.viewer_item==7 && m.viewer_full);
    }
    {
        Model m;m.era=1;m.picture_background=2;m.picture_background_enabled=false;
        m.set_installed_themes({{"theme-2222222222222222","Zeta",false},{"theme-1111111111111111","Alpha",true}});
        CHECK(m.theme_count()==4&&m.theme_entry(1)=="Alpha [Compatibility 4/4]"&&m.theme_entry(99).empty());
        CHECK(m.theme_entries()==std::vector<std::string>({"Xbox 360","Alpha [Compatibility 4/4]","Zeta [Partial compatibility 1/4]","Picture Background"}));
        m.installed_theme_id="theme-2222222222222222";m.open_themes();CHECK(m.viewer_item==2);
        m.press(xb::Left);CHECK(m.viewer_item==0);m.press(xb::Right);CHECK(m.viewer_item==3);m.viewer_item=2;
        m.press(xb::LB);CHECK(m.viewer_item==1);m.press(xb::RB);CHECK(m.viewer_item==2);
        auto effect=m.press(xb::A);CHECK(effect.command==Command::ApplyInstalledTheme&&effect.app==1&&!m.picture_background_enabled);
        const auto saved=m.saved_installed_theme_setting();CHECK(saved=="installed_theme=theme-2222222222222222");
        Model restored;restored.load_installed_theme_setting(saved+"\r\n");CHECK(restored.installed_theme_id==m.installed_theme_id);
        restored.load_installed_theme_setting("installed_theme=theme-NOTHEX00000000");CHECK(restored.installed_theme_id==m.installed_theme_id);
        restored.load_installed_theme_setting("installed_theme=");CHECK(restored.installed_theme_id.empty());
        m.viewer_item=2;effect=m.press(xb::X);CHECK(effect.command==Command::SaveSettings&&m.theme_favorites==std::vector<std::string>({"theme-2222222222222222"}));
        m.press(xb::Y);CHECK(m.theme_filter==1&&m.viewer_item==0&&m.theme_count()==3);
        CHECK(m.theme_entry(1)=="Zeta [Partial compatibility 1/4]"&&m.theme_catalog_index_for_row(1)==1);
        m.viewer_item=1;effect=m.press(xb::A);CHECK(effect.command==Command::ApplyInstalledTheme&&effect.app==1&&m.theme_recent.front()=="theme-2222222222222222");
        m.press(xb::Y);CHECK(m.theme_filter==2&&m.theme_count()==3&&m.theme_entry(1)=="Zeta [Partial compatibility 1/4]");
        const auto library=m.saved_theme_library_settings();CHECK(library==std::vector<std::string>({"installed_theme=theme-2222222222222222","theme_favorite=theme-2222222222222222","theme_recent=theme-2222222222222222"}));
        Model library_restored;for(const auto&line:library)library_restored.load_installed_theme_setting(line+"\n");
        CHECK(library_restored.installed_theme_id==m.installed_theme_id&&library_restored.theme_favorites==m.theme_favorites&&library_restored.theme_recent==m.theme_recent);
        library_restored.load_installed_theme_setting("theme_favorite=theme-2222222222222222");CHECK(library_restored.theme_favorites.size()==1);
        library_restored.load_installed_theme_setting("theme_recent=bad");CHECK(library_restored.theme_recent.size()==1);
    }
    for (int era:{1,2}) {
        Model m;m.era=era;m.channel=MyXbox;m.selected[MyXbox]=1;
        CHECK(m.current_card().goal==Goal::ToProfile);
        m.press(xb::A);CHECK(m.viewer==Viewer::Profile);
        m.press(xb::Down);m.press(xb::Down);m.press(xb::A);CHECK(m.viewer==Viewer::Themes && m.viewer_count()==1);
        m.press(xb::B);m.press(xb::B);CHECK(m.current()==1);
    }
    for (int build:{15574,17559}) {
        Model m;m.era=3;m.dashboard_build=build;m.select_metro_tab(2);m.metro_focus=0;
        CHECK(m.count()==3 && m.current_card().title=="Friends");
        m.press(xb::Right);CHECK(m.metro_focus==kMetroSocialOwner);
        m.press(xb::A);CHECK(m.viewer==Viewer::Profile);
        m.press(xb::Down);m.press(xb::Down);m.press(xb::A);CHECK(m.viewer==Viewer::Themes);
        m.press(xb::B);m.press(xb::B);CHECK(m.metro_tab==2 && m.current()==0 && m.metro_focus==kMetroSocialOwner);
        m.press(xb::Right);CHECK(m.metro_focus==kMetroSocialFriend0);
        m.press(xb::Right);m.press(xb::Right);CHECK(m.metro_focus==kMetroSocialFriend2);
        m.press(xb::Left);CHECK(m.metro_focus==kMetroSocialFriend2-1);
        m.press(xb::Down);CHECK(m.metro_focus==0);
        m.press(xb::Down);CHECK(m.current()==1 && m.metro_focus==1);
    }
    Model legacy;legacy.load_picture_setting("picture_background=1");
    CHECK(legacy.picture_background_enabled && legacy.picture_background==1);
}

static void avatar_stage()
{
    Model m = with_apps(1);
    // The gamercard opens Profile; Customize Avatar still reaches the stage.
    m.press(xb::Right);
    CHECK(m.current_card().title == kBladesGamercard);
    CHECK(m.current_card().icon == IconAvatar);
    Model before = m;
    Effect e = m.press(xb::A);
    CHECK(m.viewer == Viewer::Profile && e.command == Command::None);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Unfold);
    m.press(xb::Down);m.press(xb::A);
    CHECK(m.viewer==Viewer::Avatar);
    // No avatar installed yet: only Back, and A does nothing.
    CHECK(m.hints().size() == 1 && has_hint(m, xb::B, "Back"));
    CHECK(m.press(xb::A).command == Command::None);
    m.press(xb::Right);
    CHECK(m.viewer_item == 0);
    // With a 36-angle turntable: Left / Right go all the way round, A waves.
    m.avatar_angles = 36;
    CHECK(has_hint(m, xb::A, "Wave") && has_hint(m, xb::Up, "Turn"));
    m.press(xb::Left);
    CHECK(m.viewer_item == 35);
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.viewer_item == 1);
    m.press(xb::RB);
    CHECK(m.viewer_item == 5); // an eighth of a turn
    m.press(xb::LT);
    CHECK(m.viewer_item == 0);
    before = m;
    e = m.press(xb::A);
    CHECK(e.command == Command::AvatarWave && m.viewer == Viewer::Avatar);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Select);
    // Up / Down mean nothing here, and B folds the stage away.
    m.press(xb::Down);
    CHECK(m.viewer_item == 0 && m.viewer == Viewer::Avatar);
    m.press(xb::B);
    CHECK(m.viewer == Viewer::None && m.channel == MyXbox);
    // The Guide's Home blade opens it too.
    m.press(xb::Guide);
    const int at = find_action(m,BladeHome,GuideAction::Avatar);
    CHECK(at >= 0);
    m.guide_item[BladeHome]=at;
    m.press(xb::A);
    CHECK(m.viewer == Viewer::Avatar && !m.guide);
}

static void music_library_and_player()
{
    Model m = with_apps(1);
    // The Media channel's Music card opens the library and asks for a scan.
    m.press(xb::Up);
    m.press(xb::Up);
    m.press(xb::Up);
    CHECK(m.channel == Media && m.current_card().title == "Music");
    Model before = m;
    Effect e = m.press(xb::A);
    CHECK(m.viewer == Viewer::Music && e.command == Command::ScanMusic);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Unfold);
    // Nothing found yet: only Back, and A plays nothing.
    CHECK(m.hints().size() == 3 && has_hint(m, xb::B, "Back") && has_hint(m, xb::RT, "Albums") && has_hint(m, xb::Y, "Now Playing"));
    CHECK(m.press(xb::A).command == Command::None);
    CHECK(m.press(xb::LS).command == Command::None && !m.music_shuffle);
    CHECK(m.press(xb::RS).command == Command::None && m.music_repeat == 0);
    before = m;
    e = m.press(xb::Right);
    CHECK(e.command == Command::None && sound_for(before, m, xb::Right, e) == Sound::Blocked);
    // Twenty-five songs: a list that pages by ten.
    m.songs = 25;
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::LB, "Page") && !has_hint(m, xb::X, "Pause"));
    m.press(xb::Down);
    m.press(xb::Down);
    CHECK(m.viewer_item == 2);
    m.press(xb::RB);
    CHECK(m.viewer_item == 12);
    m.press(xb::RB);
    m.press(xb::RB);
    CHECK(m.viewer_item == 24);
    m.press(xb::Down);
    CHECK(m.viewer_item == 24);
    m.press(xb::LB);
    CHECK(m.viewer_item == 14);
    m.press(xb::LB);
    m.press(xb::LB);
    m.press(xb::Down);
    before = m;
    e = m.press(xb::A); // the song's page (music/1016_IndividualSongMain)
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::OpenSong) && m.music_target == 1 &&
          m.music_page == Model::MusicSong && m.viewer_count() == 3 && m.viewer_item == 0);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Select);
    CHECK(m.music_page_rows()[0] == "Play Song" && m.music_page_rows()[1] == "Add to Current Playlist");
    e = m.press(xb::A); // Play Song: from there on, back on the library's place
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::PlaySong) && m.music_target == 1 &&
          m.music_page == Model::MusicLibrary && m.viewer_item == 1 && m.viewer == Viewer::Music);
    // main.cpp reports the player's state back.
    m.now_playing = 1;
    m.now_playing_title = "Still Alive";
    CHECK(has_hint(m, xb::X, "Pause") && has_hint(m, xb::Y, "Now Playing"));
    e = m.press(xb::X);
    CHECK(e.command == Command::MusicPause && m.music_paused && has_hint(m, xb::X, "Resume"));
    e = m.press(xb::X);
    CHECK(e.command == Command::MusicPause && !m.music_paused);
    CHECK(m.press(xb::Right).command == Command::MusicNext);
    CHECK(m.press(xb::Left).command == Command::MusicPrevious);
    CHECK(m.viewer_item == 1); // skipping songs does not move the list
    // The stick clicks set the order: LS shuffles, RS turns repeat Off, All, One.
    CHECK(has_hint(m, xb::LS, "Shuffle") && has_hint(m, xb::RS, "Repeat"));
    before = m;
    e = m.press(xb::LS);
    CHECK(e.command == Command::MusicShuffle && m.music_shuffle);
    CHECK(sound_for(before, m, xb::LS, e) == Sound::Select);
    e = m.press(xb::RS);
    CHECK(e.command == Command::MusicRepeat && m.music_repeat == 1);
    m.press(xb::RS);
    CHECK(m.music_repeat == 2);
    m.press(xb::RS);
    CHECK(m.music_repeat == 0);
    e = m.press(xb::LS);
    CHECK(e.command == Command::MusicShuffle && !m.music_shuffle);
    CHECK(m.viewer == Viewer::Music && m.viewer_item == 1 && m.now_playing == 1);
    // The triggers turn the view: Albums, then Artists, then Songs again.
    e = m.press(xb::RT);
    CHECK(e.command == Command::MusicBrowse && m.music_view == 1 && m.music_in_groups() && m.viewer_item == 0);
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::RT, "Artists"));
    m.songs = 4; // main.cpp: four albums
    m.press(xb::Down);
    m.press(xb::Down);
    e = m.press(xb::A); // open the third album
    CHECK(e.command == Command::MusicBrowse && m.music_group == 2 && m.viewer_item == 0 && !m.music_in_groups());
    m.songs = 10; // four options, including Edit Album Info, and six songs
    CHECK(has_hint(m, xb::A, "Select") && m.music_group_actions().size() == 4 && m.music_group_actions()[0] == "Play Album");
    e = m.press(xb::A);
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::PlayGroup) && m.music_target == 2);
    m.press(xb::Down);
    e = m.press(xb::A);
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::AddGroup) && m.music_page == Model::MusicLibrary);
    m.press(xb::Down);
    m.press(xb::Down);
    m.press(xb::Down); // pass the new Edit Album Info action
    e = m.press(xb::A); // the first song
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::OpenSong) && m.music_target == 4);
    m.press(xb::B);
    CHECK(m.music_page == Model::MusicLibrary && m.viewer_item == 4 && m.music_group == 2);
    e = m.press(xb::B); // back to the albums, on the one left
    CHECK(e.command == Command::MusicBrowse && m.viewer == Viewer::Music && m.music_group < 0 && m.viewer_item == 2);
    m.songs = 4;
    m.press(xb::LT);
    CHECK(m.music_view == 0 && !m.music_in_groups());
    m.press(xb::LT);
    CHECK(m.music_view == 6 && !m.music_in_groups() && has_hint(m, xb::RT, "Songs")); // Recently Added
    m.press(xb::LT);
    CHECK(m.music_view == 5 && !m.music_in_groups() && has_hint(m, xb::RT, "Recently Added")); // Most Played
    m.press(xb::RT);
    m.press(xb::RT);
    CHECK(m.music_view == 0);
    m.songs = 25;
    m.viewer_item = 1;
    // B folds the library away; the music plays on.
    m.press(xb::B);
    CHECK(m.viewer == Viewer::None && m.now_playing == 1);

    // The Guide's Media blade shows what is playing and the controls.
    m.press(xb::Guide);m.guide_blade=BladeMedia;
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::Music);
    CHECK(m.press(xb::A).command==Command::GuideMusicRefresh);
    auto media = m.blade_entries(BladeMedia);
    CHECK(media.size() == 12 && media[5].label == "Now Playing: Still Alive" && media[6].label == "Pause Music" &&
          media[7].label == "Next Song" && media[8].label == "Stop Music");
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::MusicPause);
    before = m;
    e = m.press(xb::A); // Pause Music: in place
    CHECK(e.command == Command::MusicPause && m.music_paused && m.guide);
    CHECK(sound_for(before, m, xb::A, e) == Sound::GuideSelect);
    CHECK(m.blade_entries(BladeMedia)[std::size_t(m.guide_item[BladeMedia])].label == "Resume Music");
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::MusicNext);
    e = m.press(xb::A);
    CHECK(e.command == Command::MusicNext && m.guide);
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::MusicStop);
    e = m.press(xb::A); // Stop Music: the controls go, the place stays in range
    CHECK(e.command == Command::MusicStop && m.now_playing < 0 && !m.music_paused && m.guide);
    media = m.blade_entries(BladeMedia);
    CHECK(find_action(m,BladeMedia,GuideAction::GuideMusicAlbums)>=0 && find_action(m,BladeMedia,GuideAction::GuideMusicOpenFull)>=0);
    CHECK(m.guide_item[BladeMedia] <= int(media.size()) - 1);
    // Full Music is explicit; the normal Music row stays inside the popup.
    m.now_playing = 0;
    m.guide_item[BladeMedia] = find_action(m,BladeMedia,GuideAction::GuideMusicOpenFull);
    e = m.press(xb::A);
    CHECK(m.viewer == Viewer::Music && e.command == Command::ScanMusic && !m.guide);
    e = m.press(xb::Y);
    CHECK(e.command == Command::MusicBrowse && m.music_page == Model::MusicNowPlaying);
    e = m.press(xb::Y);
    CHECK(e.command == Command::MusicStop && m.now_playing < 0);
    CHECK(m.press(xb::Y).command == Command::None);
}

static void avatar_editor()
{
    Model m = with_apps(1);
    // Without a kit there is no editor: Y on the stage does nothing.
    m.press(xb::Right);
    m.press(xb::A);
    CHECK(m.viewer==Viewer::Profile);
    m.press(xb::Down);m.press(xb::A);
    CHECK(m.viewer == Viewer::Avatar && !has_hint(m, xb::Y, "Edit"));
    CHECK(m.press(xb::Y).command == Command::None && m.viewer == Viewer::Avatar);
    // With one: Y opens the editor over the stage.
    m.avatar_editable = true;
    m.avatar_categories = 19;
    m.avatar_angles = 36;
    CHECK(has_hint(m, xb::Y, "Edit"));
    Model before = m;
    Effect e = m.press(xb::Y);
    CHECK(m.viewer == Viewer::AvatarEdit && e.command == Command::AvatarEditStart && m.avatar_category == 0);
    CHECK(sound_for(before, m, xb::Y, e) == Sound::Unfold);
    CHECK(has_hint(m, xb::A, "Save") && has_hint(m, xb::B, "Cancel") && has_hint(m, xb::X, "Random"));
    // Up / Down move through the categories and stop at the ends.
    m.press(xb::Up);
    CHECK(m.avatar_category == 0);
    before = m;
    e = m.press(xb::Down);
    CHECK(m.avatar_category == 1 && sound_for(before, m, xb::Down, e) == Sound::Focus);
    for (int i = 0; i < 30; ++i)
        m.press(xb::Down);
    CHECK(m.avatar_category == 18);
    // Left / Right change the choice; main.cpp does the changing.
    e = m.press(xb::Left);
    CHECK(e.command == Command::AvatarChange && e.app == -1);
    e = m.press(xb::Right);
    CHECK(e.command == Command::AvatarChange && e.app == 1);
    // LB / RB turn an eighth, LT faces front; X, Y.
    m.press(xb::RB);
    CHECK(m.viewer_item == 4);
    m.press(xb::LB);
    m.press(xb::LB);
    CHECK(m.viewer_item == 32);
    m.press(xb::LT);
    CHECK(m.viewer_item == 0);
    CHECK(m.press(xb::X).command == Command::AvatarRandom);
    CHECK(m.press(xb::Y).command == Command::AvatarEmote);
    // B puts it back and returns to the stage; A keeps it.
    before = m;
    e = m.press(xb::B);
    CHECK(e.command == Command::AvatarCancel && m.viewer == Viewer::Avatar);
    CHECK(sound_for(before, m, xb::B, e) == Sound::Fold);
    m.press(xb::Y);
    before = m;
    e = m.press(xb::A);
    CHECK(e.command == Command::AvatarSave && m.viewer == Viewer::Avatar);
    CHECK(sound_for(before, m, xb::A, e) == Sound::Select);
    // The Guide's Home blade has the editor too, once there is a kit.
    m.press(xb::B);
    m.press(xb::Guide);
    const int at=find_action(m,BladeHome,GuideAction::AvatarEdit);
    CHECK(at >= 0);
    m.guide_item[BladeHome]=at;
    e = m.press(xb::A);
    CHECK(m.viewer == Viewer::AvatarEdit && !m.guide && e.command == Command::AvatarEditStart);
    Model plain;
    for (const auto &entry : plain.host_blade_entries(BladeHome))
        CHECK(entry.label != "Avatar Editor");
}

static void recently_played_come_first()
{
    Model m = with_apps(3);
    m.set_games(some_games(3));
    // Nothing played: name order.
    CHECK(m.game_order() == std::vector<int>({0, 1, 2}));
    m.played("title:PPSA04607"); // Game 2
    m.played("title:CUSA12345"); // Game 1, most recent
    CHECK(m.game_order() == std::vector<int>({1, 2, 0}));
    m.press(xb::Up);
    m.press(xb::Up); // Games
    const Card c = m.current_card();
    CHECK(c.title == "Game 1" && c.game == 1); // the card keeps the game's real index
    CHECK(m.press(xb::A).app == 1);
    const auto blade = m.host_blade_entries(BladeGames);
    CHECK(blade[1].app == 1 && blade[2].app == 2 && blade[3].app == 0);
    // Homebrew in Apps too; playing again moves it up without duplicates.
    m.played("/data/homebrew/App2/eboot.elf");
    m.played("/data/homebrew/App1/eboot.elf");
    m.played("/data/homebrew/App2/eboot.elf");
    CHECK(m.app_homebrew() == std::vector<int>({2, 1, 0}));
    CHECK(m.recent.size() == 4 && m.recent[0] == "/data/homebrew/App2/eboot.elf");
    for (int i = 0; i < 20; ++i)
        m.played("/data/homebrew/X" + std::to_string(i) + "/eboot.elf");
    CHECK(m.recent.size() == kMaxRecent);
    // Loading: junk and duplicates are dropped.
    m.load_recent({"title:PPSA00001", "title:bad", "/etc/passwd", "title:PPSA00001", "/data/homebrew/App0/eboot.elf"});
    CHECK(m.recent == std::vector<std::string>({"title:PPSA00001", "/data/homebrew/App0/eboot.elf"}));
}

static void guide_library_shortcuts_and_preferences()
{
    Model m = with_apps(3);
    m.apps[0].title = "zeta";
    m.apps[1].title = "Alpha";
    m.apps[2].title = "beta";
    m.set_games(some_games(2));
    m.played("/data/homebrew/App1/eboot.elf");
    m.played("title:CUSA12345");
    CHECK(m.app_homebrew() == std::vector<int>({1, 0, 2}));
    const auto home = m.host_blade_entries(BladeHome);
    CHECK(home[0].label == "Xbox Dashboard" && home[1].label == "Quick Launch" && home[1].action == GuideAction::RecentGames);
    CHECK(home[2].label == "Play Game 1" && home[2].action == GuideAction::LaunchTitle && home[2].app == 1);
    CHECK(home[3].label == "Play Alpha" && home[3].action == GuideAction::Launch && home[3].app == 1);
    m.open_guide();
    m.guide_item[BladeHome] = find_action(m,BladeHome,GuideAction::LaunchTitle,1);
    CHECK(m.press(xb::A).command == Command::LaunchTitle && !m.guide);
    m.open_guide();
    const auto entries = m.blade_entries(BladeHome);
    int summary = 0;
    while (summary < int(entries.size()) && entries[std::size_t(summary)].action != GuideAction::LibrarySummary)
        ++summary;
    CHECK(summary < int(entries.size()));
    m.guide_item[BladeHome] = summary;
    CHECK(m.press(xb::A).command == Command::None && m.details && !m.guide);
    CHECK(m.details_text.find("Games: 2   Apps: 3") != std::string::npos);
    CHECK(m.details_text.find("Recently played:\nGame 1\nAlpha") != std::string::npos);
    m.details = false;
    m.open_guide();
    m.guide_blade = BladeSettings;
    auto activate = [&](GuideAction action) {
        const auto settings = m.blade_entries(BladeSettings);
        for (std::size_t i = 0; i < settings.size(); ++i)
            if (settings[i].action == action)
            {
                m.guide_item[BladeSettings] = int(i);
                return m.press(xb::A).command;
            }
        return Command::None;
    };
    CHECK(activate(GuideAction::CycleLibraryOrder) == Command::SaveSettings && m.library_order == 1);
    CHECK(m.app_homebrew() == std::vector<int>({1, 2, 0}));
    CHECK(activate(GuideAction::CycleLibraryOrder) == Command::SaveSettings && m.library_order == 2);
    CHECK(m.app_homebrew() == std::vector<int>({0, 2, 1}));
    CHECK(activate(GuideAction::CycleLibraryOrder) == Command::SaveSettings && m.library_order == 0);
    CHECK(activate(GuideAction::CycleNotifications) == Command::SaveSettings && m.notification_level == 3);
    CHECK(activate(GuideAction::CycleNotifications) == Command::SaveSettings && m.notification_level == 0);
    CHECK(m.blade_entries(BladeSettings)[std::size_t(m.guide_item[BladeSettings])].label == "Notifications: Off");
}

static void attached_shortcuts_and_notification_history()
{
    Model m = with_apps(3);
    m.set_games(some_games(2));
    m.load_pins({"/data/homebrew/App2/eboot.elf", "title:CUSA12345"});
    const auto home = m.host_blade_entries(BladeHome);
    CHECK(home[2].label == "Attached: App 2" && home[2].action == GuideAction::Launch && home[2].app == 2);
    CHECK(home[3].label == "Attached: Game 1" && home[3].action == GuideAction::LaunchTitle && home[3].app == 1);
    m.open_guide();
    m.guide_item[BladeHome] = find_action(m,BladeHome,GuideAction::Launch,2);
    CHECK(m.press(xb::A).command == Command::Launch && !m.guide);
    for (int i = 0; i < 12; ++i)
        m.record_notification("Notice " + std::to_string(i), "Body " + std::to_string(i));
    CHECK(m.notification_history.size() == 10);
    CHECK(m.notification_history.front().title == "Notice 11" && m.notification_history.back().title == "Notice 2");
    m.open_guide();
    const auto entries = m.blade_entries(BladeHome);
    int history = 0;
    while (history < int(entries.size()) && entries[std::size_t(history)].action != GuideAction::NotificationHistory)
        ++history;
    CHECK(history < int(entries.size()));
    m.guide_item[BladeHome] = history;
    CHECK(m.press(xb::A).command == Command::None && m.details && !m.guide);
    CHECK(m.details_text.find("Notice 11: Body 11") != std::string::npos);
    CHECK(m.details_text.find("Notice 8") == std::string::npos);
}

static void sounds_follow_the_360()
{
    Model m = with_apps(3);
    auto step = [&](xb::Button b) {
        Model before = m;
        Effect e = m.press(b);
        return sound_for(before, m, b, e);
    };
    CHECK(step(xb::Right) == Sound::PaneRight);
    CHECK(step(xb::Down) == Sound::Blocked); // already on the last channel
    CHECK(step(xb::LT) == Sound::PaneLeft);
    CHECK(step(xb::Left) == Sound::Blocked);
    CHECK(step(xb::Up) == Sound::ChannelUp); // to Apps
    CHECK(step(xb::Down) == Sound::ChannelDown); // back to My Xbox
    CHECK(step(xb::Up) == Sound::ChannelUp);
    CHECK(step(xb::A) == Sound::Launch); // launch
    CHECK(step(xb::B) == Sound::Back);
    CHECK(step(xb::B) == Sound::None); // nothing to go back from
    CHECK(step(xb::Guide) == Sound::GuideOpen);
    CHECK(step(xb::Down) == Sound::GuideFocus);
    CHECK(step(xb::RB) == Sound::BladeSwitch);
    CHECK(step(xb::Up) == Sound::Blocked); // top of the list
    CHECK(step(xb::B) == Sound::GuideClose);
    // T19: the Guide's Home rows are the 360's own (Xbox Dashboard, Quick Launch,
    // Connect to Xbox LIVE ...), so go to the Games channel directly.
    m.channel = Games; m.selected[Games] = 0;
    for (int i = 0; i < 4; ++i)
        step(xb::Up);
    CHECK(m.channel == Friends);
    CHECK(step(xb::A) == Sound::Refused);
    CHECK(step(xb::B) == Sound::Back);
    m.press(xb::Down);
    m.press(xb::Down);
    m.press(xb::Down);
    m.press(xb::Down);
    m.controller_map = true;
    CHECK(step(xb::X) == Sound::None);
    CHECK(step(xb::B) == Sound::Back);
}

static void details_order_my_xbox()
{
    Model m = with_apps(4);
    m.press(xb::Up); // Apps
    for (int i = 0; i < 3; ++i)
    {
        via_details(m, InfoAction::Attach);
        m.press(xb::Right);
    }
    CHECK(m.pins.size() == 3);
    m.press(xb::Down); // My Xbox: disc slot, Gamer Card, then App 0, App 1, App 2
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(m.current_card().title == "App 0");
    m.press(xb::Y);
    // First attachment: it can only move later.
    auto labels = m.info_entries();
    CHECK(labels.size() == 4 && labels[2].action == InfoAction::MoveLater);
    m.press(xb::Down);
    m.press(xb::Down);
    CHECK(m.press(xb::A).command == Command::SavePins && m.info);
    CHECK(m.pins[1] == "/data/homebrew/App0/eboot.elf" && m.current() == 3); // selection follows it
    CHECK(m.current_card().title == "App 0");
    // Now in the middle: both moves are offered.
    labels = m.info_entries();
    CHECK(labels[2].action == InfoAction::MoveEarlier && labels[3].action == InfoAction::MoveLater);
    m.press(xb::B);
    // Detaching from its own card closes Details, as the card is gone.
    CHECK(via_details(m, InfoAction::Detach).command == Command::SavePins && !m.info && m.pins.size() == 2);
    // A Play from Details launches the card's own title.
    m.press(xb::Left);
    m.press(xb::Y);
    CHECK(m.press(xb::A).command == Command::Launch && !m.info);
}

static void details_move_homebrew_between_channels()
{
    Model m = with_apps(3);
    m.press(xb::Up); // Apps
    m.press(xb::Right);
    CHECK(m.current_card().title == "App 1");
    Effect e = via_details(m, InfoAction::ToGames);
    CHECK(e.command == Command::SaveCategories && m.game_apps.count("/data/homebrew/App1/eboot.elf") && !m.info);
    CHECK(m.count() == 2 && m.current_card().title == "App 2"); // gone from Apps
    m.press(xb::Up); // Games: no console games, just the homebrew game
    CHECK(m.count() == 1 && m.current_card().title == "App 1" && m.current_card().kind == Kind::App);
    m.set_games(some_games(2));
    m.press(xb::RT);
    CHECK(m.count() == 3 && m.current_card().title == "App 1"); // after the installed games
    m.press(xb::Y);
    CHECK(m.info_entries().back().action == InfoAction::ToApps);
    m.press(xb::B);
    e = via_details(m, InfoAction::ToApps);
    CHECK(e.command == Command::SaveCategories && m.game_apps.empty() && m.count() == 2);
    // Games with nothing in it says how to fill it.
    m.set_games({});
    CHECK(m.current_card().kind == Kind::Empty && m.current_card().title == "No Games Found");
    m.press(xb::A);
    CHECK(m.details && m.details_text.find("Details") != std::string::npos);
}

static void details_sounds_and_guide()
{
    Model m = with_apps(2);
    m.press(xb::Up);
    auto step = [&](xb::Button b) {
        Model before = m;
        Effect e = m.press(b);
        return sound_for(before, m, b, e);
    };
    CHECK(step(xb::Y) == Sound::Unfold && m.info);
    CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::B, "Back"));
    CHECK(m.info_entries().size() == 3); // Open, Attach, Show in Games
    CHECK(step(xb::Down) == Sound::Focus);
    CHECK(step(xb::Down) == Sound::Focus);
    CHECK(step(xb::Down) == Sound::Blocked);
    CHECK(step(xb::B) == Sound::Fold && !m.info);
    m.press(xb::Y);
    m.press(xb::Guide); // the Guide takes over from Details
    CHECK(!m.info && m.guide);
}

static void details_choose_a_trailer()
{
    Model m = with_apps(2);
    m.press(xb::Up);
    m.press(xb::Y);
    for (const auto &e : m.info_entries())
        CHECK(e.action != InfoAction::CycleTrailer); // no trailers on the console: no entry
    m.press(xb::B);
    m.trailers = {"bayonetta", "gears of war 2"};
    m.press(xb::Y);
    auto entries = m.info_entries();
    CHECK(entries.back().action == InfoAction::CycleTrailer && entries.back().label == "Trailer: Automatic");
    while (m.info_item + 1 < int(entries.size()))
        m.press(xb::Down);
    const std::string key = m.info_target.key;
    CHECK(m.press(xb::A).command == Command::SaveTrailers && m.info); // stays open to watch it
    CHECK(m.trailer_choice.at(key) == "bayonetta" && m.info_entries().back().label == "Trailer: Bayonetta");
    m.press(xb::A);
    CHECK(m.info_entries().back().label == "Trailer: Gears Of War 2");
    m.press(xb::A);
    CHECK(m.trailer_choice.at(key).empty() && m.info_entries().back().label == "Trailer: None");
    m.press(xb::A);
    CHECK(!m.trailer_choice.count(key) && m.info_entries().back().label == "Trailer: Automatic");

    // Left and Right step both ways and wrap; they tick like the focus.
    const Model before = m;
    const Effect e = m.press(xb::Left);
    CHECK(e.command == Command::SaveTrailers && m.trailer_choice.at(key).empty());
    CHECK(sound_for(before, m, xb::Left, e) == Sound::Focus);
    m.press(xb::Left);
    CHECK(m.trailer_choice.at(key) == "gears of war 2");
    m.press(xb::Right);
    m.press(xb::Right);
    CHECK(!m.trailer_choice.count(key));
    m.press(xb::Right);
    CHECK(m.trailer_choice.at(key) == "bayonetta" && m.info);

    // A display name from the manifest beats the title-cased key.
    m.trailer_names["bayonetta"] = "Bayonetta (TGS 2008)";
    CHECK(m.info_entries().back().label == "Trailer: Bayonetta (TGS 2008)");

    // Left / Right on any other row do nothing.
    m.press(xb::Up);
    const auto choice = m.trailer_choice;
    const Model still = m;
    const Effect none = m.press(xb::Right);
    CHECK(none.command == Command::None && m.trailer_choice == choice);
    CHECK(sound_for(still, m, xb::Right, none) == Sound::Blocked);
}

static void blades_matches_its_visible_rows()
{
    Model m=with_apps(9);
    m.set_games(some_games(13));
    m.era=0;
    m.press(xb::Left);
    CHECK(m.channel==Media);
    CHECK(m.current()==0 && m.current_card().title==kBladesGamercard);
    m.press(xb::Down);
    CHECK(m.channel==Media && m.current()==1);
    m.press(xb::Down); m.press(xb::Down); m.press(xb::Down);
    CHECK(m.current()==4 && m.current_card().title=="Media Center");
    m.press(xb::Down);
    CHECK(m.current()==5 && m.current_card().title=="Game Library");
    m.press(xb::Up); m.press(xb::Up); m.press(xb::Up);
    CHECK(m.current()==2);
    m.press(xb::A);
    CHECK(m.viewer==Viewer::Pictures);
    m.press(xb::B);
    CHECK(m.viewer==Viewer::None && m.current()==2);
    m.press(xb::Up);
    m.press(xb::LB);
    CHECK(m.channel==Games);
    CHECK(m.card(1).title=="Achievements" && m.card(2).title=="Played Games" && m.card(5).title=="Trailers");
    m.press(xb::RT);
    CHECK(m.current()==6 && m.current_card().title=="Game Library");
    m.press(xb::LT);
    CHECK(m.current()==0);
    m.press(xb::RB);
    CHECK(m.channel==Media && m.current()==1);
}

static void blades_tray_opens_and_returns_to_the_same_slot()
{
    for (int build:{1888,6770}) for (int channel:{Friends,Media,Games,Apps}) {
        Model m=with_apps(2);
        m.era=0;m.dashboard_build=build;m.channel=channel;
        const int tray=m.count()-1;
        m.press(xb::RT);
        CHECK(m.current()==tray && m.current_card().goal==Goal::ToSearch);
        CHECK(m.press(xb::A).command==Command::None && m.viewer==Viewer::Library);
        CHECK(m.viewer_count()==2);
        m.press(xb::B);
        CHECK(m.viewer==Viewer::None && m.channel==channel && m.current()==tray);
        m.set_apps({});
        CHECK(m.current()>=0 && m.current()<m.count());
        m.press(xb::RT);m.press(xb::Up);
        CHECK(m.current()==m.count()-2);
    }
}

static void metro_tabs_enter_and_return()
{
    Model m=with_apps(3);
    m.set_games(some_games(13));
    m.era=3;
    m.select_metro_tab(3);
    m.press(xb::Up);
    CHECK(m.metro_focus==-1 && m.metro_tab==3 && !m.current_is_title());
    m.press(xb::Right);
    CHECK(m.metro_focus==-1 && m.metro_tab==4 && m.channel==Media && m.count()==2);
    m.press(xb::Right);
    CHECK(m.metro_tab==5 && m.count()==7);
    CHECK(m.card(0).title=="Music Library" && m.card(1).title=="Random Album" &&
          m.card(2).title=="Most Played" && m.card(3).title=="Saved Playlists" &&
          m.card(4).title=="Recently Added" && m.card(5).title=="Albums" &&
          m.card(6).title=="Now Playing");
    CHECK(m.press(xb::A).command==Command::None && m.metro_focus==0 && m.viewer==Viewer::None);
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.viewer==Viewer::Music);
    m.press(xb::B); m.press(xb::B);
    CHECK(m.metro_focus==-1 && m.viewer==Viewer::None);

    // Every personalized choice is a sibling dashboard tile.  It does not
    // require opening Music and then finding an internal tab.
    m.press(xb::Down);
    m.selected[m.channel]=1;m.metro_focus=1;
    CHECK(m.press(xb::A).command==Command::MusicEdit);
    CHECK(m.viewer==Viewer::None);
    m.selected[m.channel]=2;m.metro_focus=2;
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.viewer==Viewer::Music && m.music_view==5);
    m.press(xb::B);
    m.selected[m.channel]=3;m.metro_focus=3;
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.music_view==4);
    m.press(xb::B);
    m.selected[m.channel]=4;m.metro_focus=4;
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.music_view==6);
    m.press(xb::B);
    m.selected[m.channel]=5;m.metro_focus=0;
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.music_view==1);
    m.press(xb::B);
    m.selected[m.channel]=6;m.metro_focus=1;
    CHECK(m.press(xb::A).command==Command::MusicBrowse && m.music_page==Model::MusicNowPlaying);
    m.press(xb::B);m.press(xb::B);m.press(xb::B);
    m.press(xb::RT);
    CHECK(m.metro_tab==7 && m.metro_focus==-1);
    m.press(xb::Down); m.press(xb::A);
    CHECK(!m.guide && m.viewer==Viewer::SystemSettings && m.system_entries()[2].label=="Storage");
    m.press(xb::B);
    CHECK(m.viewer==Viewer::None && m.metro_tab==7 && m.metro_focus==0);
    m.select_metro_tab(3);
    m.press(xb::RT);
    CHECK(m.current()==12);
    m.press(xb::RB); m.press(xb::LB);
    CHECK(m.metro_tab==3 && m.current()==12 && m.metro_focus==12%kMetroPageSize);
}

static void metro_every_tile_is_reachable()
{
    for (int n=1;n<=19;n++)
    {
        Model start;
        start.set_games(some_games(n)); start.era=3; start.select_metro_tab(3);
        std::deque<Model> pending{start};
        std::set<int> seen, cards, auxiliary;
        while (!pending.empty())
        {
            Model m=pending.front();pending.pop_front();
            const int key=m.current()*16+m.metro_focus+1;
            if (!seen.insert(key).second) continue;
            if (m.metro_focus>=0 && m.metro_focus<kMetroPageSize) cards.insert(m.current());
            else auxiliary.insert(m.metro_focus);
            for (xb::Button b:{xb::Left,xb::Right,xb::Up,xb::Down})
            {
                Model next=m;next.press(b);
                if (next.metro_tab==3) pending.push_back(next);
            }
        }
        CHECK(int(cards.size())==n);
        CHECK(auxiliary.count(-1) && auxiliary.count(kMetroPins) && auxiliary.count(kMetroRecent));
    }
}

static void metro_collections_and_details_keep_context()
{
    Model m=with_apps(2);
    m.set_games(some_games(3));m.era=3;m.select_metro_tab(3);
    m.load_pins({"title:PPSA01949","/data/homebrew/App0/eboot.elf","title:PPSA99999"});
    m.load_recent({"/data/homebrew/App1/eboot.elf","title:PPSA01949"});
    m.press(xb::Left);
    CHECK(m.metro_focus==kMetroPins);
    m.press(xb::A);
    CHECK(m.viewer==Viewer::Library && m.collection==Collection::Pins && m.viewer_count()==2);
    m.press(xb::Down);m.press(xb::Y);
    CHECK(m.info && m.info_card().title=="App 0");
    m.press(xb::B);
    CHECK(!m.info && m.viewer==Viewer::Library && m.viewer_item==1);
    CHECK(m.press(xb::A).command==Command::Launch);
    m.press(xb::B);
    CHECK(m.metro_focus==kMetroPins && m.viewer==Viewer::None);
    m.press(xb::Down);m.press(xb::A);
    CHECK(m.collection==Collection::Recent && m.library_cards()[0].title=="App 1");
    m.press(xb::B);m.select_metro_tab(0);m.press(xb::A);
    CHECK(m.viewer==Viewer::Library && m.viewer_count()==5);
    m.press(xb::Right);
    CHECK(m.library_filter==1 && m.viewer_count()==2);
    m.press(xb::Left);
    CHECK(m.library_filter==0 && m.viewer_count()==5);
}

static void refresh_and_system_navigation()
{
    Model m=with_apps(2);
    m.era=0;
    // The 1888 System blade: its six items, each doing the nearest PS5 thing.
    CHECK(m.channel==MyXbox && m.count()==6 && m.card(0).title=="Console Settings" && m.card(5).title=="Initial Setup");
    // Console Settings opens dashSysCslSet's nine buttons in place.
    m.press(xb::A);
    CHECK(!m.guide && m.blades_page==1 && m.count()==9 && m.current()==0 && m.blades_page_title()=="Console Settings");
    CHECK(m.card(0).title=="Display" && m.card(6).title=="Screen Saver" && m.card(8).title=="System Info");
    CHECK(has_hint(m,xb::B,"Back"));
    for (int i=0;i<6;i++) m.press(xb::Down);
    const bool dim=m.dimming;
    m.press(xb::A); // Screen Saver toggles dimming, staying on the page
    CHECK(m.dimming!=dim && m.blades_page==1 && !m.guide);
    m.press(xb::B);
    CHECK(m.blades_page==0 && m.current()==0 && m.count()==6);
    // Memory: devices, then a device's categories; B climbs back a level at a time.
    m.press(xb::Down);m.press(xb::Down);
    m.press(xb::A);
    CHECK(m.blades_page==2 && m.card(1).title=="Hard Drive");
    m.press(xb::Down);
    CHECK(m.press(xb::A).command==Command::ScanMusic && m.blades_page==3 && m.card(2).title=="Videos and Trailers");
    m.press(xb::B);
    CHECK(m.blades_page==2 && m.current()==1);
    m.press(xb::B);
    CHECK(m.blades_page==0 && m.current()==2);
    // Family Settings: Console Controls / Xbox Live Controls, then the controls.
    m.press(xb::Up);
    m.press(xb::A);
    CHECK(m.blades_page==4 && m.blades_page_title()=="Family Settings" && m.card(1).title=="Xbox Live Controls");
    m.press(xb::A);
    CHECK(m.blades_page==5 && m.count()==7 && m.card(6).title=="Turn Off Family Settings");
    m.press(xb::B);
    CHECK(m.blades_page==4 && m.current()==0);
    m.press(xb::B);
    CHECK(m.blades_page==0 && m.current()==1);
    m.press(xb::Down);
    m.press(xb::Up);m.press(xb::Up);
    m.press(xb::Down);m.press(xb::Down);m.press(xb::Down);
    CHECK(m.card(m.current()).title=="Network Settings" && m.press(xb::A).command==Command::ReadNetwork);
    CHECK(m.system_page==Model::NetworkMain);
    m.press(xb::B);
    m.press(xb::Down);m.press(xb::Down);
    CHECK(m.card(m.current()).title=="Initial Setup");m.press(xb::A);
    // T11: Initial Setup opens the setup flow; the dashboard picker stays under Guide > Settings.
    CHECK(m.viewer==Viewer::InitialSetup);
    m.viewer=Viewer::None;
    m.channel=Friends;
    CHECK(m.card(1).title=="Messages" && m.card(3).title=="Marketplace");
    // X signs out: the Games blade shows its signed-out scene until the
    // gamercard (or Create Gamer Profile) signs back in.
    m.channel=Games;m.selected[Games]=0;
    m.press(xb::X);
    CHECK(m.blades_signed_out && m.card(1).title=="Create Gamer Profile" && m.count()==6);
    m.press(xb::A);
    CHECK(!m.blades_signed_out && m.card(1).title=="Achievements");
    m.channel=MyXbox;

    m.era=3;m.set_games(some_games(13));m.select_metro_tab(3);m.press(xb::RT);
    CHECK(m.current()==12 && m.metro_focus==5);
    m.set_games(some_games(2));
    CHECK(m.current()==1 && m.metro_focus==1);
    m.press(xb::Up);m.set_games({});
    CHECK(m.metro_focus==-1 && m.count()==1);
    m.select_metro_tab(0);m.press(xb::A);
    m.launch_failed("Test title");
    CHECK(m.details && m.viewer==Viewer::Library);
    m.press(xb::B);
    CHECK(!m.details && m.viewer==Viewer::Library);
    m.press(xb::B);
    CHECK(m.viewer==Viewer::None && m.metro_tab==0);
}

static void era_picker_and_update_navigation()
{
    Model m;
    m.open_guide();m.guide_blade=BladeSettings;m.guide_item[BladeSettings]=find_action(m,BladeSettings,GuideAction::CycleEra);
    m.press(xb::A);
    CHECK(m.viewer==Viewer::DashboardEras && !m.guide);
    m.press(xb::Down);m.press(xb::A);
    CHECK(m.viewer==Viewer::DashboardUpdates && m.picker_family==2 && m.viewer_count()==6);
    m.press(xb::X);
    CHECK(m.picker_previews && m.viewer_count()>6 && m.viewer_item==0);
    m.press(xb::B);
    CHECK(m.viewer==Viewer::DashboardEras && m.viewer_item==2);
    m.press(xb::B);
    CHECK(m.guide && m.guide_item[BladeSettings]==find_action(m,BladeSettings,GuideAction::CycleEra) && m.viewer==Viewer::None);
    for (const auto update:dashboard_updates()) {
        m.guide=false;m.viewer=Viewer::DashboardUpdates;m.picker_family=update.family;m.picker_previews=true;
        const auto choices=updates_for(update.family,true);
        for (size_t i=0;i<choices.size();i++) if (choices[i].build==update.build) m.viewer_item=int(i);
        CHECK(m.press(xb::A).command==Command::SaveSettings);
        CHECK(m.era==kDashboardFamilies[update.family].era && m.dashboard_build==update.build);
        CHECK(m.viewer==Viewer::None && !m.guide && m.channel==MyXbox);
        CHECK(m.current()>=0 && m.current()<m.count());
    }
    m.era=3;m.dashboard_build=17559;m.open_guide();
    for (int i=0;i<6;i++) m.press(xb::LB);
    CHECK(m.guide_blade==BladeGames && m.blade_title(BladeGames)=="Games & Apps");
    m.guide=false;m.era=0;m.dashboard_build=1888;m.channel=Games;
    for (int i=0;i<5;i++) m.press(xb::LB);
    CHECK(m.channel==Friends);
    m.dashboard_build=6770;m.press(xb::LB);
    CHECK(m.channel==Apps);
    m.open_guide();m.guide_blade=BladeMedia;
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::Music);
    m.press(xb::A);
    CHECK(m.guide && !m.guide_host && m.viewer==Viewer::None && m.guide_music_page>0);
    m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::GuideMusicOpenFull);m.press(xb::A);
    CHECK(!m.guide && m.viewer==Viewer::Music);
    m.open_guide();m.guide_blade=BladeSettings;m.guide_item[BladeSettings]=find_action(m,BladeSettings,GuideAction::CycleEra);
    m.press(xb::A);m.press(xb::A);m.press(xb::B);m.press(xb::B);
    CHECK(m.guide && m.viewer==Viewer::Music && m.guide_blade==BladeSettings);
}
static void promotions_keep_library_actions_and_legacy_navigation()
{
    Model m=with_apps(2);
    Card promo{"Bayonetta",Kind::Hub,IconVideo,Goal::ToVideos,-1,-1,"bayo.jpg"};
    promo.promo=0;promo.preview="bayonetta";
    m.promo_cards={promo,promo,promo};m.promotions=true;m.trailers={"halo 3","bayonetta"};
    CHECK(m.count()==12); // nine NXE slots (with Solutions) and three promotions
    m.press(xb::RT);m.press(xb::A);
    CHECK(m.viewer==Viewer::Videos && m.viewer_full && m.viewer_item==1);
    m.viewer=Viewer::None;m.viewer_full=false;m.era=3;m.dashboard_build=15574;m.select_metro_tab(1);
    CHECK(m.count()==4 && m.metro_v1_home());
    std::set<int> reached{0};std::deque<int> pending{0};
    while (!pending.empty()) {
        int focus=pending.front();pending.pop_front();
        for (auto button:{xb::Left,xb::Right,xb::Up,xb::Down}) {
            Model next=m;next.metro_focus=focus;if(focus<7)next.selected[MyXbox]=focus;
            next.press(button);
                CHECK(next.metro_focus==-1 || next.metro_focus==7 || next.metro_focus==9 || (next.metro_focus>=0 && next.metro_focus<4));
            if(next.metro_focus>=0 && reached.insert(next.metro_focus).second)pending.push_back(next.metro_focus);
        }
    }
            CHECK(reached==std::set<int>({0,1,2,3,7,9}));
    m.metro_focus=7;m.press(xb::A);
    CHECK(m.viewer==Viewer::Library && m.collection==Collection::Recent);
    m.viewer=Viewer::None;m.dashboard_build=17559;m.select_metro_tab(1);
    CHECK(m.count()==7 && !m.metro_v1_home());
    m.promotions=false;m.select_metro_tab(1);
    m.press(xb::LT);
    CHECK(m.count()==6 && m.current_card().title=="Your Homebrew");
    m.promotions=true;m.promo_cards.resize(1);m.select_metro_tab(1);
    CHECK(m.card(0).promo==0 && m.card(2).goal==Goal::ToGames && m.card(3).goal==Goal::ToMedia);
    m.dashboard_build=15574;m.select_metro_tab(1);
    CHECK(m.count()==4 && m.card(0).promo==0 && m.card(2).goal==Goal::ToGames);
}
static void metro_v1_pages_have_no_invisible_targets()
{
    for (int total=1;total<=13;total++) {
        Model m;m.era=3;m.dashboard_build=15574;m.set_games(some_games(total));m.select_metro_tab(3);
        std::set<int> reached;std::set<std::pair<int,int>> visited;
        std::deque<Model> pending{m};
        while (!pending.empty()) {
            Model at=pending.front();pending.pop_front();
            if (!visited.insert({at.current(),at.metro_focus}).second) continue;
            if (at.metro_focus>=0) {
                CHECK(at.metro_focus<5 && at.metro_focus==at.current()%5);
                reached.insert(at.current());
            }
            CHECK(at.current()>=0 && at.current()<total);
            for (auto button:{xb::Left,xb::Right,xb::Up,xb::Down}) {
                Model next=at;next.press(button);
                if (next.metro_tab==3) pending.push_back(next);
            }
        }
        if (int(reached.size())!=total) {std::printf("total=%d reached:",total);for(int i:reached)std::printf(" %d",i);std::puts("");}
        CHECK(int(reached.size())==total);
        m.press(xb::RT);CHECK(m.current()==total-1 && m.metro_focus==(total-1)%5);
        m.press(xb::B);CHECK(m.metro_focus==-1);
        m.press(xb::Down);CHECK(m.metro_focus==(total-1)%5);
        m.dashboard_build=17559;m.select_metro_tab(3);m.press(xb::RT);
        CHECK(m.metro_focus==(total-1)%7);
    }
}
void ads_profiles_have_independent_modes_and_navigation()
{
    const int eras[]={0,0,1,2,3,3},builds[]={1888,6717,9199,12625,14699,17559};
    Model m;
    for (int i=0;i<3;i++) {AdEntry ad;ad.id=std::to_string(i);ad.title="Archived image "+ad.id;m.ad_catalog.push_back(ad);}
    for (int profile=0;profile<6;profile++) {
        m.era=eras[profile];m.dashboard_build=builds[profile];
        CHECK(m.current_ad_profile()==profile);
        const Viewer previous=profile%2?Viewer::Pictures:Viewer::None;
        const int previous_item=profile%2?7:0;
        m.viewer=previous;m.viewer_item=previous_item;m.viewer_full=profile%2;m.pictures=20;
        m.open_guide();if (m.era>=1) m.press(xb::RS);m.guide_blade=BladeSettings;
        const auto entries=m.blade_entries(BladeSettings);int item=-1;
        for (int i=0;i<int(entries.size());i++) if (entries[size_t(i)].action==GuideAction::AdsSettings) item=i;
        CHECK(item>=0);m.guide_item[BladeSettings]=item;m.press(xb::A);
        CHECK(!m.guide && m.viewer==Viewer::AdsSettings && m.viewer_count()==8);
        for (int i=0;i<5;i++) m.press(xb::Down);
        CHECK(m.press(xb::A).command==Command::SaveAdProfiles);
        CHECK(m.ad_mode[profile]==1 && m.ad_selected[profile]==std::vector<std::string>{"0"});
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.ad_selected[profile].size()==2);
        m.press(xb::LT);m.press(xb::Right);
        CHECK(m.ad_mode[profile]==2);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.ad_shuffle_seed[profile]==1);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.ad_selected[profile].size()==3 && m.ad_mode[profile]==2);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.ad_selected[profile].empty() && m.ad_mode[profile]==2);
        m.press(xb::RT);CHECK(m.viewer_item==7);m.press(xb::A);
        CHECK(m.ad_selected[profile]==std::vector<std::string>{"2"});
        m.press(xb::Guide);CHECK(m.guide && m.viewer==Viewer::AdsSettings);
        m.press(xb::B);CHECK(!m.guide && m.viewer==Viewer::AdsSettings && m.viewer_item==7);
        m.press(xb::Guide);if (m.era>=1) m.press(xb::RS);
        m.guide_blade=BladeSettings;m.guide_item[BladeSettings]=item;m.press(xb::A);
        CHECK(!m.guide && m.viewer==Viewer::AdsSettings && m.ad_previous_viewer==previous);
        m.press(xb::B);CHECK(m.guide && m.guide_blade==BladeSettings && m.guide_item[BladeSettings]==item && m.viewer==previous && m.viewer_item==previous_item && m.viewer_full==bool(profile%2));
        for (int other=profile+1;other<6;other++) CHECK(m.ad_mode[other]==0 && m.ad_selected[other].empty() && m.ad_shuffle_seed[other]==0);
        m.guide=false;
    }
    m.era=1;m.dashboard_build=9199;m.viewer=Viewer::AdsSettings;m.viewer_item=0;
    m.press(xb::Right);CHECK(m.ad_mode[2]==3 && m.ad_mode[1]==2 && m.ad_mode[3]==2);
    for (int i=0;i<4;i++) m.press(xb::Down);
    CHECK(m.press(xb::A).command==Command::SaveAdProfiles);
    CHECK(m.ad_mode[2]==0 && m.ad_selected[2].empty() && m.ad_shuffle_seed[2]==0);
    CHECK(m.ad_mode[1]==2 && m.ad_selected[1]==std::vector<std::string>{"2"} && m.ad_shuffle_seed[1]==1);
    m.viewer=Viewer::None;
    m.era=0;m.dashboard_build=1888;m.channel=Friends;m.promotions=true;
    Card card;card.title="Custom image";card.promo=0;card.goal=Goal::ToVideos;m.promo_cards={card};
    m.ad_mode[0]=0;const int original=m.count();
    m.ad_mode[0]=1;CHECK(m.count()==original+1);
    m.selected[Friends]=m.count()-2;CHECK(m.current_card().promo==0);
    CHECK(m.press(xb::A).command==Command::None && m.viewer==Viewer::Videos);
    m.viewer=Viewer::None;m.ad_mode[0]=0;m.set_apps(m.apps);
    CHECK(m.count()==original && m.current()<m.count());
}
void custom_ad_pages_keep_every_selected_image_reachable()
{
    const int eras[]={0,0,1,2,3,3},builds[]={1888,6717,9199,12625,14699,17559};
    for (int i=0;i<6;i++) {
        Model m;m.era=eras[i];m.dashboard_build=builds[i];m.promotions=true;m.ad_mode[m.current_ad_profile()]=1;
        for (int n=0;n<13;n++) {Card c;c.title="Ad "+std::to_string(n);c.promo=n;c.goal=Goal::ToVideos;m.promo_cards.push_back(c);}
        m.channel=m.era==0?Friends:MyXbox;
        if (m.era==3) m.select_metro_tab(1);
        m.press(xb::RT);if (m.era==0) m.press(xb::Up);
        CHECK(m.current_card().promo==12);
        if (m.era!=3) continue;
        CHECK(m.count()==14 && m.metro_page_size()==(i==4?4:7));
        m.press(xb::LT);
        std::set<int> reached;std::set<std::pair<int,int>> visited;std::deque<Model> pending{m};
        while (!pending.empty()) {
            Model at=pending.front();pending.pop_front();
            if (!visited.insert({at.current(),at.metro_focus}).second) continue;
            if (at.metro_focus>=0 && at.metro_focus<at.metro_page_size() && at.current_card().promo>=0)
                reached.insert(at.current_card().promo);
            for (auto button:{xb::Left,xb::Right,xb::Up,xb::Down}) {
                Model next=at;next.press(button);if (next.metro_tab==1) pending.push_back(next);
            }
        }
        CHECK(reached.size()==13);
        m.ad_mode[m.current_ad_profile()]=0;CHECK(m.count()==(i==4?4:7));
    }
}
void ad_candidate_filter_keeps_manual_resources_explicit()
{
    Model m;m.viewer=Viewer::AdsSettings;
    for (int i=0;i<3;i++) {AdEntry entry;entry.id=std::to_string(i);entry.title=entry.id;entry.auto_eligible=i!=0;m.ad_catalog.push_back(entry);}
    CHECK(m.ad_visible_count()==2 && m.viewer_count()==7 && m.ad_catalog_index(0)==1 && m.ad_catalog_index(1)==2 && m.ad_catalog_index(2)==-1);
    for (int i=0;i<5;i++) m.press(xb::Down);
    m.press(xb::A);CHECK(m.ad_selected[m.current_ad_profile()]==std::vector<std::string>{"1"});
    CHECK(m.press(xb::X).command==Command::None && m.ad_show_all && m.viewer_item==0 && m.viewer_count()==8);
    for (int i=0;i<5;i++) m.press(xb::Down);
    m.press(xb::A);CHECK(m.ad_selected[m.current_ad_profile()].size()==2 && m.ad_selected[m.current_ad_profile()].back()=="0");
    m.press(xb::X);CHECK(!m.ad_show_all && m.viewer_item==0);
    m.press(xb::Down);m.press(xb::Down);m.press(xb::A);
    const auto &selected=m.ad_selected[m.current_ad_profile()];
    CHECK(selected.size()==3 && std::find(selected.begin(),selected.end(),"0")!=selected.end());
    m.press(xb::LT);m.press(xb::Right);CHECK(m.ad_mode[m.current_ad_profile()]==2 && selected.size()==3);
}

static Model video_library()
{
    Model m = with_apps(1);
    m.trailers = {"halo 3", "bayonetta", "crackdown", "army of two"};
    m.trailer_names = {{"halo 3", "Halo 3"}, {"bayonetta", "Bayonetta"}, {"crackdown", "Crackdown"}, {"army of two", "Army of Two"}};
    m.video_info["halo 3"] = {132000, 9000000, true, true};
    m.video_info["bayonetta"] = {60000, 1000000, true, false};
    m.video_info["crackdown"] = {0, 0, false, false}; // in the manifest, not on the console
    m.video_info["army of two"] = {90000, 5000000, true, false};
    m.open_guide();
    m.guide_blade = BladeMedia;
    for (const auto &e : m.blade_entries(BladeMedia))
        if (e.action == GuideAction::Videos)
            m.perform_guide_entry(e);
    return m;
}

static void game_videos_library_sorts_and_hides_missing()
{
    Model m = video_library();
    CHECK(m.viewer == Viewer::Videos && !m.guide);
    // A clip whose frames are not on the console is not offered.
    CHECK(m.viewer_count() == 3 && m.video_hidden() == 1);
    CHECK((m.video_list() == std::vector<std::string>{"halo 3", "bayonetta", "army of two"}));
    CHECK(has_hint(m, xb::X, "Sort by Title") && has_hint(m, xb::Y, "Refresh"));
    CHECK(m.press(xb::Y).command == Command::RefreshMedia);
    // X sorts by Title, keeping the same video selected.
    m.press(xb::Right); // bayonetta
    Effect e = m.press(xb::X);
    CHECK(e.command == Command::SaveSettings && m.video_order == 1);
    CHECK((m.video_list() == std::vector<std::string>{"army of two", "bayonetta", "halo 3"}));
    CHECK(m.video_key() == "bayonetta" && m.viewer_item == 1);
    // Type: all trailers are Game Videos, with Title breaking the tie.
    m.press(xb::Right); // halo 3
    CHECK(m.press(xb::A).command == Command::SaveVideos && m.viewer_full);
    m.press(xb::B);
    m.press(xb::X);
    CHECK(m.video_order == 2 && m.video_list().front() == "army of two" && m.video_key() == "halo 3");
    CHECK(has_hint(m, xb::X, "Sort by Download Date"));
    m.press(xb::X);
    CHECK(m.video_order == 0 && m.video_key() == "halo 3" && m.viewer_item == 0);
}

static void game_videos_seek_pause_and_resume()
{
    // Theater Party is a first-class Video-grid action, not a hidden
    // full-screen toggle.  A fresh item enters the same player directly in
    // Theater mode while the ordinary A action remains ordinary playback.
    Model theater = video_library();
    CHECK(has_hint(theater, xb::RS, "Theater Party"));
    CHECK(theater.press(xb::RS).command == Command::SaveVideos);
    CHECK(theater.viewer_full && theater.video_theater && theater.video_pending.empty());
    theater.press(xb::B);
    CHECK(!theater.viewer_full && theater.video_theater);
    CHECK(theater.press(xb::A).command == Command::SaveVideos);
    CHECK(theater.viewer_full && !theater.video_theater);

    Model m = video_library();
    CHECK(m.video_key() == "halo 3");
    Model before = m;
    Effect e = m.press(xb::A);
    CHECK(m.viewer_full && m.video_elapsed == 0 && !m.video_paused && sound_for(before, m, xb::A, e) == Sound::Select);
    CHECK(has_hint(m, xb::A, "Pause") && has_hint(m, xb::RT, "Seek") && has_hint(m, xb::Y, "Subtitles Off"));
    // Left / Right seek ten seconds, the triggers thirty; never past either end.
    m.video_elapsed = 40000; // as main.cpp reports while it plays
    e = m.press(xb::Right);
    CHECK(e.command == Command::VideoSeek && e.app == 50000 && m.video_elapsed == 50000 && m.viewer_item == 0);
    before = m;
    e = m.press(xb::LT);
    CHECK(e.app == 20000 && sound_for(before, m, xb::LT, e) == Sound::Focus);
    m.press(xb::LT);
    CHECK(m.video_elapsed == 0 && m.press(xb::Left).command == Command::None);
    for (int i = 0; i < 6; ++i)
        m.press(xb::RT);
    CHECK(m.video_elapsed == 132000);
    // A pauses and plays; at the end it replays from the start.
    m.video_elapsed = 70000;
    CHECK(m.press(xb::A).command == Command::VideoPause && m.video_paused && has_hint(m, xb::A, "Play"));
    CHECK(m.press(xb::A).command == Command::VideoPause && !m.video_paused);
    m.video_paused = true;
    m.video_elapsed = 132000; // main.cpp holds the last frame
    CHECK(has_hint(m, xb::A, "Restart"));
    e = m.press(xb::A);
    CHECK(e.command == Command::VideoSeek && e.app == 0 && !m.video_paused);
    // Y turns the sidecar subtitles off and on; a video without them ignores it.
    CHECK(m.press(xb::Y).command == Command::SaveSettings && !m.video_subtitles && has_hint(m, xb::Y, "Subtitles On"));
    m.press(xb::Y);
    // B keeps the place; opening it again offers Resume / Restart.
    m.video_elapsed = 70000;
    CHECK(m.press(xb::B).command == Command::SaveVideos && !m.viewer_full);
    CHECK(m.video_resume["halo 3"] == 70000);
    CHECK(m.press(xb::A).command == Command::None && !m.viewer_full && m.video_pending == "halo 3");
    CHECK(m.press(xb::A).command == Command::SaveVideos);
    CHECK(m.viewer_full && m.video_pending.empty() && m.video_elapsed == 70000);
    CHECK(!m.video_theater);
    CHECK(m.press(xb::RS).command == Command::None && m.video_theater);
    CHECK(has_hint(m,xb::X,"Wave"));
    CHECK(m.press(xb::Down).command == Command::None && m.video_theater_reaction == 1);
    CHECK(has_hint(m,xb::X,"Cheer"));
    const unsigned reaction=m.video_theater_reaction_serial;
    CHECK(m.press(xb::X).command == Command::None && m.video_theater_reaction_serial == reaction+1);
    CHECK(m.press(xb::RS).command == Command::None && !m.video_theater);
    // RB moves to the next video, keeping this one's place.
    m.video_elapsed = 80000;
    CHECK(m.press(xb::RB).command == Command::SaveVideos && m.video_key() == "bayonetta" && m.viewer_full);
    CHECK(m.video_resume["halo 3"] == 80000 && m.video_elapsed == 0 && !has_button(m, xb::Y));
    CHECK(m.press(xb::Y).command == Command::None && m.video_subtitles);
    // Nearly finished or barely started: nothing to resume.
    m.video_elapsed = 57000;
    m.press(xb::LB);
    CHECK(m.video_resume.count("bayonetta") == 0 && m.video_key() == "halo 3" && m.video_elapsed == 80000);
    m.video_elapsed = 3000;
    m.press(xb::B);
    CHECK(m.video_resume.count("halo 3") == 0);
    // Saved most recent first, and loaded back for clips still on the console.
    m.video_resume["bayonetta"] = 30000;
    const auto lines = m.saved_videos();
    CHECK((lines == std::vector<std::string>{"halo 3\t0", "bayonetta\t30000"}));
    Model again = video_library();
    again.load_videos({"bayonetta\t30000", "gone\t9000", "halo 3\t0", "bayonetta\t1", "army of two\t2000"});
    CHECK((again.video_recent == std::vector<std::string>{"bayonetta", "halo 3", "army of two"}));
    CHECK(again.video_resume.size() == 1 && again.video_resume["bayonetta"] == 30000);
    // Promotions also offer the saved-position choice before opening full screen.
    Card promo{"Bayonetta", Kind::Hub, IconVideo, Goal::ToVideos, -1, -1, ""};
    promo.preview = "bayonetta";
    again.viewer = Viewer::None;
    again.promo_cards = {promo};
    again.promotions = true;
    again.selected[again.channel] = again.count() - 1;
    CHECK(again.current_card().preview == "bayonetta");
    CHECK(again.press(xb::A).command == Command::None && again.video_pending == "bayonetta" && !again.viewer_full);
    CHECK(again.press(xb::A).command == Command::SaveVideos);
    CHECK(again.viewer_full && again.video_key() == "bayonetta" && again.video_elapsed == 30000);
}

static void video_download_dates_sort_cycle_and_settings()
{
    Model m = video_library();
    m.video_info["halo 3"].downloaded = 100;
    m.video_info["bayonetta"].downloaded = 300;
    m.video_info["army of two"].downloaded = 300;
    m.video_info["crackdown"].downloaded = 900; // missing is still hidden
    CHECK((m.video_list() == std::vector<std::string>{"bayonetta", "army of two", "halo 3"}));
    const std::string selected = m.video_key();
    for (int i = 0; i < kVideoOrderCount; i++)
    {
        const int next = (m.video_order + 1) % kVideoOrderCount;
        CHECK(has_hint(m, xb::X, kVideoOrders[next]));
        CHECK(m.press(xb::X).command == Command::SaveSettings);
        CHECK(m.video_order == next && m.video_key() == selected);
    }
    m.video_info.erase("halo 3"); // unknown date sorts after known dates
    CHECK(m.video_list().back() == "halo 3");
    m.trailer_names["bayonetta"] = "ALPHA";
    m.trailer_names["army of two"] = "alpha";
    m.video_order = 1;
    CHECK((m.video_list() == std::vector<std::string>{"bayonetta", "army of two", "halo 3"}));
    m.video_order = 2;
    CHECK((m.video_list() == std::vector<std::string>{"bayonetta", "army of two", "halo 3"}));
    for (int old = 0; old < 3; old++)
    {
        m.load_video_order(old, true);
        CHECK(m.video_order == (old == 1 ? 1 : 0));
        m.load_video_order(old, false);
        CHECK(m.video_order == old);
    }
    m.load_video_order(-1, false); CHECK(m.video_order == 0);
    m.load_video_order(90, false); CHECK(m.video_order == 2);
    m.load_video_order(90, true); CHECK(m.video_order == 0);
    m.trailers.clear();
    CHECK(m.press(xb::X).command == Command::SaveSettings && m.viewer_item == 0);
    CHECK(m.press(xb::A).command == Command::None && !m.viewer_full);
}

static void video_resume_restart_choice()
{
    CHECK(std::string(kVideoStartActions[0]) == "Resume" && std::string(kVideoStartActions[1]) == "Restart");
    const int eras[][2] = {{0,6770},{1,9199},{2,13604},{3,15574},{3,17559}};
    for (const auto &era : eras)
    {
        Model m = video_library();
        m.era = era[0];
        m.dashboard_build = era[1];
        m.video_resume["halo 3"] = 70000;
        const auto recent = m.video_recent;
        Model before = m;
        Effect e = m.press(xb::A);
        CHECK(e.command == Command::None && m.video_pending == "halo 3" && !m.viewer_full);
        CHECK(sound_for(before, m, xb::A, e) == Sound::Unfold);
        CHECK(m.video_recent == recent && m.video_start_item == 0);
        CHECK(has_hint(m, xb::A, "Select") && has_hint(m, xb::B, "Back") && !has_button(m, xb::X));
        CHECK(m.press(xb::X).command == Command::None && m.video_order == 0);
        CHECK(m.press(xb::Right).command == Command::None && m.viewer_item == 0);
        CHECK(m.press(xb::Y).command == Command::None);
        before = m;
        e = m.press(xb::Down);
        CHECK(m.video_start_item == 1 && sound_for(before, m, xb::Down, e) == Sound::Focus);
        m.press(xb::Down); CHECK(m.video_start_item == 1);
        m.press(xb::Up); CHECK(m.video_start_item == 0);
        m.press(xb::Guide); CHECK(m.guide && m.video_pending == "halo 3");
        m.press(xb::B); CHECK(!m.guide && m.video_pending == "halo 3");
        before = m;
        e = m.press(xb::B);
        CHECK(m.video_pending.empty() && !m.viewer_full && m.viewer == Viewer::Videos);
        CHECK(m.video_resume["halo 3"] == 70000 && m.video_recent == recent);
        CHECK(sound_for(before, m, xb::B, e) == Sound::Back);
        m.press(xb::A); m.press(xb::Down);
        CHECK(m.press(xb::A).command == Command::SaveVideos && m.viewer_full && m.video_elapsed == 0);
        CHECK(m.video_pending.empty() && m.video_resume.count("halo 3") == 0);
        CHECK(m.video_recent.front() == "halo 3");
        m.press(xb::B);
        for (int stale : {0, 2000, 129000, 132000, 200000})
        {
            m.video_resume["halo 3"] = stale;
            CHECK(m.press(xb::A).command == Command::SaveVideos && m.viewer_full && m.video_elapsed == 0);
            CHECK(m.video_pending.empty() && m.video_resume.count("halo 3") == 0);
            m.press(xb::B);
        }
        m.video_info["halo 3"].ms = 0; // unknown duration still supports Resume
        m.video_resume["halo 3"] = 9000;
        m.press(xb::A);
        CHECK(!m.video_pending.empty());
        CHECK(m.press(xb::A).command == Command::SaveVideos && m.video_elapsed == 9000);
        m.press(xb::B); m.press(xb::A);
        m.trailers.clear(); // refreshed media disappeared while a choice was open
        CHECK(m.press(xb::A).command == Command::None && m.video_pending.empty() && !m.viewer_full);
    }
}

// The media screens use each retail build's own words (strings/*.json).
static void media_text_follows_each_build()
{
    namespace mt = dash::media_text;
    Model m;
    struct Era { int era, build; const char *now, *videos, *pictures, *loading; };
    const Era eras[] = {{0, 1888, "Music Player", "Video", "Pictures", "Loading..."},
                        {0, 6717, "Now Playing", "Videos", "Pictures", "Loading picture"},
                        {1, 9199, "Now Playing", "Video Library", "Picture Library", "Loading picture"},
                        {2, 13604, "Now Playing", "Video Library", "Picture Library", "Loading..."},
                        {3, 15574, "Now Playing", "Video Library", "Picture Library", "Loading..."},
                        {3, 17559, "Now Playing", "Video Library", "Picture Library", "Loading..."}};
    for (const Era &e : eras)
    {
        m.era = e.era;
        m.dashboard_build = e.build;
        CHECK(std::string(mt::now_playing(m)) == e.now && std::string(mt::videos_title(m)) == e.videos &&
              std::string(mt::pictures_title(m)) == e.pictures && std::string(mt::loading_picture(m)) == e.loading);
    }
    CHECK(std::string(mt::music_empty(0)) == "No songs found." && std::string(mt::music_empty(3)) == "No genres found");
    // Only Metro V2 named the caption toggle; it said "Closed Captions".
    m.era = 3;
    m.dashboard_build = 15574;
    CHECK(m.metro_v1() && std::string(mt::captions(m, true)) == "Subtitles Off");
    m.dashboard_build = 17559;
    CHECK(!m.metro_v1() && std::string(mt::captions(m, true)) == "Closed Captions Off" &&
          std::string(mt::captions(m, false)) == "Closed Captions On");
}
static void subtitles_parse_sidecar_srt()
{
    const std::string text = "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:04,500\r\n<i>Hello</i> there\r\nsecond line\r\n\r\n"
                              "2\n00:00:05.250 --> 00:00:06,000\n{\\an8}Top\n3\n00:01:00,000 --> 00:01:02,000\nLater\n\n"
                              "bad\n00:00:09,000 --> nonsense\nSkipped\n\n4\n00:00:07,000 --> 00:00:07,000\nEmpty span\n";
    const auto cues = srt::parse(text);
    CHECK(cues.size() == 3);
    CHECK(cues[0].from == 1000 && cues[0].to == 4500 && cues[0].lines.size() == 2 && cues[0].lines[0] == "Hello there");
    CHECK(cues[1].from == 5250 && cues[1].lines == std::vector<std::string>{"Top"}); // its number joined no cue
    CHECK(cues[2].from == 60000 && cues[2].lines[0] == "Later");
    CHECK(srt::at(cues, 999) == nullptr && srt::at(cues, 1000) == &cues[0] && srt::at(cues, 4499) == &cues[0]);
    CHECK(srt::at(cues, 4500) == nullptr && srt::at(cues, 5300) == &cues[1] && srt::at(cues, 61000) == &cues[2]);
    CHECK(srt::parse("").empty() && srt::parse("garbage\n\n\n").empty());
}

static void scene_guide_media_player()
{
    Model m;
    m.era = 1;
    m.media_player = 1; // NXE: the controls above the song
    auto media = m.blade_entries(BladeMedia);
    CHECK(media.size() >= 5 && media[0].label == "Video Library" && media[3].action == GuideAction::RecordScreen);
    CHECK(std::any_of(media.begin(),media.end(),[](const GuideEntry &row){return row.label=="Select Music" && row.action==GuideAction::Music;}));
    m.now_playing = 0;
    m.now_playing_title = "Still Alive";
    media = m.blade_entries(BladeMedia);
    CHECK(find_action(m,BladeMedia,GuideAction::MusicPause)>=0 && find_action(m,BladeMedia,GuideAction::MusicPrevious)>=0 &&
          find_action(m,BladeMedia,GuideAction::MusicStop)>=0 && find_action(m,BladeMedia,GuideAction::MusicNext)>=0 &&
          find_action(m,BladeMedia,GuideAction::MusicShuffle)>=0 && find_action(m,BladeMedia,GuideAction::MusicVolume)>=0 &&
          std::any_of(media.begin(),media.end(),[](const GuideEntry &row){return row.label=="Still Alive";}));
    m.media_player = 2; // Kinect / Metro: the song first
    media = m.blade_entries(BladeMedia);
    CHECK(media[4].label == "Still Alive" && media[5].action == GuideAction::MusicPause);
    m.open_guide();
    m.guide_blade = BladeMedia;
    m.guide_item[BladeMedia] = find_action(m,BladeMedia,GuideAction::MusicPrevious);
    auto e = m.perform_guide_entry(m.blade_entries(BladeMedia)[std::size_t(m.guide_item[BladeMedia])]);
    CHECK(e.command == Command::MusicPrevious && m.guide);
    e = m.perform_guide_entry(m.blade_entries(BladeMedia)[std::size_t(find_action(m,BladeMedia,GuideAction::MusicShuffle))]);
    CHECK(e.command == Command::MusicShuffle && m.music_shuffle && m.guide);
    CHECK(m.blade_entries(BladeMedia)[std::size_t(find_action(m,BladeMedia,GuideAction::MusicShuffle))].label == "Shuffle Off");
    e = m.perform_guide_entry(m.blade_entries(BladeMedia)[std::size_t(find_action(m,BladeMedia,GuideAction::MusicVolume))]);
    CHECK(e.command == Command::None && m.guide_volume_open && m.music_volume == 8);
    e = m.press(xb::Right);CHECK(e.command == Command::MusicVolume && e.app == 9 && m.music_volume == 9);
    e = m.press(xb::A);CHECK(e.command == Command::MusicVolume && e.app == 0 && m.music_volume == 0);
    e = m.press(xb::A);CHECK(e.command == Command::MusicVolume && e.app == 9 && m.music_volume == 9);
    m.press(xb::B);CHECK(!m.guide_volume_open && m.guide);
    m.era = 0; // the Blades Guide keeps its own list; RS never opens a secondary menu
    m.open_guide();m.press(xb::RS);CHECK(m.guide && !m.guide_host);
    CHECK(m.blade_entries(BladeMedia)[0].label != "Media Channel" || m.blade_entries(BladeMedia).size() != 11);
}

static void music_pages()
{
    Model m;
    m.perform_guide_entry({"Music Library", GuideAction::Music});
    m.songs = 5;
    m.now_playing = 0;
    m.music_queue = 4;
    // Y: Now Playing, Edit or Save Playlist then the four songs.
    Effect e = m.press(xb::Y);
    CHECK(m.music_page == Model::MusicNowPlaying && m.viewer_count() == 5 && m.music_page_rows()[0] == "Edit or Save Playlist");
    CHECK(has_hint(m, xb::Y, "Stop") && has_hint(m, xb::B, "Back"));
    m.press(xb::Down);
    m.press(xb::Down);
    e = m.press(xb::A); // the second song now
    CHECK(e.command == Command::MusicEdit && e.app == int(MusicOp::Jump) && m.music_target == 1);
    m.viewer_item = 0;
    e = m.press(xb::A); // Edit or Save Playlist
    CHECK(m.music_page == Model::MusicEditPlaylist && m.viewer_count() == 7 && m.music_page_rows()[2] == "Done");
    m.viewer_item = 4; // the second song
    CHECK(has_hint(m, xb::X, "Remove") && has_hint(m, xb::LB, "Move Up / Down"));
    e = m.press(xb::LB);
    CHECK(e.app == int(MusicOp::MoveUp) && m.music_target == 1 && m.viewer_item == 3);
    CHECK(m.press(xb::LB).command == Command::None); // the first cannot go higher
    m.viewer_item = 6;
    CHECK(m.press(xb::RB).command == Command::None); // nor the last lower
    e = m.press(xb::X);
    CHECK(e.app == int(MusicOp::Remove) && m.music_target == 3 && m.music_queue == 3 && m.viewer_item == 5);
    m.viewer_item = 0;
    e = m.press(xb::A);
    CHECK(e.app == int(MusicOp::SavePlaylist) && m.music_page == Model::MusicEditPlaylist);
    m.viewer_item = 1;
    e = m.press(xb::A); // Clear All: the music stops
    CHECK(e.app == int(MusicOp::ClearPlaylist) && m.now_playing < 0 && m.music_page == Model::MusicNowPlaying);
    e = m.press(xb::B);
    CHECK(m.music_page == Model::MusicLibrary && e.command == Command::MusicBrowse);
    // Choose Cover from a song's page; B goes back to that page, X clears it.
    m.viewer_item = 2;
    m.press(xb::A);
    m.press(xb::Down);
    m.press(xb::Down);
    e = m.press(xb::A);
    CHECK(e.app == int(MusicOp::OpenCover) && m.music_page == Model::MusicChooseCover && m.music_target == 2);
    m.music_covers = 6;
    CHECK(m.viewer_count() == 6 && has_hint(m, xb::X, "Use the Song's Own"));
    m.press(xb::B);
    CHECK(m.music_page == Model::MusicSong && m.viewer_item == 2);
    m.press(xb::A);
    m.press(xb::Down);
    e = m.press(xb::A);
    CHECK(e.app == int(MusicOp::SetCover) && m.music_target == 1 && m.music_page == Model::MusicLibrary && m.viewer_item == 2);
    // Saved Playlists: open one, its options, then Delete asks first.
    m.music_view = 4;
    m.music_group = -1;
    m.songs = 2;
    m.viewer_item = 1;
    m.press(xb::A);
    CHECK(m.music_group == 1 && m.music_group_actions().size() == 3 && m.music_group_actions()[2] == "Delete Saved Playlist");
    m.songs = 6;
    m.viewer_item = 2;
    m.press(xb::A);
    CHECK(m.music_page == Model::MusicConfirmDelete && m.viewer_count() == 2);
    m.press(xb::Down);
    m.press(xb::A); // Cancel
    CHECK(m.music_page == Model::MusicLibrary && m.music_group == 1);
    m.viewer_item = 2;
    m.press(xb::A);
    e = m.press(xb::A);
    CHECK(e.app == int(MusicOp::DeletePlaylist) && m.music_target == 1 && m.music_group < 0);
    // Leaving and coming back starts on the library.
    m.music_page = Model::MusicNowPlaying;
    m.viewer = Viewer::None;
    m.perform_guide_entry({"Music Library", GuideAction::Music});
    CHECK(m.viewer == Viewer::Music && m.music_page == Model::MusicLibrary);
}

// Visit every reachable Console Settings row in each retail build, including
// deeper choices, informational pages, unavailable dialogs, and controller map.
static void walk_console_page(Model m)
{
    const int page=m.system_page;
    const auto entries=m.system_entries();
    CHECK(!m.system_page_title().empty());
    m.viewer_item=0;
    m.press(xb::Up);
    CHECK(m.viewer_item==0);
    for (std::size_t i=0;i<entries.size()+2;++i) m.press(xb::Down);
    CHECK(m.viewer_item==std::max(0,int(entries.size())-1));
    if (entries.empty()) {
        CHECK(m.press(xb::A).command==Command::None && !m.details);
        CHECK(has_hint(m,xb::B,"Back") && !has_hint(m,xb::A,"Select"));
    }
    for (int i=0;i<int(entries.size());++i) {
        m.viewer_item=i;
        const Model before=m;
        const Effect e=m.press(xb::A);
        if (m.system_page!=page) {
            CHECK(sound_for(before,m,xb::A,e)==Sound::Select);
            walk_console_page(m);
            const Model child=m;
            const Effect back=m.press(xb::B);
            CHECK(sound_for(child,m,xb::B,back)==Sound::Back);
            CHECK(m.system_page==page && m.viewer_item==i);
        } else if (m.details) {
            CHECK(m.details_title==entries[std::size_t(i)].label);
            CHECK(!m.details_text.empty());
            CHECK(sound_for(before,m,xb::A,e)==Sound::Refused);
            CHECK(has_hint(m,xb::A,"OK"));
            m.press(xb::B);
            CHECK(!m.details && m.system_page==page && m.viewer_item==i);
            CHECK(m.sounds==before.sounds && m.dimming==before.dimming && m.clock_24_hour==before.clock_24_hour);
        } else if (m.controller_map) {
            m.press(xb::B);
            CHECK(!m.controller_map && m.system_page==page && m.viewer_item==i);
        } else if (m.viewer!=Viewer::SystemSettings) {
            // Functional routes (profile, avatar, local media and apps) leave
            // Settings. Restore the page copy and continue the exhaustive walk.
            m=before;
        } else {
            CHECK(page==Model::SystemSoundEffects || page==Model::SystemTimeFormat ||
                  page==Model::SystemWelcome || page==Model::SystemScreenSaver || page==Model::SystemResolution ||
                  page==Model::SystemScreenFormat || page==Model::SystemMusicVolume ||
                  page==Model::SystemGameMusic || page==Model::SystemLocalFriends);
        }
        CHECK(m.viewer==Viewer::SystemSettings);
    }
}

static void rendering_resolution_is_requested_until_restart()
{
    Model defaults;
    CHECK(defaults.render_scale==1 && defaults.active_render_scale==1);
    constexpr int eras[]={0,0,1,2,3,3},builds[]={1888,6717,9199,13604,15574,17559};
    for (int i=0;i<6;++i) for (int active:{1,2}) {
        Model m;m.era=eras[i];m.dashboard_build=builds[i];m.active_render_scale=active;
        m.viewer=Viewer::SystemSettings;m.system_page=Model::SystemDisplay;
        const auto rows=m.system_entries();int resolution=-1;
        for (int r=0;r<int(rows.size());++r) if(rows[r].app==Model::SystemResolution)resolution=r;
        CHECK(resolution>=0);m.viewer_item=resolution;
        CHECK(m.press(xb::A).command==Command::None && m.system_page==Model::SystemResolution && m.viewer_item==0);
        const auto choices=m.system_entries();CHECK(choices.size()==2 && choices[0].label=="1080p Original" && choices[1].label=="4K Sharp");
        CHECK(has_hint(m,xb::A,"Select") && has_hint(m,xb::B,"Back"));
        CHECK(m.system_description().find("Restart required")!=std::string::npos);
        CHECK(m.press(xb::A).command==Command::None && m.render_scale==1 && m.active_render_scale==active);
        m.press(xb::Down);const Model before=m;Effect e=m.press(xb::A);
        CHECK(e.command==Command::SaveRenderScale && e.app==2 && m.render_scale==2 && m.active_render_scale==active);
        CHECK(sound_for(before,m,xb::A,e)==Sound::Select);
        CHECK(m.press(xb::A).command==Command::None);
        CHECK(m.system_description().find("Requested: 4K Sharp")!=std::string::npos);
        CHECK((m.system_value("Screen Resolution").find("restart required")!=std::string::npos)==(active!=2));
        CHECK(m.press(xb::B).command==Command::None && m.system_page==Model::SystemDisplay && m.viewer_item==resolution);
        CHECK(m.render_scale==2 && m.active_render_scale==active);
        m.press(xb::A);CHECK(m.system_page==Model::SystemResolution && m.viewer_item==1);
        m.press(xb::Up);e=m.press(xb::A);CHECK(e.command==Command::SaveRenderScale && e.app==1 && m.render_scale==1 && m.active_render_scale==active);
        CHECK(m.sounds==before.sounds && m.dimming==before.dimming && m.clock_24_hour==before.clock_24_hour);
        m.press(xb::Up);CHECK(m.viewer_item==0);m.press(xb::Down);m.press(xb::Down);CHECK(m.viewer_item==1);
        m.press(xb::B);CHECK(m.system_page==Model::SystemDisplay && m.viewer_item==resolution);
    }
}

static void exbox_settings_are_functional_and_honest()
{
    Model m;m.era=3;m.dashboard_build=17559;m.viewer=Viewer::SystemSettings;
    const auto root=m.system_entries();
    const auto exbox=std::find_if(root.begin(),root.end(),[](const GuideEntry&e){return e.app==Model::SystemExBox;});
    CHECK(exbox!=root.end());m.viewer_item=int(exbox-root.begin());m.press(xb::A);
    CHECK(m.system_page==Model::SystemExBox && m.system_page_title()=="Ex-Box Settings");
    m.games={{"Installed Game","","PPSA12345",""}};m.apps={{"Homebrew","/data/homebrew/test","",""}};m.game_library_companion=true;
    const auto rows=m.system_entries();CHECK(rows.size()==11);
    CHECK(rows[0].label=="Dashboard Version" && rows[1].label=="Themes" && rows[2].label=="Promotions");
    CHECK(rows[3].label=="Game Library" && rows[4].label=="Library Status" && rows[5].label=="Storage & Memory");
    CHECK(rows[6].label=="Game Soundtracks" && rows[7].label=="Local Offline Friends" && rows[8].label=="Refresh Libraries");
    CHECK(m.system_description().find("PS5 system settings")!=std::string::npos);
    m.viewer_item=4;m.press(xb::A);
    CHECK(m.system_page==Model::SystemLibraryStatus && m.system_entries().size()==8);
    CHECK(m.system_value("PS4 / PS5 Games")=="1" && m.system_value("Homebrew Apps")=="1");
    m.viewer_item=3;CHECK(m.system_value("Discovery Source")=="Companion feed" && m.system_description().find("companion feed")!=std::string::npos);
    CHECK(!has_hint(m,xb::A,"Select") && has_hint(m,xb::B,"Back"));m.press(xb::B);
    m.viewer_item=5;CHECK(m.press(xb::A).command==Command::ScanStorage && m.system_page==Model::StorageDevices);m.press(xb::B);
    m.viewer_item=6;m.press(xb::A);
    CHECK(m.system_page==Model::SystemGameMusic && m.viewer_item==1 && !m.music_game_scan);
    m.press(xb::Up);CHECK(m.press(xb::A).command==Command::ScanGameMusic && m.music_game_scan);
    CHECK(m.system_value("Game Soundtracks")=="Installed Game Scan Enabled");
    m.press(xb::B);CHECK(m.system_page==Model::SystemExBox && m.viewer_item==6);
    m.viewer_item=7;m.press(xb::A);
    CHECK(m.system_page==Model::SystemLocalFriends && m.viewer_item==0 && m.local_friend_avatars);
    m.press(xb::Down);CHECK(m.press(xb::A).command==Command::SaveSettings && !m.local_friend_avatars);
    CHECK(m.system_value("Local Offline Friends")=="Hidden");
    m.press(xb::B);m.viewer_item=8;CHECK(m.press(xb::A).command==Command::Refresh);
}

static void console_settings_pages_follow_the_build()
{
    constexpr int eras[]={1,2,3,3}, builds[]={9199,13604,15574,17559};
    for (int b=0;b<4;++b) {
        Model m;
        m.era=eras[b];m.dashboard_build=builds[b];
        m.viewer=Viewer::SystemSettings;
        m.press(xb::A);
        CHECK(m.system_page==Model::SystemConsole && m.system_history.size()==1);
        auto rows=m.system_entries();
        CHECK(rows.size()==std::size_t(b==3?11:8));
        constexpr const char *base[]={"Display","Audio","Language and Locale","Clock","Startup and Shutdown","Auto-Play"};
        for (int i=0;i<6;++i) CHECK(rows[std::size_t(i)].label==base[i]);
        CHECK(rows[std::size_t(b==3?7:6)].label=="Remote Control");
        CHECK(rows.back().label=="System Info");
        if (b==3) CHECK(rows[6].label=="Auto-Downloads" && rows[8].label=="Console Name" && rows[9].label=="Connected Devices");
        walk_console_page(m);
        m.press(xb::A); // Display
        rows=m.system_entries();
        CHECK(rows.size()==std::size_t(b==0?7:b==3?10:9));
        if (b>0) CHECK(rows[6].label=="3D TV" && rows[7].label=="TV Size");
        if (b==3) CHECK(rows[8].label=="Closed Captioning");
        CHECK(m.system_value("Screen Resolution")=="1080p Original" && m.system_value("Screen Format")=="Widescreen");
        m.press(xb::B);
        m.viewer_item=1;m.press(xb::A);
        CHECK(m.system_page==Model::SystemAudio && m.system_entries().size()==std::size_t(b==3?4:3));
        m.press(xb::B);
        m.viewer_item=4;m.press(xb::A);
        rows=m.system_entries();
        CHECK(rows.size()==std::size_t(b<2?4:3));
        CHECK(rows[1].label==(b<2?"Welcome Channel":"Auto-Off"));
        m.press(xb::B);
        m.viewer_item=int(m.system_entries().size())-1;m.press(xb::A);
        CHECK(m.system_page==Model::SystemInfo);
        CHECK(m.system_value("System Info")=="Dashboard: 2.0."+std::to_string(builds[b])+".0");
        CHECK(m.system_entries().size()==8);
        m.press(xb::B);m.press(xb::B);
        CHECK(m.system_page==Model::SystemRoot && m.viewer_item==0 && m.system_history.empty());
        m.press(xb::B);
        CHECK(m.viewer==Viewer::None);
    }
}

static void console_settings_choices_are_live()
{
    Model m;
    CHECK(!m.clock_24_hour);
    m.viewer=Viewer::SystemSettings;
    m.press(xb::A);m.viewer_item=1;m.press(xb::A); // Audio opens without mutating sounds
    CHECK(m.sounds && m.system_page==Model::SystemAudio);
    m.viewer_item=1;m.press(xb::A);
    CHECK(m.system_page==Model::SystemSoundEffects && m.viewer_item==0);
    CHECK(m.system_description().find("You'll still hear game sounds, music, video, and voice communications.")!=std::string::npos);
    m.press(xb::Down);
    CHECK(m.press(xb::A).command==Command::SaveSettings && !m.sounds);
    CHECK(m.system_value("Audio")=="Sound Effects Disabled");
    m.press(xb::B);m.press(xb::A);
    CHECK(m.viewer_item==1); // selected value, not always the first option
    m.press(xb::Up);
    CHECK(m.press(xb::A).command==Command::SaveSettings && m.sounds);
    m.press(xb::B);m.press(xb::B);
    m.viewer_item=3;m.press(xb::A);m.viewer_item=1;m.press(xb::A);
    CHECK(m.system_page==Model::SystemTimeFormat && m.viewer_item==0);
    m.press(xb::Down);
    CHECK(m.press(xb::A).command==Command::SaveSettings && m.clock_24_hour);
    CHECK(m.system_value("Time Format")=="24-Hour Clock");
    m.press(xb::B);m.press(xb::A);
    CHECK(m.viewer_item==1);
    m.press(xb::Up);
    CHECK(m.press(xb::A).command==Command::SaveSettings && !m.clock_24_hour);
    CHECK(m.system_value("Time Format")=="12-Hour Clock");
    m.press(xb::B);m.press(xb::B);
    m.viewer_item=4;m.press(xb::A);
    CHECK(m.dimming); // opening Startup and Shutdown doesn't toggle
    m.viewer_item=2;m.press(xb::A); // NXE Auto-Off -> the actual dimming control
    CHECK(m.system_page==Model::SystemScreenSaver && m.viewer_item==0);
    CHECK(m.system_description().find("does not turn off the PS5")!=std::string::npos);
    m.press(xb::Down);
    CHECK(m.press(xb::A).command==Command::SaveSettings && !m.dimming);
    CHECK(m.system_value("Screen Saver")=="Screen Saver Disabled");
    CHECK(m.system_value("Auto-Off").empty()); // never claim a PS5 shutdown policy
    m.press(xb::B);m.press(xb::A);
    CHECK(m.viewer_item==1);
    m.press(xb::Up);
    CHECK(m.press(xb::A).command==Command::SaveSettings && m.dimming);
    CHECK(m.system_value("Remote Control").empty() && m.system_value("Language and Locale").empty());
    // Re-entering through SystemRoot discards old navigation history.
    m.system_page=Model::SystemRoot;m.viewer_item=0;m.press(xb::A);
    CHECK(m.system_history.size()==1);
    m.press(xb::B);
    CHECK(m.system_page==Model::SystemRoot && m.viewer_item==0);
}

static void memory_storage_flow_and_unavailable()
{
    constexpr int eras[]={1,2,3,3}, builds[]={9199,13604,15574,17559};
    for (int build=0;build<4;++build) {
        Model m;m.era=eras[build];m.dashboard_build=builds[build];
        m.viewer=Viewer::SystemSettings;m.viewer_item=2;
        CHECK(m.system_entries()[2].label==(m.era==3?"Storage":"Memory"));
        CHECK(m.press(xb::A).command==Command::ScanStorage);
        CHECK(m.system_page==Model::StorageDevices && m.system_page_title()=="Storage Devices");
        CHECK(m.storage_body()=="No storage devices found.");
        m.press(xb::A);m.press(xb::Y);m.press(xb::Down);
        CHECK(m.viewer_item==0 && m.system_page==Model::StorageDevices && !m.details);
        m.storage.devices={{"Internal Storage (/user)","/user",100000,34567,false},
                           {"Extended Storage (/mnt/usb0)","/mnt/usb0",200000,50000,true}};
        for (int i=0;i<11;++i) {
            StorageItem item;item.name="Song "+std::to_string(i);item.path="/data/nxe/music/song"+std::to_string(i)+".wav";
            item.category=StorageMusic;item.device=0;item.bytes=i==10?-1:i*123;
            m.storage.items.push_back(item);
        }
        StorageItem external;external.name="USB Song";external.device=1;external.category=StorageMusic;external.bytes=7;
        m.storage.items.push_back(external);
        StorageItem game;game.name="Installed title";m.storage.items.push_back(game);
        CHECK(m.system_entries().size()==3 && m.storage_body()=="Select a storage device.");
        CHECK(has_hint(m,xb::Y,"Device Options"));
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.storage_device==0 && m.system_page==Model::StorageSummary);
        CHECK(m.system_page_title()=="Device Summary");
        CHECK(m.storage_body().find("Capacity: 98 KB")!=std::string::npos);
        CHECK(m.storage_body().find("Free Space: 34 KB")!=std::string::npos);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.system_page==Model::StorageOptions && m.system_page_title()=="Device Options");
        const auto actions=m.system_entries();
        CHECK(actions.size()==5);
        const char *device_actions[]={"Rename","Format","Clear System Cache","Transfer Content","Delete System Update"};
        for (int i=0;i<int(actions.size());++i) {
            CHECK(i<5 && actions[size_t(i)].label==device_actions[i]);
            m.viewer_item=i;
            CHECK(m.press(xb::A).command==Command::None);
            CHECK(m.details && m.details_title==actions[size_t(i)].label && m.details_text=="Not available on PS5 yet.");
            CHECK(m.storage.items.size()==13 && m.storage.devices.size()==2);
            m.press(xb::B);
            CHECK(!m.details && m.system_page==Model::StorageOptions && m.viewer_item==i);
        }
        m.press(xb::B);
        CHECK(m.system_page==Model::StorageSummary && m.viewer_item==1);
        m.press(xb::Up);m.press(xb::A);
        CHECK(m.system_page==Model::StorageCategories && m.system_entries().size()==9);
        CHECK(m.system_entries()[0].label==(m.era==3?"Games and Apps":"Games"));
        CHECK(m.system_entries()[1].label==(build==3?"Profiles":"Gamer Profiles"));
        CHECK(m.system_entries()[3].label=="Videos" && m.system_entries()[6].label=="Avatar Items");
        m.press(xb::Y);
        CHECK(m.details && m.details_title=="Transfer Content" && m.details_text=="Not available on PS5 yet.");
        m.press(xb::B);
        m.viewer_item=StorageProfiles;m.press(xb::A);
        CHECK(m.system_page==Model::StorageItems && m.system_entries().empty());
        CHECK(m.storage_body()=="No items found.");
        m.press(xb::A);m.press(xb::X);m.press(xb::Right);m.press(xb::Down);
        CHECK(!m.details && m.viewer_item==0 && !has_hint(m,xb::X,"Delete"));
        m.press(xb::B);CHECK(m.viewer_item==StorageProfiles);
        m.viewer_item=StorageMusic;m.press(xb::A);
        CHECK(m.system_page_title()=="Select Item" && m.system_entries().size()==11);
        CHECK(m.storage_row_size(0)=="0 KB" && m.storage_row_size(10)=="Unknown");
        m.press(xb::Left);CHECK(m.viewer_item==0);
        m.press(xb::Right);CHECK(m.viewer_item==1);
        m.press(xb::Right);CHECK(m.viewer_item==1); // no horizontal row wrapping
        m.press(xb::Up);CHECK(m.viewer_item==1); // top row keeps its column
        m.press(xb::Down);CHECK(m.viewer_item==3);
        m.press(xb::Up);m.press(xb::Left);CHECK(m.viewer_item==0);
        for (int i=0;i<8;++i) m.press(xb::Down);
        CHECK(m.viewer_item==10); // beyond one rendered page, odd final grid row
        m.press(xb::Right);CHECK(m.viewer_item==10);
        m.press(xb::X);
        CHECK(m.details && m.details_title=="Delete" && m.details_text=="Not available on PS5 yet.");
        m.press(xb::A);CHECK(!m.details && m.viewer_item==10);
        m.press(xb::A);
        CHECK(m.system_page==Model::StorageDetails && m.system_page_title()=="Item Options");
        CHECK(m.storage_body().find("File Size: Unknown")!=std::string::npos);
        CHECK(m.storage_body().find("Owner: Unknown")!=std::string::npos);
        CHECK(m.storage_body().find("Created: Unknown")!=std::string::npos);
        CHECK(m.storage_body().find("Location: /data/nxe/music/song10.wav")!=std::string::npos);
        const char *item_actions[]={"Copy","Move","Delete"};
        for (int i=0;i<3;++i) {
            m.viewer_item=i;CHECK(m.press(xb::A).command==Command::None);
            CHECK(m.details && m.details_title==item_actions[i] && m.details_text=="Not available on PS5 yet.");
            m.press(xb::B);CHECK(m.system_page==Model::StorageDetails && m.viewer_item==i);
        }
        m.press(xb::B);CHECK(m.system_page==Model::StorageItems && m.viewer_item==10);
        m.press(xb::B);CHECK(m.system_page==Model::StorageCategories && m.viewer_item==StorageMusic);
        m.press(xb::B);CHECK(m.system_page==Model::StorageSummary && m.viewer_item==0);
        m.press(xb::B);CHECK(m.system_page==Model::StorageDevices && m.viewer_item==1);
        m.press(xb::Y);CHECK(m.system_page==Model::StorageOptions);
        m.press(xb::B);CHECK(m.system_page==Model::StorageDevices && m.viewer_item==1);
        // Physical device filters never assign an unknown game's payload to a guessed drive.
        m.press(xb::Down);m.press(xb::A);m.press(xb::A);
        m.viewer_item=StorageMusic;m.press(xb::A);
        CHECK(m.system_entries().size()==1 && m.system_entries()[0].label=="USB Song");
        m.press(xb::B);m.press(xb::B);m.press(xb::B);
        m.viewer_item=0;m.press(xb::A);m.press(xb::A);m.press(xb::A);
        CHECK(m.storage_device==-1 && m.system_entries().size()==1 && m.system_entries()[0].label=="Installed title");
        m.press(xb::A);CHECK(m.storage_body().find("Location: Unknown")!=std::string::npos);
        m.press(xb::B);m.press(xb::B);m.press(xb::B);m.press(xb::B);m.press(xb::B);
        CHECK(m.system_page==Model::SystemRoot && m.viewer_item==2 && m.system_history.empty());
        CHECK(m.storage.items.size()==13 && m.storage.items[10].bytes==-1);
        CHECK(m.press(xb::A).command==Command::ScanStorage); // refresh on re-entry
        m.press(xb::B);m.press(xb::B);CHECK(m.viewer==Viewer::None);
    }
    CHECK(storage_size(-1)=="Unknown" && storage_size(0)=="0 KB" && storage_size(1234567890123LL)=="1149.8 GB" && storage_size(47185920)=="45 MB");
}

static void network_settings_pages_by_era()
{
    const int eras[]={0,0,1,2,3,3}, builds[]={1888,6770,9199,13604,15574,17559};
    for (int i=0;i<6;++i) {
        Model m;m.era=eras[i];m.dashboard_build=builds[i];
        m.channel=MyXbox;m.selected[MyXbox]=3;
        if (m.era) {m.viewer=Viewer::SystemSettings;m.viewer_item=3;}
        CHECK(m.press(xb::A).command==Command::ReadNetwork);
        CHECK(m.viewer==Viewer::SystemSettings && m.system_page==Model::NetworkMain);
        CHECK(m.system_page_title()=="Network Settings" && m.viewer_item==0);
        CHECK(m.network.address.empty() && m.network.mac.empty() && m.network.signal==-1);
        CHECK(m.system_entries()[0].label==(builds[i]==1888 || builds[i]==17559?"Test Xbox Live Connection":"Test Xbox LIVE Connection"));
        CHECK(m.system_entries()[1].label==(m.era==0?"Edit Settings":"Configure Network"));
        CHECK(m.hints()[0].button==xb::A && m.hints()[1].button==xb::B);
        const unsigned first=m.network_generation;
        CHECK(m.press(xb::A).command==Command::TestNetwork);
        CHECK(m.system_page==Model::NetworkTesting && m.network_test.running && m.network_generation>first);
        CHECK(m.system_entries()[0].label==(builds[i]==1888?"Test Xbox Live Connection":m.era==0?"Restart Tests":"Restart Test"));
        CHECK(m.system_page_title()==(builds[i]==1888?"Test Xbox Live Connection":m.era==0?"Test Xbox LIVE Connection":"Testing Connection"));
        const auto depth=m.system_history.size();const unsigned generation=m.network_generation;
        m.network_test.steps[0]=NetworkResult::Failed;
        CHECK(m.press(xb::A).command==Command::TestNetwork);
        CHECK(m.network_generation>generation && m.system_history.size()==depth);
        CHECK(m.network_test.steps[0]==NetworkResult::Idle && m.network_test.steps[3]==NetworkResult::Unavailable);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.details && m.details_title=="More Info" && m.details_text.find("example.com:443")!=std::string::npos);
        m.press(xb::B);m.press(xb::Up);
        m.network_test.steps[0]=NetworkResult::Running;
        m.press(xb::B);
        CHECK(m.system_page==Model::NetworkMain && m.viewer_item==0 && !m.network_test.running);
        CHECK(m.network_test.steps[0]==NetworkResult::Idle);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.details && m.details_text=="Not available on PS5 yet.");m.press(xb::B);
        CHECK(m.system_page==Model::NetworkMain && m.viewer_item==1);
        m.press(xb::Down);m.press(xb::A);
        CHECK(m.system_page==Model::NetworkDetails && m.viewer_item==0);
        m.press(xb::A);CHECK(m.system_page==Model::NetworkIP);
        m.network.address="192.0.2.17";m.network.mask="255.255.255.0";
        CHECK(m.network_value("IP Address")=="192.0.2.17" && m.network_value("Subnet Mask")=="255.255.255.0");
        CHECK(m.network_value("Gateway").empty());
        m.press(xb::A);CHECK(m.details && m.details_title=="IP Address");m.press(xb::B);
        CHECK(m.network.address=="192.0.2.17");
        m.press(xb::B);m.press(xb::Down);m.press(xb::A);
        CHECK(m.system_page==Model::NetworkDNS);
        m.network.dns="192.0.2.1";CHECK(m.network_value("Primary DNS Server")=="192.0.2.1");
        CHECK(m.network_value("Secondary DNS Server").empty());
        m.press(xb::B);CHECK(m.system_page==Model::NetworkDetails && m.viewer_item==1);
        m.press(xb::B);CHECK(m.system_page==Model::NetworkMain && m.viewer_item==2);
        m.press(xb::Down);m.press(xb::A);CHECK(m.system_page==Model::NetworkConsole);
        m.network.mac="00-11-22-33-44-55";m.network.hostname="test-host";
        m.network.medium=NetworkMedium::Wired;
        CHECK(m.network_value("Host Name")=="test-host");
        CHECK(m.network_value(m.system_entries()[1].label)==m.network.mac);
        CHECK(m.system_entries()[1].label==(m.era<2?"Wired MAC Address":"MAC Address"));
        m.network.medium=NetworkMedium::Wireless;
        CHECK(m.system_entries()[1].label=="MAC Address" && m.network_value("Network Adapter")=="Wireless");
        m.viewer_item=2;m.press(xb::A);CHECK(m.details && m.details_title=="Wireless Information");m.press(xb::B);
        m.press(xb::B);m.press(xb::Down);m.press(xb::A);
        CHECK(m.details && m.details_text=="Not available on PS5 yet.");m.press(xb::B);
        m.press(xb::B);
        CHECK(m.system_page==Model::SystemRoot && m.system_history.empty());
        if (m.era==0) CHECK(m.viewer==Viewer::None && m.current()==3);
        else CHECK(m.viewer==Viewer::SystemSettings && m.viewer_item==3);
        // Refresh Homebrew remains available as an ordinary Guide row.
        bool refresh=false;
        for (const auto &entry:m.host_blade_entries(BladeApps)) if (entry.action==GuideAction::Refresh) {
            refresh=true;CHECK(m.perform_guide_entry(entry).command==Command::Refresh);
        }
        CHECK(refresh);
    }
}

static void network_test_steps_with_fake_probe()
{
    using R=NetworkResult;
    for (int failure=-1;failure<3;++failure) {
        NetworkTest test;test.start();std::vector<int> calls;std::vector<NetworkTest> states;
        auto fake=[&](int step) {calls.push_back(step);return step==failure?R::Failed:R::Passed;};
        auto publish=[&](const NetworkTest &state) {states.push_back(state);};
        while (test.running) test.advance(fake,publish);
        CHECK((calls==std::vector<int>{0,1,2}));CHECK(states.size()==6 && test.next==3);
        for (int step=0;step<3;++step) {
            CHECK(states[size_t(step*2)].steps[size_t(step)]==R::Running);
            CHECK(test.steps[size_t(step)]==(step==failure?R::Failed:R::Passed));
        }
        for (const auto &state:states) CHECK(state.steps[3]==R::Unavailable);
        test.advance(fake,publish);CHECK(calls.size()==3 && states.size()==6);
        test.start();CHECK(test.running && test.next==0 && test.steps[0]==R::Idle);
    }
    NetworkTest unknown;unknown.start();
    auto missing=[](int) {return R::Unavailable;};auto ignore=[](const NetworkTest &) {};
    while (unknown.running) unknown.advance(missing,ignore);
    for (auto step:unknown.steps) CHECK(step==R::Unavailable);
}


static void welcome_channel_and_setting()
{
    for (int era:{1,2}) {
        Model m;m.era=era;m.dashboard_build=era==1?9199:13604;
        CHECK(m.welcome_available() && m.channel==MyXbox && MyXbox==4);
        CHECK(m.channel_rank(Welcome)+1==m.channel_rank(MyXbox));
        const Model hub=m;
        const auto up=m.press(xb::Up);CHECK(m.channel==Welcome && m.count()==(era==1?4:6));
        CHECK(sound_for(hub,m,xb::Up,up)==Sound::ChannelUp);
        m.press(xb::A);CHECK(m.viewer==Viewer::WelcomePages && !m.welcome_confirm);
        CHECK(m.viewer_count()==(era==1?9:5));
        CHECK(m.welcome_key()==(era==1?"WhatsNewAvatarGearScene":"XboxBasicsGettingAroundScene"));
        for (int i=0;i<20;++i) m.press(xb::RB);
        CHECK(m.viewer_item==m.viewer_count()-1);
        m.press(xb::Guide);CHECK(m.guide);m.press(xb::B);
        CHECK(!m.guide && m.viewer==Viewer::WelcomePages);
        for (int i=0;i<20;++i) m.press(xb::LB);
        CHECK(m.viewer_item==0);m.press(xb::B);
        CHECK(m.channel==Welcome && m.viewer==Viewer::None);
        if (era==1) {
            m.press(xb::Right);m.press(xb::A);
            CHECK(!m.welcome_hot && m.viewer_count()==8 && m.welcome_key()=="XboxBasicsGettingAroundScene");
            m.press(xb::B);m.press(xb::Right);m.press(xb::A);
            CHECK(m.details && m.details_text=="Not available on PS5 yet.");m.press(xb::B);
        } else {
            for (int i=1;i<5;++i) {
                m.selected[Welcome]=i;m.press(xb::A);
                CHECK(m.details && m.details_text=="Not available on PS5 yet.");m.press(xb::B);
            }
        }
        m.press(xb::RT);m.press(xb::A);
        CHECK(m.welcome_confirm && !m.welcome_yes && m.viewer_count()==2);
        CHECK(m.press(xb::A).command==Command::None && m.welcome_visible);
        m.press(xb::A);m.press(xb::Left);m.press(xb::B);
        CHECK(m.welcome_visible && !m.welcome_confirm);
        m.press(xb::A);m.press(xb::Left);
        CHECK(m.press(xb::A).command==Command::SaveSettings);
        CHECK(!m.welcome_available() && m.channel==MyXbox && m.viewer==Viewer::None);
        Model loaded;loaded.load_welcome_setting(m.saved_welcome_setting());CHECK(!loaded.welcome_visible);
        loaded.load_welcome_setting("welcome_visible=garbage");CHECK(!loaded.welcome_visible);
        loaded.load_welcome_setting("welcome_visible=1\r\n");CHECK(loaded.welcome_visible);
        m.press(xb::Up);CHECK(m.channel==Apps);m.press(xb::Down);CHECK(m.channel==MyXbox);
        m.viewer=Viewer::SystemSettings;m.system_page=Model::SystemStartup;m.viewer_item=1;
        CHECK(m.system_entries()[1].action==GuideAction::ConsoleSettings);
        m.press(xb::A);CHECK(m.system_page==Model::SystemWelcome && m.viewer_item==1);
        CHECK(m.system_value("Welcome Channel")=="Hide");
        m.press(xb::Up);CHECK(m.press(xb::A).command==Command::SaveSettings && m.welcome_available());
        CHECK(m.system_value("Welcome Channel")=="Show");
        m.press(xb::B);CHECK(m.system_page==Model::SystemStartup && m.viewer_item==1);
        m.viewer=Viewer::None;m.press(xb::Up);CHECK(m.channel==Welcome);
        // Turning it off while its channel is behind Settings leaves Settings open.
        m.viewer=Viewer::SystemSettings;m.system_page=Model::SystemWelcome;m.viewer_item=1;
        m.press(xb::A);CHECK(m.channel==MyXbox && m.viewer==Viewer::SystemSettings);
    }
    for (int era:{0,3}) {Model m;m.era=era;CHECK(!m.welcome_available());}
    Model content;
    content.load_welcome_content({"bad row","XboxBasicsGettingAroundScene\tHeading\timage.png\tFirst\\nSecond\r"});
    content.viewer=Viewer::WelcomePages;
    CHECK(content.welcome_page().title=="Heading" && content.welcome_page().body=="First\nSecond");
    CHECK(content.welcome_page().art=="image.png");
    content.load_welcome_content({});CHECK(content.welcome_page().body=="Not available on PS5 yet.");
}

static void initial_setup_per_era()
{
    const int eras[]={0,0,1,2,3,3}, builds[]={1888,6770,9199,13604,15574,17559};
    for (int i=0;i<6;++i) {
        Model m;m.era=eras[i];m.dashboard_build=builds[i];
        if (m.era==0) {
            m.selected[MyXbox]=int(m.blades_items(MyXbox).size())-1;
            CHECK(m.blades_items(MyXbox).back().action==GuideAction::InitialSetup);m.press(xb::A);
        } else {
            m.viewer=Viewer::SystemSettings;
            const auto entries=m.system_entries();const auto setup=std::find_if(entries.begin(),entries.end(),[](const GuideEntry&entry){return entry.action==GuideAction::InitialSetup;});
            CHECK(setup!=entries.end());m.viewer_item=int(setup-entries.begin());m.press(xb::A);
        }
        CHECK(m.viewer==Viewer::InitialSetup && m.setup_step==Model::SetupIntro && m.viewer_count()==5);
        const Viewer previous=m.setup_previous_viewer;const int previous_item=m.setup_previous_item;
        m.press(xb::A);CHECK(m.setup_step==Model::SetupResolution && m.system_value("Screen Resolution")=="1080p Original");
        const bool dim=m.dimming, clock=m.clock_24_hour;
        CHECK(m.press(xb::Right).command==Command::None && dim==m.dimming && clock==m.clock_24_hour);
        m.press(xb::A);CHECK(m.setup_step==Model::SetupAutoOff);
        CHECK(m.press(xb::Right).command==Command::SaveSettings && !m.dimming);
        CHECK(m.press(xb::Left).command==Command::SaveSettings && m.dimming);
        m.press(xb::A);CHECK(m.setup_step==Model::SetupClock);
        CHECK(m.press(xb::Right).command==Command::SaveSettings && m.clock_24_hour);
        CHECK(m.press(xb::Left).command==Command::SaveSettings && !m.clock_24_hour);
        m.press(xb::Guide);m.press(xb::B);CHECK(m.viewer==Viewer::InitialSetup && !m.guide);
        Model before=m;const auto effect=m.press(xb::A);
        CHECK(m.setup_step==Model::SetupDone && m.welcome_key()=="setup/4");
        CHECK(sound_for(before,m,xb::A,effect)==Sound::SetupComplete);
        m.press(xb::A);CHECK(m.viewer==previous && m.viewer_item==previous_item);
        CHECK(m.dashboard_build==builds[i] && m.era==eras[i]);
        m.open_setup();m.press(xb::A);m.press(xb::B);CHECK(m.setup_step==Model::SetupIntro);
        m.press(xb::B);CHECK(m.viewer==previous && m.viewer_item==previous_item);
        bool picker=false;
        for (const auto &entry:m.host_blade_entries(BladeSettings)) if (entry.action==GuideAction::CycleEra) picker=true;
        CHECK(picker);
    }
}

static void metro_settings_tiles_and_navigation()
{
    for (int build:{15574,17559}) {
        Model m;m.era=3;m.dashboard_build=build;m.select_metro_tab(7);
        CHECK(m.metro_page_size()==8 && m.count()==(build==15574?10:8));
        CHECK(m.card(0).title=="System" && m.card(5).title=="Privacy");
        CHECK(m.card(1).title==(build==15574?"Preferences":"Profile"));
        CHECK(m.card(2).title==(build==15574?"Profile":"Preferences"));
        std::set<int> reached;std::set<std::pair<int,int>> seen;std::deque<Model> pending{m};
        while (!pending.empty()) {
            Model at=pending.front();pending.pop_front();
            if (!seen.insert({at.current(),at.metro_focus}).second) continue;
            if (at.metro_focus>=0) reached.insert(at.current());
            for (auto button:{xb::Up,xb::Down,xb::Left,xb::Right}) {
                Model next=at;next.press(button);if (next.metro_tab==7) pending.push_back(next);
            }
        }
        CHECK(int(reached.size())==m.count());
        for (int i=0;i<m.count();++i) {
            m.viewer=Viewer::None;m.details=false;m.selected[MyXbox]=i;m.metro_focus=i%8;
            const auto c=m.current_card();const auto e=m.press(xb::A);
            if (i==0) CHECK(m.viewer==Viewer::SystemSettings && m.system_page==Model::SystemRoot);
            else if (c.title=="Profile") CHECK(m.viewer==Viewer::Profile);
            else if (c.title=="Turn Off") CHECK(e.command==Command::ExitToPs5 && m.viewer==Viewer::None);
            else CHECK(m.details && m.details_text=="Not available on PS5 yet.");
            m.press(xb::B);
        }
        // Focus back on a tile: B on the last tile leaves it on the tab row, where LT changes tab.
        m.viewer=Viewer::None;m.details=false;m.selected[MyXbox]=0;m.metro_focus=0;m.press(xb::LT);m.press(xb::Down);
        CHECK(m.current()==4);m.press(xb::Up);CHECK(m.current()==0);
        m.press(xb::Up);CHECK(m.metro_focus==-1);m.press(xb::Down);CHECK(m.metro_focus==0);
        m.press(xb::RT);CHECK(m.current()==m.count()-1 && m.metro_focus==(m.count()-1)%8);
        const int selected=m.current();m.set_apps({});CHECK(m.current()==selected && m.metro_focus==selected%8);
        m.press(xb::LB);m.press(xb::RB);CHECK(m.metro_tab==7 && m.current()==selected);
    }
}

static void profile_achievements_navigation_and_empty_states()
{
    for (int build:{1888,6770,9199,13604,15574,17559}) {
        Model m;m.dashboard_build=build;m.era=build<=6770?0:build==9199?1:build==13604?2:3;
        m.perform_guide_entry({"Profile",GuideAction::Profile});
        const auto rows=m.profile_entries();
        CHECK(rows.size()==4 && rows[0].label=="Achievements" && rows[1].label=="Customize Avatar");
        CHECK(rows[2].label=="Change Theme" && rows[3].label=="Switch Profile");
        CHECK(rows[3].action==GuideAction::Unavailable);
        Model before=m;const auto opened=m.press(xb::A);
        CHECK(m.viewer==Viewer::Achievements && m.viewer_count()==0);
        CHECK(sound_for(before,m,xb::A,opened)==Sound::Select);
        CHECK(!has_hint(m,xb::A,"Select") && has_hint(m,xb::B,"Back"));
        for (auto button:{xb::A,xb::Up,xb::Down,xb::LB,xb::RB,xb::LT,xb::RT}) {
            CHECK(m.press(button).command==Command::None && m.viewer_item==0 && m.achievement_game.empty());
        }
        CHECK(std::string(kAchievementsEmpty)=="Your achievements will be listed here. Unlock achievements by playing games.");
        m.press(xb::Guide);CHECK(m.guide);m.press(xb::B);CHECK(!m.guide && m.viewer==Viewer::Achievements);
        before=m;const auto back=m.press(xb::B);
        CHECK(m.viewer==Viewer::Profile && m.viewer_item==0 && sound_for(before,m,xb::B,back)==Sound::Back);
        m.viewer_item=3;m.press(xb::A);
        CHECK(m.details && m.details_text=="Not available on PS5 yet.");m.press(xb::B);
        m.avatar_editable=true;m.viewer_item=1;
        CHECK(m.press(xb::A).command==Command::AvatarEditStart && m.viewer==Viewer::AvatarEdit);
        m.press(xb::B);CHECK(m.viewer==Viewer::Avatar);
        if (m.era==0) {
            m.viewer=Viewer::None;m.channel=Games;m.selected[Games]=1;
            m.press(xb::A);CHECK(m.viewer==Viewer::Achievements && !m.details);
            m.press(xb::B);CHECK(m.viewer==Viewer::None && m.current()==1);
            m.selected[Games]=0;m.press(xb::A);CHECK(m.viewer==Viewer::Profile);
            m.press(xb::B);CHECK(m.viewer==Viewer::None && m.current()==0);
        }
    }
}
static void achievements_fake_source_counts_points_and_navigation()
{
    Model m;
    m.set_games({{"One","","PPSA00001",""},{"Two","","PPSA00002",""},{"Unplayed","","PPSA00003",""}});
    m.load_recent({"title:PPSA00002","title:PPSA00001","title:MISSING"});
    // Fake read-only source: only verified complete catalogues may set known=true.
    const std::string one="title:PPSA00001",two="title:PPSA00002";
    m.achievement_source[one]={true,{
        {"Bronze","First step","2026-10-28",TrophyGrade::Bronze,true,false},
        {"Silver","Second step","",TrophyGrade::Silver,false,false},
        {"Gold secret","Hidden description","",TrophyGrade::Gold,false,true},
        {"Platinum","All done","2026-10-28",TrophyGrade::Platinum,true,true}}};
    CHECK(trophy_points(TrophyGrade::Bronze)==15 && trophy_points(TrophyGrade::Silver)==30);
    CHECK(trophy_points(TrophyGrade::Gold)==90 && trophy_points(TrophyGrade::Platinum)==300);
    auto p=m.achievement_progress(one);
    CHECK(p.known && p.earned==2 && p.total==4 && p.points==315 && p.possible==435 && !p.complete());
    CHECK(m.achievement_counts(one)=="2 / 4" && m.achievement_counts(one,true)=="315 / 435");
    CHECK(!m.achievement_progress(two).known && m.achievement_counts(two)=="-- / --");
    CHECK(m.achievement_counts(two,true)=="-- / --");
    const auto &entries=m.achievement_source[one].entries;
    CHECK(entries[0].art()==0 && entries[1].art()==1 && entries[2].art()==2 && entries[3].art()==0);
    CHECK(entries[2].display_name()=="Secret" && entries[2].display_description().find("Hidden description")==std::string::npos);
    CHECK(entries[3].display_name()=="Platinum" && entries[3].display_description()=="All done");
    CHECK(entries[0].display_date()=="2026-10-28" && entries[1].display_date().empty());
    const auto games=m.achievement_games();
    CHECK(games.size()==2 && games[0].key==two && games[1].key==one);
    m.viewer=Viewer::Profile;m.press(xb::A);
    CHECK(m.viewer_count()==2 && has_hint(m,xb::A,"Select"));
    m.press(xb::A);CHECK(m.achievement_game==two && m.viewer_count()==0);
    m.press(xb::A);CHECK(m.viewer_item==0);m.press(xb::B);CHECK(m.viewer_item==0 && m.achievement_game.empty());
    m.press(xb::Down);m.press(xb::A);CHECK(m.achievement_game==one && m.viewer_count()==4 && m.viewer_item==0);
    m.press(xb::RT);CHECK(m.viewer_item==3);m.press(xb::Down);CHECK(m.viewer_item==3);
    m.press(xb::LT);CHECK(m.viewer_item==0);m.press(xb::RB);CHECK(m.viewer_item==3);
    m.press(xb::LB);CHECK(m.viewer_item==0);
    m.press(xb::Guide);m.press(xb::B);CHECK(m.achievement_game==one && m.viewer==Viewer::Achievements);
    m.press(xb::B);CHECK(m.achievement_game.empty() && m.viewer_item==1);
    m.press(xb::B);CHECK(m.viewer==Viewer::Profile && m.viewer_item==0);
    for (auto &entry:m.achievement_source[one].entries) entry.earned=true;
    p=m.achievement_progress(one);CHECK(p.complete() && p.points==435);
    m.achievement_source[two]={true,{}};
    CHECK(m.achievement_counts(two)=="0 / 0" && !m.achievement_progress(two).complete());
    m.achievement_source[two]={false,{{"Unverified","","",TrophyGrade::Gold,true,false}}};
    CHECK(!m.achievement_title(two) && m.achievement_counts(two)=="-- / --");
    m.viewer=Viewer::Library;m.viewer_item=7;m.viewer_full=true;m.open_achievements();
    m.press(xb::RT);m.press(xb::A);m.achievement_source.clear();m.press(xb::Down);
    CHECK(m.viewer_count()==0 && m.viewer_item==0);
    m.set_games({});m.press(xb::B);CHECK(m.viewer_item==0 && m.viewer_count()==0);
    m.press(xb::B);CHECK(m.viewer==Viewer::Library && m.viewer_item==7 && m.viewer_full);
}

static void album_info_edits_and_storage()
{
    Model m;m.viewer=Viewer::Music;m.music_view=1;m.music_group=0;m.songs=6;m.viewer_item=3;
    auto e=m.press(xb::A);
    CHECK(e.command==Command::MusicEdit && e.app==int(MusicOp::OpenAlbumInfo));
    CHECK(m.music_page==Model::MusicAlbumInfo && m.viewer_count()==4 && has_hint(m,xb::B,"Cancel"));
    m.music_album_original=m.music_album_fields={"Album","","Rock"};
    m.press(xb::A);CHECK(m.music_album_field==0);
    m.press(xb::X);m.press(xb::A);CHECK(m.music_album_input=="AlbuA");
    m.press(xb::B);CHECK(m.music_album_fields[0]=="Album");
    m.press(xb::A);m.music_album_input="Renamed";m.press(xb::Y);
    CHECK(m.music_album_fields[0]=="Renamed" && m.music_album_field==-1 && m.music_album_changed==1);
    m.viewer_item=3;e=m.press(xb::A);
    CHECK(e.app==int(MusicOp::SaveAlbumInfo) && m.music_page==Model::MusicAlbumInfo);
    // Failure leaves the editor/draft available for retry; B discards the draft.
    m.album_save_result(false);CHECK(m.details && m.music_album_fields[0]=="Renamed");m.press(xb::B);m.press(xb::B);
    CHECK(m.music_page==Model::MusicLibrary && m.music_album_fields==m.music_album_original && m.viewer_item==3);
    m.press(xb::A);m.press(xb::A);m.music_album_input="\xc3\xa9";m.press(xb::X);
    CHECK(m.music_album_input.empty());
    m.music_album_input=std::string(128,'x');m.press(xb::A);CHECK(m.music_album_input.size()==128);
    m.music_album_key=0;m.press(xb::Left);CHECK(m.music_album_key==int(std::string(Model::music_album_keys).size())-1);
    m.press(xb::Right);CHECK(m.music_album_key==0);
    m.press(xb::B);m.music_album_fields[0]="Saved";m.album_save_result(true);
    CHECK(m.music_page==Model::MusicLibrary && m.music_album_original[0]=="Saved" && m.viewer_item==3);
    musicmeta::Edits edits;
    const musicmeta::Fields before{"Album","","Rock"},after{"Renamed","",""};
    edits=musicmeta::changed(edits,{"/music/a.mp3","/music/b.mp3"},before,after);
    CHECK(edits.size()==2 && edits.at("/music/a.mp3").mask==5);
    const auto applied=musicmeta::apply({"Album","Different artist","Rock"},edits.at("/music/b.mp3"));
    CHECK(applied[0]=="Renamed" && applied[1]=="Different artist" && applied[2].empty());
    CHECK(musicmeta::changed({}, {"/music/a.mp3"},before,before).empty());
    const auto cleared=musicmeta::changed({}, {"/music/a.mp3"},before,before,2);
    CHECK(musicmeta::apply({"Album","Mixed artist","Rock"},cleared.at("/music/a.mp3"))[1].empty());
    edits["/music/quote\"\\.mp3"]={musicmeta::Fields{"A\"B","line\nartist",""},7};
    musicmeta::Edits loaded;
    const auto saved=musicmeta::encode(edits);
    CHECK(musicmeta::decode(saved,loaded) && musicmeta::encode(loaded)==saved);
    CHECK(!musicmeta::decode("EXBOX-ALBUM-1\n\"/music/a\" 8 \"x\" \"y\" \"z\"",loaded));
    CHECK(!musicmeta::decode("EXBOX-ALBUM-1\n\"/music/a\" 1 \"truncated",loaded));
    CHECK(musicmeta::encode(loaded)==saved);
    // Only this test's sidecar is written; media paths above are never opened.
    const std::string file="build/test-album-info.txt";
    CHECK(musicmeta::save(file,edits));
    std::ifstream input(file,std::ios::binary);
    const std::string disk((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    input.close();
    CHECK(musicmeta::decode(disk,loaded) && musicmeta::encode(loaded)==saved);
    CHECK(!musicmeta::save(file+"/invalid-child",{}));
    std::ifstream unchanged(file,std::ios::binary);
    CHECK(std::string((std::istreambuf_iterator<char>(unchanged)),std::istreambuf_iterator<char>())==saved);
}

// T19: expected visible rows transcribed from each original signed-in HUD scene.
static void tabbed_guide_authored_rows_and_bindings()
{
    using A=GuideAction;
    const int builds[]={9199,13604,15574,17559};
    const std::vector<std::vector<GuideEntry>> expected[]={
        { // 9199: Marketplace, Games, Home, Media, Settings
            {{"Apps",A::Apps,-1,"btnGameMarketplace"},{"Media",A::Media,-1,"btnVideoMarketplace"},{"My Avatar",A::Avatar,-1,"btnAvatarMarketplace"},{"Refresh Homebrew",A::Refresh,-1,"btnActiveDownloads"},{"Library Summary",A::LibrarySummary,-1,"btnRedeemCode"}},
            {{"Achievements",A::Achievements,-1,"btnAchievements"},{"Refresh Games & Homebrew",A::Refresh,-1,"btnAvatarAwards"},{"Game Library",A::AllGames,-1,"btnGamesLibrary"}},
            {{"Xbox Dashboard",A::MyXbox,-1,"btnDashboard"},{"Quick Launch",A::RecentGames,-1,"btnJumpIn"},{"Notifications",A::NotificationHistory,-1,"btnConnectToLive"},{"Disc in Tray",A::Unavailable,-1,"btnDiscInTray"}},
            {{"Video Library",A::Videos,-1,"btnVideoLibrary"},{"Music Library",A::Music,-1,"btnMusicLibrary"},{"Picture Library",A::Pictures,-1,"btnPictureLibrary"},{"Record Screen (Create)",A::RecordScreen,-1,"btnMCX"}},
            {{"Profile",A::Profile,-1,"btnProfile"},{"Notifications",A::NotificationHistory,-1,"btnNotifications"},{"Themes",A::SelectTheme,-1,"btnPersonalSettings"},{"Controller Map",A::ControllerMap,-1,"btnFamilySettings"},{"System Settings",A::SystemSettings,-1,"btnSystemSettings"},{"Avatar Settings",A::Avatar,-1,"btnAccountManagement"},{"Turn Off Console",A::ExitToPs5,-1,"btnShutdown"}},
        },
        { // 13604: Marketplace, Games, Home, Media, Settings
            {{"Refresh Homebrew",A::Refresh,-1,"btnActiveDownloads"},{"Library Summary",A::LibrarySummary,-1,"btnRedeemCode"},{"Apps",A::Apps,-1,"btnGameMarketplace"},{"Media",A::Media,-1,"btnVideoMarketplace"},{"My Avatar",A::Avatar,-1,"btnAvatarMarketplace"}},
            {{"Achievements",A::Achievements,-1,"btnAchievements"},{"Refresh Games & Homebrew",A::Refresh,-1,"btnAvatarAwards"},{"Quick Launch",A::RecentGames,-1,"btnQuickLaunch"},{"Game Library",A::AllGames,-1,"btnGamesLibrary"}},
            {{"Xbox Dashboard",A::MyXbox,-1,"btnDashboard"},{"Notifications",A::NotificationHistory,-1,"btnConnectToLive"},{"Disc in Tray",A::Unavailable,-1,"btnDiscInTray"}},
            {{"Video Library",A::Videos,-1,"btnVideoLibrary"},{"Music Library",A::Music,-1,"btnMusicLibrary"},{"Picture Library",A::Pictures,-1,"btnPictureLibrary"},{"Record Screen (Create)",A::RecordScreen,-1,"btnMCX"}},
            {{"Profile",A::Profile,-1,"btnProfile"},{"Themes",A::SelectTheme,-1,"btnPersonalSettings"},{"Controller Map",A::ControllerMap,-1,"btnFamilySettings"},{"System Settings",A::SystemSettings,-1,"btnSystemSettings"},{"Avatar Settings",A::Avatar,-1,"btnAccountManagement"},{"Turn Off Console",A::ExitToPs5,-1,"btnShutdown"}},
        },
        { // 15574: Marketplace, Games, Home, Media, Settings
            {{"Refresh Homebrew",A::Refresh,-1,"btnActiveDownloads"},{"Library Summary",A::LibrarySummary,-1,"btnRedeemCode"},{"Apps",A::Apps,-1,"btnGameMarketplace"},{"My Avatar",A::Avatar,-1,"btnAvatarMarketplace"}},
            {{"Achievements",A::Achievements,-1,"btnAchievements"},{"Refresh Games & Homebrew",A::Refresh,-1,"btnAvatarAwards"},{"Quickplay",A::RecentGames,-1,"btnQuickLaunch"},{"My Games",A::AllGames,-1,"btnGamesLibrary"}},
            {{"Xbox Home",A::MyXbox,-1,"btnDashboard"},{"Notifications",A::NotificationHistory,-1,"btnConnectToLive"},{"Disc in Tray",A::Unavailable,-1,"btnDiscInTray"}},
            {{"Video Player",A::Videos,-1,"btnVideoLibrary"},{"Music Player",A::Music,-1,"btnMusicLibrary"},{"Picture Viewer",A::Pictures,-1,"btnPictureLibrary"},{"Record Screen (Create)",A::RecordScreen,-1,"btnMCX"}},
            {{"Profile",A::Profile,-1,"btnProfile"},{"Themes",A::SelectTheme,-1,"btnPersonalSettings"},{"Controller Map",A::ControllerMap,-1,"btnFamilySettings"},{"System Settings",A::SystemSettings,-1,"btnSystemSettings"},{"Avatar Settings",A::Avatar,-1,"btnAccountManagement"},{"Notifications",A::NotificationHistory,-1,"btnKinectTuner"},{"Turn Off Console",A::ExitToPs5,-1,"btnShutdown"}},
        },
        { // 17559: Marketplace, Games, Home, Media, Settings
            {},
            {{"Achievements",A::Achievements,-1,"btnAchievements"},{"Refresh Games & Homebrew",A::Refresh,-1,"btnAvatarAwards"},{"Recent",A::RecentGames,-1,"btnQuickLaunch"},{"My Games",A::AllGames,-1,"btnGamesLibrary"}},
            {{"Xbox Home",A::MyXbox,-1,"btnDashboard"},{"Notifications",A::NotificationHistory,-1,"btnConnectToLive"},{"Disc in Tray",A::Unavailable,-1,"btnDiscInTray"}},
            {{"System Video Player",A::Videos,-1,"btnVideoLibrary"},{"System Music Player",A::Music,-1,"btnMusicLibrary"},{"Picture Viewer",A::Pictures,-1,"btnPictureLibrary"},{"Record Screen (Create)",A::RecordScreen,-1,"btnMCX"}},
            {{"Profile",A::Profile,-1,"btnProfile"},{"Themes",A::SelectTheme,-1,"btnPersonalSettings"},{"Controller Map",A::ControllerMap,-1,"btnFamilySettings"},{"System Settings",A::SystemSettings,-1,"btnSystemSettings"},{"Avatar Settings",A::Avatar,-1,"btnAccountManagement"},{"Notifications",A::NotificationHistory,-1,"btnKinectTuner"},{"Turn Off Console",A::ExitToPs5,-1,"btnShutdown"}},
        },
    };
    for (int build=0;build<4;++build) {
        Model m;m.era=build==0?1:build==1?2:3;m.dashboard_build=builds[build];
        m.open_guide();
        for (int blade=0;blade<BladeCount;++blade) {
            const auto rows=m.blade_entries(blade);
            CHECK(rows.size()>=expected[build][std::size_t(blade)].size());
            for (std::size_t i=0;i<rows.size() && i<expected[build][std::size_t(blade)].size();++i) {
                const auto &want=expected[build][std::size_t(blade)][i];
                CHECK(rows[i].label==want.label && rows[i].action==want.action);
                CHECK(std::string(rows[i].node)==want.node);
                Model at=m;at.guide_blade=blade;
                for (std::size_t step=0;step<i;++step) at.press(xb::Down);
                CHECK(at.guide_item[blade]==int(i));
                CHECK(at.hints().size()==3 && has_hint(at,xb::A,"Select") && has_hint(at,xb::B,"Back"));
                CHECK(has_hint(at,xb::Y,build<2?"Xbox Dashboard":"Xbox Home"));
                CHECK(!has_button(at,xb::X) && !has_button(at,xb::RS));
                const auto effect=at.press(xb::A);
                CHECK(want.action==A::Music?at.guide:!at.guide);
                switch (want.action) {
                case A::Unavailable:
                    CHECK(at.details && at.details_title==want.label && at.details_text=="Not available on PS5 yet.");break;
                case A::Achievements: CHECK(at.viewer==Viewer::Achievements);break;
                case A::Profile: CHECK(at.viewer==Viewer::Profile);break;
                case A::Music: CHECK(at.viewer==Viewer::None && !at.guide_host && at.guide_music_page>0 && effect.command==Command::GuideMusicRefresh);break;
                case A::Pictures: CHECK(at.viewer==Viewer::Pictures && effect.command==Command::ScanPictures);break;
                case A::Videos: CHECK(at.viewer==Viewer::Videos);break;
                case A::AllGames: CHECK(at.viewer==Viewer::Library && at.collection==Collection::All);break;
                case A::RecentGames: CHECK(at.viewer==Viewer::Library && at.collection==Collection::Recent);break;
                case A::SystemSettings: CHECK(at.viewer==Viewer::SystemSettings && at.system_page==Model::SystemRoot);break;
                case A::Refresh: CHECK(effect.command==Command::Refresh);break;
                case A::NotificationHistory: CHECK(at.details && at.details_title=="Notifications This Session");break;
                case A::SelectTheme: CHECK(at.viewer==Viewer::Themes);break;
                case A::ControllerMap: CHECK(at.controller_map);break;
                case A::Avatar: CHECK(at.viewer==Viewer::Avatar);break;
                case A::RecordScreen: CHECK(at.details && at.details_title=="Record Screen");break;
                case A::Apps: CHECK(at.channel==Apps && at.viewer==Viewer::None);break;
                case A::Media: CHECK(at.channel==Media && at.viewer==Viewer::None);break;
                case A::LibrarySummary: CHECK(at.details && at.details_title=="Library Summary");break;
                case A::ExitToPs5: CHECK(effect.command==Command::ExitToPs5);break;
                case A::MyXbox: CHECK(at.channel==MyXbox && at.viewer==Viewer::None);break;
                default: CHECK(false);break;
                }
            }
            if (!rows.empty()) {
                Model at=m;at.guide_blade=blade;
                for (int step=0;step<30;++step) at.press(xb::Down);
                CHECK(at.guide_item[blade]==int(rows.size())-1);
                at.press(xb::Y);CHECK(!at.guide && at.channel==MyXbox);
            }
        }
        // Metro V1 retains Marketplace; only V2 starts at Games & Apps.
        for (int step=0;step<6;++step) m.press(xb::LB);
        CHECK(m.guide_blade==(build==3?BladeGames:BladeApps));
        m.press(xb::B);CHECK(!m.guide);
        m.viewer=Viewer::DashboardEras;m.open_guide();
        CHECK(m.hints().size()==3 && !has_button(m,xb::X) && has_hint(m,xb::A,"Select"));
        m.press(xb::B);CHECK(m.viewer==Viewer::DashboardEras && !m.guide);
        m.viewer=Viewer::None;
        m.open_guide();m.media_player=build==0?1:2;m.guide_blade=BladeMedia;
        auto media=m.blade_entries(BladeMedia);
        CHECK(media.size()>=5 && media[1].node[0] && media[3].action==GuideAction::RecordScreen && media[3].node[0]);
        m.guide_item[BladeMedia]=find_action(m,BladeMedia,GuideAction::Music);
        CHECK(m.press(xb::A).command==Command::GuideMusicRefresh && m.guide && !m.guide_host && m.guide_music_page>0 && m.viewer==Viewer::None);
        const auto popup=m.blade_entries(BladeMedia);
        CHECK(popup.size()>=5 && popup[0].action==GuideAction::GuideMusicAlbums && popup[4].action==GuideAction::GuideMusicOpenFull);
        m.press(xb::B); // back to the era-authored Guide
    m.open_guide();m.guide_blade=BladeMedia;m.now_playing=0;m.now_playing_title="Song";
        media=m.blade_entries(BladeMedia);CHECK(media.size()>=12);
        for (std::size_t i=4;i<media.size();++i) CHECK(!media[i].node[0]);
        // Mini-player song and original Music Library are distinct focus targets.
        CHECK(std::any_of(media.begin(),media.end(),[](const GuideEntry &row){return row.label=="Song";}));
    }
}

static void tabbed_guide_host_utilities_reachable()
{
    using A=GuideAction;
    for (int build:{9199,13604,15574,17559}) {
        Model m=with_apps(3);m.era=build==9199?1:build==13604?2:3;m.dashboard_build=build;
        m.set_games(some_games(2));m.avatar_editable=true;
        m.load_pins({"/data/homebrew/App2/eboot.elf","title:CUSA12345"});
        m.played("/data/homebrew/App1/eboot.elf");m.played("title:CUSA12345");
        m.open_guide();CHECK(m.guide && !m.guide_host);
        const int initial_blade=m.guide_blade,initial_item=m.guide_item[initial_blade];
        m.press(xb::RS); // no hidden/secondary menu exists
        CHECK(m.guide && !m.guide_host && m.guide_blade==initial_blade && m.guide_item[initial_blade]==initial_item);
        std::set<A> actions;std::set<int> system_apps;
        bool attached=false,played=false;
        for (int blade=0;blade<BladeCount;++blade) {
            if(build==17559 && blade==BladeApps) continue; // merged into Games & Apps
            m.guide_blade=blade;m.guide_item[blade]=0;
            const auto rows=m.blade_entries(blade);
            for (std::size_t i=0;i<rows.size();++i) {
                CHECK(m.guide_item[blade]==int(i));
                actions.insert(rows[i].action);
                if (rows[i].action==A::LaunchSystem) system_apps.insert(rows[i].app);
                attached=attached || rows[i].label.rfind("Attached: ",0)==0;
                played=played || rows[i].label.rfind("Play ",0)==0;
                m.press(xb::Down);
            }
        }
        m.guide_music_page=5;
        for(const auto &row:m.blade_entries(BladeMedia)) actions.insert(row.action);
        m.guide_music_page=0;
        for (A required:{A::Apps,A::Games,A::Media,A::Launch,A::LaunchTitle,A::Refresh,
                         A::ControllerMap,A::ExitToPs5,A::ChangeGamerpic,A::ToggleSounds,
                         A::CycleEra,A::CycleButtonStyle,A::ToggleDimming,A::CycleLibraryOrder,
                         A::CycleNotifications,A::CyclePromotionMode,A::AdsSettings,A::LibrarySummary,
                         A::NotificationHistory,A::Avatar,A::AvatarEdit,A::Profile,A::GuideMusicAlbums,A::GuideMusicOpenFull,A::Pictures,A::Videos})
            CHECK(actions.count(required)==1);
        CHECK(system_apps.count(0)==1 || actions.count(A::AllGames)==1);
        CHECK(system_apps.count(1)==1);
        CHECK(system_apps.count(2)==1);
        CHECK(system_apps.count(3)==1);
        CHECK(system_apps.size()>=3 && system_apps.size()<=4);
        CHECK(attached);
        CHECK(played);
        // Settings pickers return to the same host section and row.
        const auto settings=m.blade_entries(BladeSettings);
        for (std::size_t i=0;i<settings.size();++i) {
            if (settings[i].action!=A::CycleEra && settings[i].action!=A::AdsSettings) continue;
            m.guide_item[BladeSettings]=int(i);m.press(xb::A);CHECK(!m.guide);
            m.press(xb::B);CHECK(m.guide && !m.guide_host && m.guide_blade==BladeSettings && m.guide_item[BladeSettings]==int(i));
        }
        CHECK(m.blade_entries(BladeSettings)[0].label=="Profile");
        m.press(xb::RS);CHECK(!m.guide_host);
        m.press(xb::B);CHECK(!m.guide);m.open_guide();CHECK(!m.guide_host);
    }
}

static void separate_avatar_editor_keeps_navigation(){
    for(int era=0;era<4;++era){Model m;m.era=era;m.external_avatar_editor=true;m.viewer=Viewer::Profile;m.viewer_item=1;m.guide=true;
        const auto effect=m.perform_guide_entry({"Avatar Editor",GuideAction::AvatarEdit});
        CHECK(effect.command==Command::OpenAvatarEditor);CHECK(m.viewer==Viewer::Profile);CHECK(m.viewer_item==1);CHECK(m.guide);
        m.guide=false;m.viewer=Viewer::Avatar;CHECK(m.press(xb::Y).command==Command::OpenAvatarEditor);CHECK(m.viewer==Viewer::Avatar);
        CHECK(m.profile_entries()[1].action==GuideAction::AvatarEdit);
    }
}

static void screen_recording_companion_and_fallback()
{
    const auto idle=exbox_recorder::parse_status(
        R"({"version":"1.3","backend":"h264","recording":false,"recording_name":"","recording_seconds":0.0,"recording_error":false})");
    CHECK(idle.valid && !idle.recording && !idle.error && idle.seconds==0.0);
    const auto live=exbox_recorder::parse_status(
        R"({"backend":"h264","recording":true,"recording_paused":false,"recording_name":"glint-123-45.h264","recording_seconds":65.8,"recording_error":false})");
    CHECK(live.valid && live.recording && live.pause_supported && !live.paused && live.name=="glint-123-45.h264" && int(live.seconds)==65);
    const auto paused=exbox_recorder::parse_status(
        R"({"backend":"h264","recording":true,"recording_paused":true,"recording_name":"glint-123-45.h264","recording_seconds":65.8,"recording_error":false})");
    CHECK(paused.valid && paused.recording && paused.paused);
    CHECK(!exbox_recorder::parse_status(R"({"backend":"other","recording":true})").valid);

    Model m;m.era=1;
    auto find_record=[&] {const auto rows=m.blade_entries(BladeMedia);return *std::find_if(rows.begin(),rows.end(),[](const GuideEntry &row){return row.action==GuideAction::RecordScreen;});};
    CHECK(find_record().label=="Record Screen (Create)");
    auto effect=m.perform_guide_entry(find_record());
    CHECK(effect.command==Command::None && m.details && m.details_text.find("DualSense Create")!=std::string::npos);
    m.details=false;m.recorder_available=true;
    CHECK(find_record().label=="Start Recording");
    effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::RecorderAction && effect.app==0);
    m.recorder_recording=true;m.recorder_seconds=65.8;m.recorder_pause_supported=true;m.recorder_control=1;
    CHECK(find_record().label=="Recording 01:05");
    effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::RecorderAction && effect.app==1);
    m.recorder_paused=true;m.recorder_control=0;CHECK(find_record().label=="Recording Paused 01:05");
    effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::RecorderAction && effect.app==2);
    const auto rows=m.blade_entries(BladeMedia);const int record_row=int(std::find_if(rows.begin(),rows.end(),[](const GuideEntry &row){return row.action==GuideAction::RecordScreen;})-rows.begin());
    m.guide=true;m.guide_host=false;m.guide_blade=BladeMedia;m.guide_item[BladeMedia]=record_row;m.recorder_control=0;
    m.press(xb::Right);CHECK(m.guide_item[BladeMedia]==record_row && m.recorder_control==2);
    m.press(xb::Left);CHECK(m.guide_item[BladeMedia]==record_row && m.recorder_control==0);
    m.recorder_paused=false;m.recorder_control=1;m.press(xb::Right);CHECK(m.recorder_control==2);
    m.press(xb::Left);CHECK(m.recorder_control==1);
    m.recorder_control=2;effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::RecorderAction && effect.app==3);
    Model nav;nav.era=1;nav.guide=true;nav.guide_blade=BladeMedia;nav.recorder_available=true;nav.recorder_recording=true;nav.recorder_pause_supported=true;
    const auto nav_rows=nav.blade_entries(BladeMedia);const int nav_record=int(std::find_if(nav_rows.begin(),nav_rows.end(),[](const GuideEntry &row){return row.action==GuideAction::RecordScreen;})-nav_rows.begin());
    nav.guide_item[BladeMedia]=nav_record-1;nav.press(xb::Down);CHECK(nav.guide_item[BladeMedia]==nav_record);
    nav.press(xb::Up);CHECK(nav.guide_item[BladeMedia]==nav_record-1);
    m.recorder_pause_supported=false;m.recorder_paused=false;
    CHECK(find_record().label=="Recording 01:05");
    m.recorder_control=2;
    effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::RecorderAction && effect.app==3);
    m.recorder_busy=true;CHECK(find_record().label=="Screen Recorder Working...");
    effect=m.perform_guide_entry(find_record());CHECK(effect.command==Command::None && m.details);

    // The normal Xbox popup Media blade owns recording as an ordinary row.
    Model popup;popup.era=3;popup.dashboard_build=17559;popup.guide=true;popup.guide_blade=BladeMedia;
    popup.recorder_available=true;popup.recorder_recording=true;popup.recorder_pause_supported=true;
    const auto popup_rows=popup.blade_entries(BladeMedia);
    const int popup_record=int(std::find_if(popup_rows.begin(),popup_rows.end(),[](const GuideEntry&row){return row.action==GuideAction::RecordScreen;})-popup_rows.begin());
    CHECK(popup_record<int(popup_rows.size()));popup.guide_item[BladeMedia]=popup_record;popup.recorder_control=1;
    popup.press(xb::Right);CHECK(popup.guide_item[BladeMedia]==popup_record && popup.recorder_control==2);
    popup.press(xb::Left);CHECK(popup.recorder_control==1);
    popup.press(xb::Left);CHECK(popup.guide_item[BladeMedia]==popup_record && popup.recorder_control==1);
}

static void fleshed_system_settings_are_truthful_and_live()
{
    Model m;m.era=3;m.dashboard_build=17559;m.player="BAPenguin";
    m.games={{"Game One","","PPSA00001",""},{"Game Two","","PPSA00002",""}};
    m.apps={{"Homebrew","/data/homebrew/demo","",""}};m.songs=37;m.pictures=12;
    m.viewer=Viewer::SystemSettings;
    const auto root=m.system_entries();
    CHECK(root.size()>=10 && root[0].label=="Console Settings" && root[3].label=="Network Settings");
    CHECK(root.back().label=="Account Management" && root.back().app==Model::SystemAccount);
    m.viewer_item=int(root.size())-1;m.press(xb::A);
    CHECK(m.system_page==Model::SystemAccount && m.system_page_title()=="Account Management");
    CHECK(m.system_value("Local Profile")=="BAPenguin");
    const auto account=m.system_entries();
    const int live=int(std::find_if(account.begin(),account.end(),[](const GuideEntry&e){return e.label=="Xbox Live Account";})-account.begin());
    m.viewer_item=live;m.press(xb::A);
    CHECK(m.details && m.details_text.find("not connected")!=std::string::npos);m.press(xb::B);
    m.press(xb::B);CHECK(m.system_page==Model::SystemRoot && m.viewer_item==int(root.size())-1);

    m.system_page=Model::SystemComputers;m.viewer_item=0;
    CHECK(m.system_value("Music Library")=="37 tracks" && m.system_value("Pictures")=="12 pictures");
    CHECK(m.system_description().find("local music")!=std::string::npos);
    m.system_page=Model::SystemCamera;m.viewer_item=0;m.press(xb::A);
    CHECK(m.details && m.details_text.find("controlled by PS5")!=std::string::npos);m.press(xb::B);
    m.system_page=Model::SystemDisplay;
    CHECK(m.system_value("HDR Output")=="Managed by PS5" && m.system_meta("HDR Output").find("PS5 video settings")!=std::string::npos);

    m.system_page=Model::SystemMusicVolume;m.viewer_item=3;
    const Effect volume=m.press(xb::A);
    CHECK(volume.command==Command::MusicVolume && volume.app==3 && m.music_volume==3);
    CHECK(m.system_value("Music Volume 30%")=="Current" && m.system_value("Ex-Box Music Volume")=="30%");
    m.system_page=Model::SystemInfo;m.active_render_scale=2;
    CHECK(m.system_entries().size()==8 && m.system_value("Installed Games")=="2" &&
          m.system_value("Homebrew Apps")=="1" && m.system_value("Rendering Mode")=="3840 x 2160");
    m.viewer_item=0;m.press(xb::A);CHECK(m.details && !m.details_text.empty());
}

int main()
{
    album_info_edits_and_storage();
    profile_achievements_navigation_and_empty_states();
    achievements_fake_source_counts_points_and_navigation();
    welcome_channel_and_setting();
    initial_setup_per_era();
    metro_settings_tiles_and_navigation();
    dualsense_reads_as_xbox();
    keyboard_reads_as_xbox();
    triggers_have_hysteresis();
    stick_acts_as_dpad();
    held_buttons_repeat();
    sticks_together_exit();
    starts_on_my_xbox();
    network_settings_pages_by_era();
    network_test_steps_with_fake_probe();
    channels_remember_their_place();
    bumpers_and_triggers_page();
    a_opens_and_launches();
    my_xbox_cards_go_somewhere();
    rendering_resolution_is_requested_until_restart();
    exbox_settings_are_functional_and_honest();
    console_settings_pages_follow_the_build();
    console_settings_choices_are_live();
    fleshed_system_settings_are_truthful_and_live();
    memory_storage_flow_and_unavailable();
    friends_and_media_are_honest();
    attach_and_detach();
    missing_attachments_stay_visible();
    attachments_are_capped();
    tabbed_guide_authored_rows_and_bindings();
    tabbed_guide_host_utilities_reachable();
    guide_opens_on_the_home_blade();
    bumpers_flip_blades();
    blades_launch_and_navigate();
    details_hand_over_to_guide();
    empty_app_channel();
    refresh_keeps_selection_in_range();
    console_games_fill_the_games_channel();
    games_attach_to_my_xbox();
    game_artwork_source_can_be_selected();
    guide_games_blade_lists_console_games();
    recently_played_come_first();
    guide_library_shortcuts_and_preferences();
    attached_shortcuts_and_notification_history();
    media_viewer_pictures_and_videos();
    pictures_read_the_ps5_path();
    pictures_sort_and_group_by_game();
    picture_full_screen_zoom_pan_slideshow();
    picture_transport_rotation_and_order();
    picture_background_confirmation_and_settings();
    theme_picker_navigation_and_migration();
    avatar_stage();
    music_library_and_player();
    avatar_editor();
    scene_guide_media_player();
    music_pages();
    sounds_follow_the_360();
    details_order_my_xbox();
    details_move_homebrew_between_channels();
    details_sounds_and_guide();
    details_choose_a_trailer();
    blades_matches_its_visible_rows();
    blades_tray_opens_and_returns_to_the_same_slot();
    metro_tabs_enter_and_return();
    metro_every_tile_is_reachable();
    metro_collections_and_details_keep_context();
    refresh_and_system_navigation();
    era_picker_and_update_navigation();
    promotions_keep_library_actions_and_legacy_navigation();
    metro_v1_pages_have_no_invisible_targets();
    ads_profiles_have_independent_modes_and_navigation();
    custom_ad_pages_keep_every_selected_image_reachable();
    ad_candidate_filter_keeps_manual_resources_explicit();
    game_videos_library_sorts_and_hides_missing();
    game_videos_seek_pause_and_resume();
    video_download_dates_sort_cycle_and_settings();
    video_resume_restart_choice();
    subtitles_parse_sidecar_srt();
    media_text_follows_each_build();
    separate_avatar_editor_keeps_navigation();
    screen_recording_companion_and_fallback();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
