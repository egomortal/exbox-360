// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
// System Settings for NXE, Kinect and Metro: consoles/SystemScene (and its
// dashSysCslSet page). The scene's own parts: a heading (Label_Head), a list
// of one-line rows with a 32 px icon (List_1line_icon_1text) and a meta panel
// on the right with the focused row's description.
namespace system_page {
// The scene's icons, by row (dashmain / common 32 px art the app already loads).
const char *icon_for(const std::string &label) {
    if (label=="Console Settings") return "ico_32x_console";
    if (label=="Family Settings") return "ico_32x_parentalControl";
    if (label=="Memory" || label=="Storage") return "ico_32x_MU";
    if (label=="Network Settings") return "ico_32x_network";
    if (label=="Computers") return "ico_32x_PC";
    if (label=="Initial Setup") return "ico_32x_systemSet";
    return nullptr;
}
// Paragraphs split on blank lines, each wrapped to the width; returns the y below.
int paragraphs(const std::string &value,int x,int y,int width,TTF_Font *font,SDL_Color ink,int pitch) {
    std::string line,word;
    auto place=[&]() {if (!line.empty()) {text(fit(line,font,width),x,y,font,ink);y+=pitch;line.clear();}};
    for (size_t i=0;i<=value.size();++i) {
        const char c=i<value.size()?value[i]:' ';
        if (c!=' ' && c!='\n') {word+=c;continue;}
        if (!word.empty()) {
            const std::string next=line.empty()?word:line+" "+word;
            if (!line.empty() && text_width(next,font)>width) {place();line=word;} else line=next;
            word.clear();
        }
        if (c=='\n') {if (line.empty()) y+=pitch/2; else place();}
    }
    place();
    return y;
}
#include "network_settings.inc.hpp"
// The console's local date and the Guide clock, as ClockTime shows them.
std::string date_time_now() {
    const std::time_t now=std::time(nullptr);
    std::tm snapshot{};
    const std::tm *local=local_time_snapshot(now,snapshot)?&snapshot:nullptr;
    char date[32]{};
    if (!local || !std::strftime(date,sizeof date,"%m/%d/%Y",local)) return "";
    return std::string(date)+"  "+clock_text();
}
std::string current_setting(const std::string &label) {
    const int page=model.system_page;
    if (label=="Date and Time") return date_time_now();
    if (page==dash::Model::SystemSoundEffects || page==dash::Model::SystemTimeFormat ||
        page==dash::Model::SystemWelcome || page==dash::Model::SystemScreenSaver || page==dash::Model::SystemResolution ||
        page==dash::Model::SystemScreenFormat) return model.system_value(model.system_page_title());
    return model.system_value(label);
}
void memory_icon(int row,int x,int y) {
    const int icon=model.storage_row_icon(row);
    if (icon>=0 && icon<12 && storage_art[icon]) blit_contained(storage_art[icon],{x,y,48,48});
}
std::string memory_body() {
    std::string body=model.storage_body();
    // Ex-Box addition: no 360 original. Never present a bounded inventory as complete.
    if (model.storage.limited) body+="\nShowing a limited file list.";
    return body;
}
// ItemsGrid/ItemsGrid2.xur: two-line items with an icon and measured file size.
// Keep the enclosing System Settings panels/fonts; two columns share the model's D-pad geometry.
bool memory_grid(int x,int y,int width,TTF_Font *font) {
    if (model.system_page!=dash::Model::StorageItems) return false;
    const auto entries=model.storage_entries();
    const int n=int(entries.size()), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const SDL_Color white{235,235,235,255};
    text(model.storage_category_name(model.storage_category),x+10,y,font,white);
    if (!n) {paragraphs(memory_body(),x+30,y+85,width-60,regular,white,42);return true;}
    const int first=(item/8)*8, cell=(width-24)/2, pitch=135;
    for (int i=first;i<std::min(n,first+8);++i) {
        const int cx=x+(i-first)%2*(cell+24), cy=y+55+(i-first)/2*pitch;
        const bool on=i==item;
        if (model.era==1) {
            shade(cx,cy,cell,pitch-12,0x25333e,100);
            if (on) guide::bg::ring(cx,cy,cell,pitch-12,6,3,0x69a50f,255);
        } else if (on) {
            if (model.era==3 && !model.metro_v1()) rect(cx,cy,cell,pitch-12,0x008a00);
            else gradient(cx,cy,cell,pitch-12,0x77ba02,0x51860f);
        } else gradient(cx,cy,cell,pitch-12,0xe8ebeb,0xc9cfd1);
        memory_icon(i,cx+14,cy+20);
        const SDL_Color ink=model.era==1 || on?white:SDL_Color{0x65,0x6d,0x72,255};
        text(fit(entries[size_t(i)].label,font,cell-95),cx+80,cy+16,font,ink);
        text(fit(model.storage_row_size(i),small,cell-95),cx+80,cy+65,small,ink);
    }
    if (model.storage.limited) text("Showing a limited file list.",x+10,y+610,small,white);
    return true;
}
// PolicyInfo_System's information block and ClockTime's date/time fields.
// No serial number, console ID, fabricated firmware, or editable system clock.
bool read_only_body(int x,int y,int width,TTF_Font *font,SDL_Color ink) {
    if (model.system_page!=dash::Model::SystemDateTime) return false;
    // dashCSettingsStrings.xus, every build: "Current Date and Time:".
    text("Current Date and Time:",x,y,font,ink);
    const std::string now=date_time_now();
    if (!now.empty()) text(now,x,y+60,font,ink);
    else paragraphs("Not available on PS5 yet.",x,y+60,width,font,ink,48); // Ex-Box addition: no 360 original
    return true;
}
}
// NXE 9199: SystemScene in the 2008 skin. One BackgroundPanel 9-grid (876 x 484
// at 720p, offsets 15/20) holds the btn_1line_icon list on the left and the
// metaScene_1line text on the right; the focused row wears greenBox1, a 0x69a50f
// glow around it, rather than a filled bar. Label_Head sits above the panel.
void draw_system_settings_nxe() {
    {   // A scene of its own over the dashboard's background, as Kinect's.
        SDL_Rect all{0,0,W,H};
        if (era_background) blit_quality(era_background,nullptr,screen,&all);
        else gradient(0,0,W,H,0x358f24,0xc9e713);
        if (era_aura_floor) blit_quality(era_aura_floor,nullptr,screen,&all);
    }
    constexpr int ox=303, oy=200, ow=1314, oh=726; // 202,133 x 876,484 at 720p
    if (nxe_panel) {
        nine_slice(nxe_panel,ox-22,oy-18,ow+44,oh+42,18,27);
    } else rounded(ox,oy,ow,oh,10,0x4a5a66,0x323f4b);
    const bool page=model.system_page!=dash::Model::SystemRoot;
    text(model.system_page_title(),ox+15,oy-78,large,{235,235,235,250});
    const auto entries=model.system_entries();
    const int n=int(entries.size()), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    TTF_Font *row_font=guide::item_font?guide::item_font:regular;
    const int lx=ox+30, ly=oy+30, lw=630, pitch=70;
    if (system_page::network_body(lx,ly,ow-60)) return;
    if (system_page::read_only_body(lx+27,ly+20,ow-100,regular,{235,235,235,255})) return;
    if (system_page::memory_grid(lx,ly,ow-60,row_font)) return;
    const int first=std::max(0,item-8), end=std::min(n,first+9);
    for (int i=first;i<end;i++) {
        const int y=ly+(i-first)*pitch;
        shade(lx+5,y+3,lw-10,2,0xebebeb,26);   // "top" and "bottom1": 0xebebeb at 10%
        shade(lx+5,y+pitch,lw-10,2,0xebebeb,26);
        if (i==item) {
            // greenBox1: a soft green rim, strongest on the row's edge.
            for (int g=8;g>=1;g--) guide::bg::ring(lx-g,y-g+2,lw+2*g,pitch+2*g-2,6+g,1,0x69a50f,Uint8(std::max(0,170-g*20)));
            guide::bg::ring(lx,y+2,lw,pitch-2,6,3,0x69a50f,255);
            shade(lx+3,y+5,lw-6,(pitch-8)/2,0xebebeb,22);   // Highlight14's wash
        }
        int tx=page?lx+27:lx+81; // dashSysCslSet's rows carry no icon
        if (model.storage_page() && model.storage_row_icon(i)>=0) {
            system_page::memory_icon(i,lx+12,y+12);tx=lx+81;
        }
        if (const char *name=system_page::icon_for(entries[size_t(i)].label)) {
            auto it=blades_system_icons.find(std::string("items/")+name);
            if (it==blades_system_icons.end()) it=blades_system_icons.find(name);
            if (it!=blades_system_icons.end()) blit_contained(it->second,{lx+12,y+15,48,48});
        }
        const bool device=model.system_page==dash::Model::StorageDevices;
        text(fit(entries[size_t(i)].label,row_font,lw-(tx-lx)-20),tx,device?y+2:y+12,row_font,{235,235,235,255});
        if (device) text(fit(model.storage_row_size(i),small,lw-(tx-lx)-20),tx,y+40,small,{235,235,235,230});
    }
    // metaScene_1line, at the panel's right half (x 456 at 720p).
    const int mx=ox+684+26, my=oy+30, mw=574;
    const std::string label=entries.empty()?"":entries[size_t(item)].label;
    if (model.storage_page()) {
        system_page::paragraphs(system_page::memory_body(),mx,my,mw,regular,{255,255,255,230},42);
    } else if (page) {
        text("Current Setting",mx,my,regular,{255,255,255,230});
        const int below=system_page::paragraphs(system_page::current_setting(label),mx,my+50,mw,regular,{255,255,255,230},42);
        system_page::paragraphs(model.system_description(),mx,below+35,mw,regular,{255,255,255,230},42);
    } else system_page::paragraphs(model.system_meta(label),mx,my,mw,regular,{255,255,255,230},42);
}
void draw_system_settings() {
    if (model.system_page==dash::Model::SystemHealth) {draw_compat_health();return;}
    if (model.system_page==dash::Model::SystemDiagnostics) {draw_developer_diagnostics();return;}
    if (model.era==1) {draw_system_settings_nxe();return;}
    // Kinect and Metro draw SystemScene in the 13604
    // skin: a white Label_Head over the dashboard background, the list on the light
    // BackgroundPanel (btn_1line_icon_1text rows: 47 px at 720p -> 70, 0x9facb2
    // rules, 0x656d72 text turning 0xebebeb on the green bar), and the
    // description in white on a dark RightPanelShader.
    const bool light=model.era>=2, metro=model.era==3;
    constexpr int px=120, py=90, pw=1680, ph=895;
    if (light) {
        // A scene of its own: the dashboard's background, not the hub behind.
        SDL_Rect all{0,0,W,H};
        if (era_background) blit_quality(era_background,nullptr,screen,&all);
        else if (metro) gradient(0,0,W,H,0x4a4a4a,0x9a9a9a);
        else gradient(0,0,W,H,0x358f24,0xc9e713);
        if (era_aura_floor) blit_quality(era_aura_floor,nullptr,screen,&all);
    } else shade(0,0,W,H,0x000000,185);
    if (!light) {
        if (blade_dark) nine_slice(blade_dark,px-18,py-18,pw+36,ph+36,24,36);
        else rounded(px,py,pw,ph,12,0x42505e,0x2c3742);
    }
    TTF_Font *head=light?(era_text?era_text:large):large, *row_font=guide::item_font?guide::item_font:regular, *body=regular;
    const bool page=model.system_page!=dash::Model::SystemRoot;
    text(model.system_page_title(),px+70,py+(light?60:40),head,{235,235,235,255});
    const auto entries=model.system_entries();
    const int n=int(entries.size()), item=std::clamp(model.viewer_item,0,std::max(0,n-1));
    const int lx=px+70, ly=py+150, lw=light?675:820, pitch=light?70:70;
    if (system_page::network_body(lx,ly,pw-140)) return;
    if (model.system_page==dash::Model::SystemDateTime) {
        if (light) shade(lx,ly,pw-140,600,0x0f1214,170);
        system_page::read_only_body(lx+30,ly+30,pw-200,body,{235,235,235,255});
        return;
    }
    if (system_page::memory_grid(lx,ly,pw-140,row_font)) return;
    const int first=std::max(0,item-8), end=std::min(n,first+9);
    // BackgroundPanel, identical in the 13604, 15574 and 17559 skins.
    if (light) gradient(lx,ly,lw,std::max(1,end-first)*pitch+20,0xe8ebeb,0xc9cfd1);
    const bool v2=metro && !model.metro_v1();
    for (int i=first;i<end;i++) {
        const int y=ly+10+(i-first)*pitch;
        const bool on=i==item;
        if (on) {
            // GreenHighlight: 13604 / 15574 77ba02 over 51860f; 17559 a flat 008a00.
            if (v2) rect(lx,y,lw,pitch,0x008a00);
            else if (light) gradient(lx,y,lw,pitch,0x77ba02,0x51860f);
            else {
                rounded(lx-2,y-2,lw+4,66,4,0x4e8e17,0x3b7410);
                rounded(lx,y,lw,62,3,0x8fd23c,0x5da516);
                rect(lx+3,y+1,lw-6,2,0xb9ea7c);
            }
        } else if (light) shade(lx,y+pitch-1,lw,1,0x9facb2,255);
        else rect(lx,y+64,lw,2,0x55636e);
        int tx=lx+(light&&!page?75:22);
        if (model.storage_page() && model.storage_row_icon(i)>=0) {
            system_page::memory_icon(i,lx+15,y+11);tx=lx+80;
        }
        if (const char *name=system_page::icon_for(entries[size_t(i)].label)) {
            auto it=blades_system_icons.find(std::string("items/")+name);
            if (it==blades_system_icons.end()) it=blades_system_icons.find(name);
            if (it!=blades_system_icons.end()) {
                if (light && !on) SDL_SetSurfaceColorMod(it->second,90,98,104);
                blit_contained(it->second,light?SDL_Rect{lx+15,y+11,48,48}:SDL_Rect{lx+16,y+11,40,40});
                SDL_SetSurfaceColorMod(it->second,255,255,255);
            }
            if (!light) tx=lx+76;
        } else if (!page && !light) tx=lx+76;
        const SDL_Color ink=!light||on?SDL_Color{235,235,235,255}:SDL_Color{0x65,0x6d,0x72,255};
        const bool device=model.system_page==dash::Model::StorageDevices;
        text(fit(entries[size_t(i)].label,row_font,lw-(tx-lx)-20),tx,y+(device?0:light?12:9),row_font,ink);
        if (device) text(fit(model.storage_row_size(i),small,lw-(tx-lx)-20),tx,y+40,small,ink);
    }
    // The meta panel: the focused row's description (SystemScene), or on the
    // Console Settings page its "Current Setting" (labMetaHeader).
    const int mx=light?lx+lw+70:px+960, my=py+150, mw=light?620:640;
    if (light) shade(mx-30,my,mw+60,600,0x0f1214,170);
    else if (metro) rect(mx-30,my-20,mw+60,600,0x333333);
    else rounded(mx-30,my-20,mw+60,600,8,0x34414d,0x26303a);
    const std::string label=entries.empty()?"":entries[size_t(item)].label;
    const int ty=light?my+24:my;
    if (model.storage_page()) {
        system_page::paragraphs(system_page::memory_body(),mx,ty,mw,body,{235,235,235,255},42);
    } else if (page) {
        text("Current Setting",mx,ty,body,{150,214,82,255});
        const int below=system_page::paragraphs(system_page::current_setting(label),mx,ty+56,mw,body,{235,235,235,255},42);
        system_page::paragraphs(model.system_description(),mx,below+35,mw,body,{235,235,235,255},42);
    } else {
        system_page::paragraphs(model.system_meta(label),mx,ty,mw,body,{235,235,235,255},42);
    }
}
