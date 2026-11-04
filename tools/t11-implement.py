from pathlib import Path

def edit(name, old, new, count=1):
    p=Path(name);raw=p.read_bytes();crlf=b'\r\n' in raw
    text=raw.decode().replace('\r\n','\n')
    assert old in text,(name,old[:100])
    text=text.replace(old,new,count)
    p.write_bytes(text.replace('\n','\r\n' if crlf else '\n').encode())

edit('dash_model.hpp','    MyXbox,\n    ChannelCount,','    Welcome, // homepage/emb_homepage.xml: immediately before XBOX360 (9199/13604).\n    MyXbox,\n    ChannelCount,')
edit('dash_model.hpp','    return channel == Friends','    return channel == Welcome ? "Welcome"\n           : channel == Friends')
edit('dash_model.hpp','    ToMediaCenter,','    ToWelcome,\n    HideWelcome,\n    ExitToPs5,\n    ToMediaCenter,')
edit('dash_model.hpp','    SystemSettings, //','    WelcomePages,\n    InitialSetup,\n    SystemSettings, //')
edit('dash_model.hpp','    CycleEra,','    InitialSetup,\n    CycleEra,')
edit('dash_model.hpp','    Launch,      // a game','    SetupComplete, // oobe/celebrate_sound.xma (13604/15574).\n    Launch,      // a game')
edit('dash_model.hpp','    int metro_page_size() const {return','    int metro_page_size() const {return era==3 && metro_tab==7?8:')
edit('dash_model.hpp','    bool clock_24_hour = false;', '''    bool welcome_visible = true; // settings.txt; Welcome Channel and Hide Channel share this.
    bool welcome_available() const { return (era==1 || era==2) && welcome_visible; }
    int channel_rank(int c) const { return c-(c>Welcome && !welcome_available()?1:0); }
    void load_welcome_setting(const std::string &line);
    std::string saved_welcome_setting() const;
    struct WelcomePage { std::string title, body, art; };
    std::map<std::string,WelcomePage> welcome_content;
    void load_welcome_content(const std::vector<std::string> &lines);
    std::vector<std::string> welcome_pages() const;
    std::string welcome_key() const;
    WelcomePage welcome_page() const;
    bool welcome_hot=false, welcome_confirm=false, welcome_yes=false;
    enum SetupStep { SetupIntro, SetupResolution, SetupAutoOff, SetupClock, SetupDone };
    int setup_step=SetupIntro;
    Viewer setup_previous_viewer=Viewer::None;
    int setup_previous_item=0;
    Effect open_setup();
    Effect press_welcome(xb::Button button);
    bool clock_24_hour = false;''')
edit('dash_model.hpp','NetworkDetails, NetworkIP, NetworkDNS, NetworkConsole };','NetworkDetails, NetworkIP, NetworkDNS, NetworkConsole, SystemWelcome };')
edit('dash_model.cpp','{"Initial Setup",A::CycleEra}','{"Initial Setup",A::InitialSetup}',99)
edit('dash_model.cpp','    if (era==0 && channel==MyXbox) return int(blades_items(MyXbox).size());','    if (channel==Welcome) return era==2?6:4;\n    if (era==0 && channel==MyXbox) return int(blades_items(MyXbox).size());')
edit('dash_model.cpp','if (metro_tab == 0 || metro_tab == 7) return 1;','if (metro_tab == 7) return metro_v1()?10:8;\n        if (metro_tab == 0) return 1;')
edit('dash_model.cpp','Card Model::card_base(int index) const\n{','''Card Model::card_base(int index) const
{
    if (channel==Welcome) {
        const char *nxe[]={"WhatsNewSlotScene","XboxBasicsSlotScene","NxeVideoSlotScene","HideWelcomeSlotScene"};
        const char *kinect[]={"XboxBasicsSlotScene","XboxKinectSlotScene","XboxGoldFamilyPackSlotScene",
                              "DashFirstUseTutorialSlotScene","WirelessWelcomeSlotScene","HideWelcomeSlotScene"};
        const int i=std::clamp(index,0,era==2?5:3);
        const auto found=welcome_content.find(era==2?kinect[i]:nxe[i]);
        Card c; c.title=i==(era==2?5:3)?"Hide Channel":era==2?"Getting Started":i==0?"What's Hot":"Xbox Essentials";
        c.goal=i==(era==2?5:3)?Goal::HideWelcome:(era==1?i<2:i==0)?Goal::ToWelcome:Goal::Unavailable;
        c.icon=IconSettings;
        if (found!=welcome_content.end()) {c.title=found->second.title;c.subtitle=found->second.body;c.image=found->second.art;}
        return c;
    }''')
edit('dash_model.cpp','        if (metro_tab == 7) return {"System Settings", Kind::Hub, IconSettings, Goal::ToSettings, -1, -1, ""};','''        if (metro_tab == 7) {
            // epix/emb_dashhome.xml (15574), dashhome-offline.xml (17559), strings.xus.
            // Template6 / Template6HD: four columns, two rows. V2 swaps Profile/Preferences.
            const char *labels[]={"System","Preferences","Profile","Kinect","Account","Privacy","Family","Turn Off","Xbox Basics","Check My Network"};
            const char *icons[]={"ico_64x_console.png","ico_64x_SettingsPreferences.png","ico_64x_pic.png","ico_64x_kinect_device.png",
                                 "ico_64x_accountmanagement.png","ico_64x_privacy.png","ico_64x_family.png","ico_64x_TurnOff.png","xboxBasics.jpg","xboxNetwork.jpg"};
            int i=std::clamp(index,0,metro_v1()?9:7);
            if (!metro_v1() && (i==1 || i==2)) i=3-i;
            Card c{labels[i],Kind::Hub,i==2?IconAvatar:IconSettings,
                   i==0?Goal::ToSettings:i==2?Goal::ToProfile:i==7?Goal::ExitToPs5:Goal::Unavailable,-1,-1,""};
            const auto art=welcome_content.find(std::string("tile/")+icons[i]);
            if (art!=welcome_content.end()) c.image=art->second.art;
            return c;
        }''')
edit('dash_model.cpp','if (era < 3) out.push_back({"Welcome Channel",A::Unavailable});','if (era==1 || era==2) out.push_back({"Welcome Channel",A::ConsoleSettings,SystemWelcome});')
edit('dash_model.cpp','    if (system_page == SystemStartup) {','    if (system_page == SystemWelcome) return {{"Show",A::ConsoleSettings},{"Hide",A::ConsoleSettings}};\n    if (system_page == SystemStartup) {')
edit('dash_model.cpp','std::string Model::system_page_title() const\n{','std::string Model::system_page_title() const\n{\n    if (system_page==SystemWelcome) return "Welcome Channel";')
edit('dash_model.cpp','    if (label == "Time Format") return','    if (label == "Welcome Channel") return welcome_visible?"Show":"Hide";\n    if (label == "Time Format") return')
edit('dash_model.cpp','Effect Model::press_viewer(xb::Button button)\n{','Effect Model::press_viewer(xb::Button button)\n{\n    if (viewer==Viewer::WelcomePages || viewer==Viewer::InitialSetup) return press_welcome(button);')
edit('dash_model.cpp','            if (system_page == SystemSoundEffects) {','''            if (system_page == SystemWelcome) {
                welcome_visible=viewer_item==0;
                if (!welcome_visible && channel==Welcome) go(MyXbox);
                return {Command::SaveSettings,-1};
            }
            if (system_page == SystemSoundEffects) {''')
edit('dash_model.cpp','const int item = page == SystemSoundEffects','const int item = page == SystemWelcome ? (welcome_visible?0:1)\n                               : page == SystemSoundEffects')
edit('dash_model.cpp','    if (entry.action == GuideAction::SelectTheme) return open_themes();','    if (entry.action == GuideAction::InitialSetup) return open_setup();\n    if (entry.action == GuideAction::SelectTheme) return open_themes();')
edit('dash_model.cpp','    case GuideAction::ChangeGamerpic:\n','    case GuideAction::InitialSetup:\n    case GuideAction::ChangeGamerpic:\n')
edit('dash_model.cpp','    case xb::Up:\n        if (channel > 0)\n            go(channel - 1);','    case xb::Up:\n        if (channel > 0)\n            go(channel == MyXbox && !welcome_available() ? Apps : channel - 1);')
edit('dash_model.cpp','            go(channel + 1);','            go(channel == Apps && !welcome_available() ? MyXbox : channel + 1);')
edit('dash_model.cpp','        case Goal::ToApps: go(Apps); break;','''        case Goal::ToWelcome:
            welcome_hot=era==1 && current()==0;welcome_confirm=false;
            return open_viewer(Viewer::WelcomePages);
        case Goal::HideWelcome:
            welcome_confirm=true;welcome_yes=false;
            return open_viewer(Viewer::WelcomePages);
        case Goal::ExitToPs5: return {Command::ExitToPs5,-1};
        case Goal::ToApps: go(Apps); break;''')
edit('dash_model.cpp','        else if (button == xb::Left || button == xb::Right || button == xb::Up || button == xb::Down)\n        {','''        else if (metro_tab==7 && (button==xb::Left || button==xb::Right || button==xb::Up || button==xb::Down)) {
            const int slot=sel%8, first=sel/8*8;
            int next=sel;
            if (button==xb::Up) {if (slot<4) metro_focus=-1;else next-=4;}
            if (button==xb::Down && slot<4 && sel+4<count()) next+=4;
            if (button==xb::Right) {
                if (slot%4<3 && sel+1<count()) ++next;
                else if (first+8<count()) next=first+8;
            }
            if (button==xb::Left) {if (slot%4>0) --next;else if (first>0) next=first-8+3;}
            sel=next;if (metro_focus!=-1) metro_focus=sel%8;
            return {};
        }
        else if (button == xb::Left || button == xb::Right || button == xb::Up || button == xb::Down)
        {''')
edit('dash_model.cpp','        if (metro_focus >= kMetroPins)','        if (metro_tab!=7 && metro_focus >= kMetroPins)')
edit('dash_model.cpp','    if (controller_map)\n        return {{xb::B, "Back"}};','''    if (controller_map)
        return {{xb::B, "Back"}};
    if (!guide && (viewer==Viewer::WelcomePages || viewer==Viewer::InitialSetup)) {
        if (viewer==Viewer::WelcomePages && !welcome_confirm) return {{xb::LB,"Previous"},{xb::RB,"Next"},{xb::B,"Back"}};
        return {{xb::A,viewer==Viewer::InitialSetup?(setup_step==SetupDone?"Done":"Continue"):"Select"},{xb::B,"Back"}};
    }''')

# Keep implementation separate from the large navigation file.
edit('dash_model.cpp','Effect Model::press_viewer(xb::Button button)',' #include "welcome_model.inc.hpp"\n\nEffect Model::press_viewer(xb::Button button)')

edit('main.cpp','        fprintf(f,"clock_24_hour=%d\\n",model.clock_24_hour?1:0);','        fprintf(f,"clock_24_hour=%d\\n",model.clock_24_hour?1:0);\n        fprintf(f,"%s\\n",model.saved_welcome_setting().c_str());')
edit('main.cpp','            if (std::strncmp(line,"clock_24_hour=",14)==0)','            model.load_welcome_setting(line);\n            if (std::strncmp(line,"clock_24_hour=",14)==0)')
edit('main.cpp','#include "ui/system_settings.inc.hpp"','#include "ui/system_settings.inc.hpp"\n#include "ui/welcome_setup.inc.hpp"')
edit('ui/viewer_dispatch.inc.hpp','void draw_viewer() {','void draw_viewer() {\n    if (model.viewer==dash::Viewer::WelcomePages || model.viewer==dash::Viewer::InitialSetup) {draw_welcome_setup();return;}')
edit('ui/system_settings.inc.hpp','page==dash::Model::SystemScreenSaver ||','page==dash::Model::SystemWelcome || page==dash::Model::SystemScreenSaver ||')
edit('ui/hub_dispatch.inc.hpp','            const float before=float((slide_from>=0?slide_from:model.channel)-i),after=float(model.channel-i);','''            if (i==dash::Welcome && !model.welcome_available()) continue;
            const float before=float(model.channel_rank(slide_from>=0?slide_from:model.channel)-model.channel_rank(i)),after=float(model.channel_rank(model.channel)-model.channel_rank(i));''')
edit('ui/kinect_hub.inc.hpp','        const int rank=model.channel-i;','        if (i==dash::Welcome && !model.welcome_available()) continue;\n        const int rank=model.channel_rank(model.channel)-model.channel_rank(i);')
edit('runtime/sound_effects.inc.hpp','        {S::Launch,"dash/snd_transitioninto"}};','        {S::SetupComplete,"dash/celebrate_sound"},{S::Launch,"dash/snd_transitioninto"}};')
edit('assets/update_art.inc.hpp','    if (library_changed) load_library_art();','    if (library_changed) {load_library_art();load_welcome_resources();}')
