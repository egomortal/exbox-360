#include "../xui_navigation.hpp"
#include "../xui_viewport.hpp"
#include "../xui_text.hpp"
#include "../xui_image.hpp"
#include <cassert>
#include <iostream>
bool near(float a,float b) {return std::abs(a-b)<.001f;}
int main(int argc,char **argv) {
    const auto measure=[](const std::string &s){return float(xui::utf8_characters(s).size()*10);};
    const auto wrapped=xui::layout_text("alpha beta gamma",65,xui::TextWordBreak,measure);
    assert(wrapped.size()==3 && wrapped[0].text=="alpha" && wrapped[1].text=="beta");
    const auto clipped=xui::layout_text("abcdef",40,xui::TextSingleLine|xui::TextEllipsis,measure);
    assert(clipped.size()==1 && clipped[0].text=="abc\xe2\x80\xa6");
    assert(xui::layout_text("a\nb",100,xui::TextSingleLine,measure)[0].text=="a");
    const auto natural=xui::image_placement(100,100,200,50,0),center=xui::image_placement(100,100,200,50,2);
    const auto stretch=xui::image_placement(100,100,200,50,4),fit=xui::image_placement(100,100,200,50,8),fit_center=xui::image_placement(100,100,200,50,16);
    assert(near(natural.width,200) && near(center.x,-50) && near(center.y,25));
    assert(near(stretch.height,100) && near(fit.height,25) && near(fit_center.y,37.5f));
    assert(!xui::image_placement(100,100,200,50,1).clip);
    xui::Scene event_scene;event_scene.valid=true;event_scene.nodes.resize(1);event_scene.state.resize(1);event_scene.world.resize(1);
    event_scene.strings={'\0','s','f','x','\0'};event_scene.animations.push_back({0,0,0,0,1,10,0});
    event_scene.tracks.push_back({0,0,xui::File,-1,0,1});xui::Key sound_key{};sound_key.frame=3;sound_key.value.token=1;
    event_scene.keys.push_back(sound_key);event_scene.playback.push_back({0,true});
    event_scene.advance(3);assert(event_scene.sound_events.size()==1);event_scene.sound_events.clear();
    event_scene.advance(1);assert(event_scene.sound_events.empty());
    event_scene.seek(0,0);event_scene.advance(9);assert(event_scene.sound_events.empty());
    assert(event_scene.bind_string(0,xui::Text,"Native game title"));event_scene.evaluate();
    assert(std::string(event_scene.str(event_scene.state[0][xui::Text].token))=="Native game title");
    assert(xui::composite(0xff406080,0xff80c040,1,1,xui::Normal)==0xff80c040);
    assert(xui::composite(0xff406080,0x0080c040,0,1,xui::Multiply)==0xff204820);
    assert(xui::composite(0xff406080,0x0080c040,0,1,xui::Minimum)==0xff406040);
    assert(xui::composite(0xff406080,0x0080c040,0,1,xui::Maximum)==0xff80c080);
    assert(xui::composite(0xff406080,0x8080c040,1,1,xui::Add)==0xff80c0a0);
    assert(xui::composite(0xff406080,0xff80c040,1,1,xui::Subtract)==0xff406000);
    const auto hd=xui::viewport(1280,720),full=xui::viewport(1920,1080),four_three=xui::viewport(960,720);
    assert(near(hd.a[0][0],8.f/7) && near(hd.a[1][0],0) && near(hd.a[1][1],12.f/11) && near(hd.a[1][3],-64));
    assert(near(full.a[1][3],-96) && near(four_three.a[0][0],6.f/7) && near(four_three.a[1][1],72.f/77));
    const auto original=(xui::inverse_viewport(1920,1080)*full).point(321,456);assert(near(original.x,321)&&near(original.y,456));
    xui::Key a{},b{};a.ease_scale=b.ease_scale=50;b.frame=10;b.value.v[0]=100;
    assert(near(xui::interpolate(a,b,5,xui::Position).v[0],50));
    a.interpolation=1;assert(near(xui::interpolate(a,b,9.9f,xui::Position).v[0],0));
    a.interpolation=2;a.ease_in=100;a.ease_out=0;
    assert(near(xui::interpolate(a,b,5,xui::Position).v[0],37.5f));
    a.value.token=0x00102030;b.value.token=0xff304050;a.interpolation=0;
    assert(xui::interpolate(a,b,5,xui::FillColor).token==0x80203040);
    a.value={};b.value={};a.value.v[3]=1;b.value.v[3]=-1;
    const auto rotation=xui::interpolate(a,b,5,xui::Rotation);
    assert(near(rotation.v[3],1));
    xui::Value props[xui::FieldCount]{};props[xui::Scale].v[0]=props[xui::Scale].v[1]=props[xui::Scale].v[2]=1;
    props[xui::Rotation].v[3]=1;props[xui::Pivot].v[0]=10;props[xui::Scale].v[0]=2;
    auto matrix=xui::local(props,5,0);assert(near(matrix.point(10,0).x,15));assert(near(matrix.point(0,0).x,-5));
    xui::Scene anchored;anchored.valid=true;anchored.nodes.resize(2);anchored.state.resize(2);anchored.world.resize(2);
    for (int i=0;i<2;i++) {
        auto &n=anchored.nodes[i];n.parent=i-1;n.branch_owner=-1;n.p[xui::Width].v[0]=i?30:200;n.p[xui::Height].v[0]=100;
        n.p[xui::Opacity].v[0]=n.p[xui::Show].v[0]=1;n.p[xui::Scale].v[0]=n.p[xui::Scale].v[1]=n.p[xui::Scale].v[2]=1;n.p[xui::Rotation].v[3]=1;
    }
    anchored.nodes[1].design_w=100;anchored.nodes[1].design_h=100;anchored.nodes[1].p[xui::Anchor].v[0]=5;
    anchored.evaluate();assert(near(anchored.world[1].width,130));
    anchored.nodes[1].p[xui::Anchor].v[0]=4;anchored.evaluate();assert(near(anchored.world[1].matrix.point(0,0).x,100));
    anchored.nodes[1].p[xui::Anchor].v[0]=16;anchored.evaluate();assert(near(anchored.world[1].matrix.point(0,0).x,50));
    anchored.nodes[1].p[xui::Anchor].v[0]=64;anchored.nodes[1].p[xui::Position].v[0]=10;anchored.evaluate();
    assert(near(anchored.world[1].width,60));assert(near(anchored.world[1].matrix.point(0,0).x,20));
    anchored.nodes[0].p[xui::Blend].v[0]=xui::Maximum;anchored.nodes[0].p[xui::Opacity].v[0]=0;
    anchored.evaluate();assert(anchored.world[0].visible && anchored.world[1].visible && anchored.world[1].blend==xui::Maximum);
    anchored.nodes[1].p[xui::Blend].v[0]=xui::Normal;anchored.evaluate();assert(!anchored.world[1].visible);
    for (int i=1;i<argc;i++) {
        xui::Scene scene;assert(scene.load(argv[i]));assert(scene.seek_name(0,"1To2",true));scene.advance(10000);scene.evaluate();
        bool stopped=false;
        for (size_t j=0;j<scene.animations.size();j++) if (scene.animations[j].owner==0) {
            assert(!scene.playback[j].playing);
            for (const auto &r:scene.ranges) if (!std::strcmp(scene.str(r.name),"1To2End")) assert(near(scene.playback[j].frame,r.frame));
            stopped=true;
        }
        assert(stopped);
        for (const auto &track:scene.tracks) for (int k=track.key_first;k<track.key_first+track.key_count;k++) {
            scene.playback[track.animation].frame=scene.keys[k].frame;scene.evaluate();
            const auto &expected=scene.keys[k].value;
            if (track.field==xui::StopPosition) assert(near(scene.stops[scene.nodes[track.target].stop_first+track.index].position,expected.v[0]));
            else if (track.field==xui::StopColor) assert(scene.stops[scene.nodes[track.target].stop_first+track.index].color==expected.token);
            else if (xui::color_field(track.field) || track.field==xui::Visual || track.field==xui::Text || track.field==xui::Texture || track.field==xui::File)
                assert(scene.state[track.target][track.field].token==expected.token);
            else for (int c=0;c<4;c++) assert(near(scene.state[track.target][track.field].v[c],expected.v[c]));
        }
        const bool early=scene.nodes.size()<700;
        xui::Navigation navigation;assert(navigation.bind(scene,early,early?1:0));
        const int first=early?1:0;
        for (int from=first;from<5;from++) for (int to=first;to<5;to++) {
            assert(navigation.reset(from));navigation.request(to);
            for (int step=0;step<1000 && (navigation.current()!=to || navigation.transitioning());step++) navigation.advance(1);
            assert(navigation.current()==to && !navigation.transitioning());
        }
        // A reverse request retains the exact current pose until a source stop.
        navigation.reset(first);navigation.request(4);navigation.advance(0);navigation.advance(5);
        const float before=navigation.frame();navigation.request(first);assert(near(before,navigation.frame()));
        for (int step=0;step<1000;step++) navigation.advance(1);
        assert(navigation.current()==first && !navigation.transitioning());
        // Unnamed nested timelines wrap into their own authored ranges.
        for (size_t j=0;j<scene.animations.size();j++) if (scene.animations[j].automatic && scene.animations[j].last>1) {
            scene.playback[j]={scene.animations[j].last-1,true};scene.advance(2);assert(near(scene.playback[j].frame,1));
        }
        std::cout<<argv[i]<<": "<<scene.keys.size()<<" original key endpoints evaluated\n";
    }
    std::cout<<"XUI interpolation, transforms, anchors and named-stop checks passed\n";
}
