#include "../xui_renderer.hpp"
#include <cassert>
#include <cstdio>
static uint32_t pixel(SDL_Surface *s,int x,int y) {return reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s->pixels)+y*s->pitch)[x];}
static SDL_Surface *bitmap(uint32_t color) {auto *s=SDL_CreateRGBSurfaceWithFormat(0,4,2,32,SDL_PIXELFORMAT_ARGB8888);SDL_FillRect(s,nullptr,color);return s;}
int main(int argc,char **argv) {
    if (argc<2) return 2;
    SDL_SetMainReady();assert(!SDL_Init(SDL_INIT_VIDEO));assert(!TTF_Init());
    xui::Scene empty;empty.valid=true;
    xui::Renderer base_renderer;assert(base_renderer.prepare(empty,64,40,argv[1],1));
    auto *pattern=SDL_CreateRGBSurfaceWithFormat(0,64,40,32,SDL_PIXELFORMAT_ARGB8888);
    assert(pattern);
    for (int y=0;y<40;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pattern->pixels)+y*pattern->pitch);
        for (int x=0;x<64;x++) row[x]=SDL_MapRGBA(pattern->format,Uint8(x*3),Uint8(y*5),Uint8(x+y),255);
    }
    base_renderer.draw(empty,xui::Matrix::identity(),false,pattern);
    for (int y=0;y<40;y++) for (int x=0;x<64;x++) assert(pixel(base_renderer.canvas,x,y)==pixel(pattern,x,y));
    base_renderer.draw(empty,xui::Matrix::identity());
    for (int y=0;y<40;y++) for (int x=0;x<64;x++) assert(pixel(base_renderer.canvas,x,y)==0xffbfc2c4);
    SDL_FreeSurface(pattern);base_renderer.clear();
    std::puts("Opaque backdrop copy exact across 2560 pixels; default clear restored exactly");
    xui::Scene scene;scene.valid=true;scene.nodes.resize(3);scene.state.resize(3);scene.world.resize(3);scene.strings={'\0'};
    for (auto &n:scene.nodes) {
        n.parent=-1;n.branch_owner=-1;n.p[xui::Width].v[0]=10;n.p[xui::Height].v[0]=10;
        n.p[xui::Opacity].v[0]=n.p[xui::Show].v[0]=1;n.p[xui::Rotation].v[3]=1;
        n.p[xui::Scale].v[0]=n.p[xui::Scale].v[1]=n.p[xui::Scale].v[2]=1;
    }
    auto &figure=scene.nodes[0];figure.kind=1;figure.fill_type=1;figure.path_w=figure.path_h=10;figure.closed=1;figure.point_count=4;
    figure.p[xui::FillColor].token=0xffffffff;figure.p[xui::StrokeColor].token=0xff000000;figure.p[xui::StrokeWidth].v[0]=2;
    scene.points={{2,2},{8,2},{8,8},{2,8}};
    auto &image=scene.nodes[1];image.kind=3;image.p[xui::Position].v[0]=12;image.p[xui::ImageMode].v[0]=4;
    assert(scene.bind_string(1,xui::Texture,"host://art"));
    auto &text=scene.nodes[2];text.kind=2;text.p[xui::Position].v[1]=16;text.p[xui::Width].v[0]=45;text.p[xui::Height].v[0]=16;
    text.p[xui::PointSize].v[0]=9;text.p[xui::TextScale].v[0]=1;text.p[xui::TextColor].token=0xff00ff00;
    assert(scene.bind_string(2,xui::Text,"long dynamic label"));text.p[xui::TextStyle].v[0]=xui::TextSingleLine|xui::TextEllipsis;
    xui::Renderer renderer;assert(renderer.prepare(scene,64,40,argv[1],2));renderer.bind_bitmap("host://art",bitmap(0xffff0000));
    auto *backdrop=SDL_CreateRGBSurfaceWithFormat(0,64,40,32,SDL_PIXELFORMAT_ARGB8888);
    assert(backdrop);
    SDL_FillRect(backdrop,nullptr,0xff112233);
    renderer.draw(scene,xui::Matrix::identity(),false,backdrop);
    assert(pixel(renderer.canvas,63,39)==0xff112233);
    assert(pixel(renderer.canvas,5,5)==0xffffffff);
    SDL_FreeSurface(backdrop);
    renderer.draw(scene,xui::Matrix::identity());assert(pixel(renderer.canvas,5,5)==0xffffffff);assert(pixel(renderer.canvas,2,5)==0xff000000);
    assert(pixel(renderer.canvas,14,4)==0xffff0000);renderer.bind_bitmap("host://art",bitmap(0xff0000ff));renderer.draw(scene,xui::Matrix::identity());
    assert(pixel(renderer.canvas,14,4)==0xff0000ff);
    assert(scene.bind_string(1,xui::Texture,""));renderer.draw(scene,xui::Matrix::identity());
    assert(pixel(renderer.canvas,14,4)==0xffbfc2c4);
    for (int y=16;y<40;y++) for (int x=45;x<64;x++) assert(pixel(renderer.canvas,x,y)==0xffbfc2c4);
    figure.closed=0;renderer.draw(scene,xui::Matrix::identity());assert(pixel(renderer.canvas,5,5)==0xffffffff);
    figure.fill_type=0;renderer.draw(scene,xui::Matrix::identity());assert(pixel(renderer.canvas,5,5)==0xffbfc2c4);
    assert(pixel(renderer.canvas,5,2)==0xff000000);assert(pixel(renderer.canvas,2,5)==0xffbfc2c4);
    figure.closed=1;figure.stop_first=0;figure.stop_count=2;figure.p[xui::StrokeWidth].v[0]=0;
    scene.points={{0,0},{10,0},{10,10},{0,10}};
    scene.original_stops={{0,0xff000000},{1,0xffffffff}};
    scene.stops=scene.original_stops;
    assert(renderer.prepare(scene,64,40,argv[1],2));
    int gradient_checks=0;
    for (int type:{2,3}) for (float angle:{0.f,37.f,90.f})
        for (auto scale:std::array<xui::Point,5>{{{1,1},{2,.5f},{-1,.5f},{0,0},{0,2}}}) {
            figure.fill_type=type;figure.p[xui::FillRotation].v[0]=angle;
            figure.p[xui::FillScale].v[0]=scale.x;figure.p[xui::FillScale].v[1]=scale.y;
            figure.p[xui::FillTranslation].v[0]=.1f;figure.p[xui::FillTranslation].v[1]=-.05f;
            renderer.draw(scene,xui::Matrix::identity());
            const double radians=double(angle)*3.141592653589793/180;
            for (int y=3;y<8;y++) for (int x=3;x<8;x++) {
                const double u=((x+.5)/10-.5)*scale.x,v=((y+.5)/10-.5)*scale.y;
                const double gu=u*std::cos(radians)-v*std::sin(radians)+.1;
                const double gv=u*std::sin(radians)+v*std::cos(radians)-.05;
                const double phase=type==2?gu+.5:2*std::hypot(gu,gv);
                const int expected=int(std::lround(std::clamp(phase,0.,1.)*255));
                const auto actual=pixel(renderer.canvas,x,y);
                for (int shift:{0,8,16}) assert(std::abs(int((actual>>shift)&255)-expected)<=1);
                assert((actual>>24)==255);gradient_checks++;
            }
        }
    std::printf("%d signed/zero/nonuniform rotated gradient samples passed\n",gradient_checks);
    renderer.clear();TTF_Quit();SDL_Quit();std::puts("Dynamic bitmap replacement, stroke paths and text clipping passed");
}
