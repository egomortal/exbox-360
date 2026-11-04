// Included inside namespace dash in dash_model.cpp; no I/O or SDL.
// homepage/welcome.xml and firstrun scenes (9199/13604).
void Model::load_welcome_setting(const std::string &line) {
    if (line=="welcome_visible=0" || line=="welcome_visible=0\n" || line=="welcome_visible=0\r\n") welcome_visible=false;
    if (line=="welcome_visible=1" || line=="welcome_visible=1\n" || line=="welcome_visible=1\r\n") welcome_visible=true;
    if (!welcome_available() && channel==Welcome) channel=MyXbox;
}
std::string Model::saved_welcome_setting() const { return welcome_visible?"welcome_visible=1":"welcome_visible=0"; }
void Model::load_welcome_content(const std::vector<std::string> &lines) {
    welcome_content.clear();
    for (const auto &line:lines) {
        const auto a=line.find('\t'),b=a==std::string::npos?a:line.find('\t',a+1),c=b==std::string::npos?b:line.find('\t',b+1);
        if (c==std::string::npos) continue;
        WelcomePage page{line.substr(a+1,b-a-1),line.substr(c+1),line.substr(b+1,c-b-1)};
        if (!page.body.empty() && page.body.back()=='\r') page.body.pop_back();
        size_t at=0;
        while ((at=page.body.find("\\n",at))!=std::string::npos) {page.body.replace(at,2,"\n");++at;}
        welcome_content[line.substr(0,a)]=std::move(page);
    }
}
std::vector<std::string> Model::welcome_pages() const {
    if (welcome_hot && era==1) return {"WhatsNewAvatarGearScene","WhatsNewAvatarsScene","WhatsNewContentTransferScene",
        "WhatsNewFacebookTwitterScene","WhatsNewGamesOnDemandScene","WhatsNewJoinXboxLIVEScene",
        "WhatsNewLastFMScene","WhatsNewUSBStorageScene","WhatsNewZuneVideoMarketplaceScene"};
    if (era==2) return {"XboxBasicsGettingAroundScene","XboxBasicsCreateProfileScene","XboxBasicsFamilyScene",
        "XboxBasicsJoinLiveScene","XboxBasicsPrivacyScene"};
    return {"XboxBasicsGettingAroundScene","XboxBasicsCreateProfileScene","XboxBasicsFamilyScene","XboxBasicsFriendsScene",
        "XboxBasicsJoinLiveScene","XboxBasicsMediaScene","XboxBasicsPlayGamesScene","XboxBasicsPrivacyScene"};
}
std::string Model::welcome_key() const {
    if (viewer==Viewer::InitialSetup) return "setup/"+std::to_string(setup_step);
    if (welcome_confirm) return "hide";
    const auto pages=welcome_pages();
    return pages[size_t(std::clamp(viewer_item,0,int(pages.size())-1))];
}
Model::WelcomePage Model::welcome_page() const {
    const auto page=welcome_content.find(welcome_key());
    if (page!=welcome_content.end()) return page->second;
    // Ex-Box addition: no 360 original. Missing private resources must be visible.
    return {viewer==Viewer::InitialSetup?"Initial Setup":"Welcome","Not available on PS5 yet.",""};
}
Effect Model::open_setup() {
    if (viewer==Viewer::InitialSetup) {guide=false;return {};}
    setup_previous_viewer=viewer;setup_previous_item=viewer_item;
    guide=false;setup_step=SetupIntro;
    return open_viewer(Viewer::InitialSetup);
}
Effect Model::press_welcome(xb::Button button) {
    if (button==xb::Guide || button==xb::Start) {open_guide();return {};}
    if (viewer==Viewer::InitialSetup) {
        if (button==xb::B) {
            if (setup_step>SetupIntro) --setup_step;
            else {viewer=setup_previous_viewer;viewer_item=setup_previous_item;}
            return {};
        }
        // Ex-Box addition: these reuse T05's local controls within the era's setup.
        // Auto-Off means idle dimming here, never a PS5 power-off timer.
        if (button==xb::Left || button==xb::Right) {
            if (setup_step==SetupAutoOff) {dimming=button==xb::Left;return {Command::SaveSettings,-1};}
            if (setup_step==SetupClock) {clock_24_hour=button==xb::Right;return {Command::SaveSettings,-1};}
        }
        if (button==xb::A) {
            if (setup_step==SetupDone) {viewer=setup_previous_viewer;viewer_item=setup_previous_item;}
            else ++setup_step;
        }
        return {};
    }
    if (button==xb::B) {viewer=Viewer::None;welcome_confirm=false;return {};}
    if (welcome_confirm) {
        if (button==xb::Left || button==xb::Up) welcome_yes=true;
        if (button==xb::Right || button==xb::Down) welcome_yes=false;
        if (button==xb::A) {
            const bool hide=welcome_yes;
            viewer=Viewer::None;welcome_confirm=false;
            if (hide) {welcome_visible=false;go(MyXbox);return {Command::SaveSettings,-1};}
        }
        return {};
    }
    const int last=int(welcome_pages().size())-1;
    if (button==xb::Left || button==xb::LB) viewer_item=std::max(0,viewer_item-1);
    if (button==xb::Right || button==xb::RB || button==xb::A) viewer_item=std::min(last,viewer_item+1);
    return {};
}
