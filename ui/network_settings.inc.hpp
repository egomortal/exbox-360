// Included in system_page, after its paragraph helper.
// NetworkMain.xur ConnectBar and TestingScene (9199/13604/15574/17559),
// 6770's 2010_TestingNetwork labels, within each era's settings chrome.
bool network_body(int x,int y,int width) {
    if (!model.network_page()) return false;
    using R=dash::NetworkResult;
    const bool nxe=model.era==1, light=model.era>=2;
    const SDL_Color white{235,235,235,255}, dark{0x65,0x6d,0x72,255};
    const int cell=width/4;
    const bool testing=model.system_page==dash::Model::NetworkTesting;
    const bool summary=testing || model.system_page==dash::Model::NetworkMain;
    // Kinect/Metro: white text over the light dashboard floor sits on the dark
    // RightPanelShader, as the other settings pages' meta text does.
    constexpr Uint32 shader=0x0f1214;
    if (summary && light) shade(x,y-10,width,185,shader,170);
    if (summary) {
        const bool live_case=(model.era==0 && model.dashboard_build<5759) || (model.era==3 && !model.metro_v1());
        const char *names[]={"Network Adapter","Network","Internet",live_case?"Xbox Live":"Xbox LIVE"};
        const int icons[]={0,model.network.medium==dash::NetworkMedium::Wireless?3:1,2,4};
        for (int i=0;i<4;++i) {
            const int cx=x+i*cell;
            if (network_art[icons[i]]) blit_contained(network_art[icons[i]],{cx+20,y,96,96});
            text(names[i],cx+8,y+100,small,white);
            const R result=model.network_test.steps[size_t(i)];
            std::string value;
            if (result==R::Running) value=model.era==0?"Pending":"Testing...";
            if (result==R::Passed) value=model.era==0?"Pass":"Connected";
            if (result==R::Failed) value=model.era==0?"Fail":"Failed";
            // Ex-Box addition: no 360 original. LIVE cannot be tested on PS5.
            if (i==3) value="Not available on PS5 yet.";
            text(fit(value,small,cell-12),cx+8,y+137,small,white);
            if (i<3) {
                SDL_Surface *link=result==R::Passed?network_art[5]:result==R::Failed?network_art[6]:nullptr;
                if (link) blit_contained(link,{cx+132,y+30,cell-155,32});
                if (result==R::Running && network_art[7]) {
                    const int travel=std::max(1,cell-165);
                    blit_contained(network_art[7],{cx+120+int(SDL_GetTicks()/12%Uint32(travel)),y+30,32,32});
                }
            }
        }
        y+=205;
    }
    const auto entries=model.network_entries();
    const int left=std::min(640,width/2), pitch=66;
    TTF_Font *font=guide::item_font?guide::item_font:regular;
    if (light && !summary && model.system_page!=dash::Model::NetworkDetails && !entries.empty())
        shade(x+left+20,y,width-left-20,int(entries.size())*pitch,shader,170);
    if (light && (summary || model.system_page==dash::Model::NetworkDetails))
        shade(x+left+20,y,width-left-20,300,shader,170);
    for (size_t i=0;i<entries.size();++i) {
        const int row=y+int(i)*pitch;
        const bool on=int(i)==model.viewer_item;
        if (light) {
            if (on && model.era==3 && !model.metro_v1()) rect(x,row,left,pitch-3,0x008a00);
            else if (on) gradient(x,row,left,pitch-3,0x77ba02,0x51860f);
            else gradient(x,row,left,pitch-3,0xe8ebeb,0xc9cfd1);
        } else if (nxe) {
            shade(x,row,left,pitch-3,0x25333e,100);
            if (on) guide::bg::ring(x,row,left,pitch-3,6,3,0x69a50f,255);
        } else if (on) rounded(x,row,left,pitch-3,4,0x8fd23c,0x5da516);
        text(fit(entries[i].label,font,left-30),x+15,row+10,font,light&&!on?dark:white);
        if (!summary && model.system_page!=dash::Model::NetworkDetails)
            text(fit(model.network_value(entries[i].label),regular,width-left-60),x+left+40,row+12,regular,white);
    }
    if (summary || model.system_page==dash::Model::NetworkDetails) {
        const int mx=x+left+40, mw=width-left-50;
        std::string body="Network Adapter: "+model.network_value("Network Adapter")+"\n";
        body+="IP Address: "+model.network.address+"\nSubnet Mask: "+model.network.mask+"\nGateway: "+model.network.gateway;
        body+="\nPrimary DNS Server: "+model.network.dns;
        paragraphs(body,mx,y,mw,small,white,40);
        // A missing signal reading stays blank, including on the PS5.
        if (model.network.medium==dash::NetworkMedium::Wireless && model.network.signal>=0 && model.network.signal<=4) {
            auto *bars=network_art[8+model.network.signal];
            if (bars) blit_contained(bars,{mx,y+230,48,48});
        }
    }
    return true;
}
