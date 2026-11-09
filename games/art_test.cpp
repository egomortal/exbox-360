#include "games_art.hpp"
#include <cassert>
#include <future>
#include <iostream>
namespace fs=std::filesystem;
using namespace exbox_games::art;
int main(int argc,char** argv) {
    assert(argc==2);
    const fs::path root=argv[1];
    fs::create_directories(root);
    png_image png{}; png.version=PNG_IMAGE_VERSION; png.width=2; png.height=2; png.format=PNG_FORMAT_RGBA;
    const unsigned char pixels[]={255,0,0,255,0,255,0,255,0,0,255,255,0,0,0,0};
    assert(png_image_write_to_file(&png,(root/"original.png").c_str(),0,pixels,0,nullptr));
    const Limits limits{1024*1024,1024*1024,512};
    Slot slot;
    auto request=slot.begin("game:one",root,"original.png");
    assert(slot.current.pixels==placeholder());
    auto work=std::async(std::launch::async,[=]{return load(request,limits);});
    assert(slot.complete(request,work.get()));
    assert(slot.current.status==Status::Ready && slot.current.pixels->width==2);
    assert(slot.current.pixels->rgba==std::vector<unsigned char>(pixels,pixels+16));
    auto stale=request;
    request=slot.begin("game:two",root,"missing.png");
    assert(!slot.complete(stale,{placeholder(),Status::Ready}));
    assert(slot.complete(request,load(request,limits)) && slot.current.status==Status::Missing);
    fs::copy_file(root/"original.png",root/"missing.png",fs::copy_options::overwrite_existing);
    request=slot.begin("game:two",root,"missing.png");
    assert(slot.complete(request,load(request,limits)) && slot.current.status==Status::Ready);
    request.relative="../original.png"; assert(load(request,limits).status==Status::Rejected);
    request.relative="/tmp/original.png"; assert(load(request,limits).status==Status::Rejected);
    request.relative="C:original.png"; assert(load(request,limits).status==Status::Rejected);
    request.relative="cover.jpg"; assert(load(request,limits).status==Status::Rejected);
    request.relative="original.png";
    assert(load(request,{1,1024,512}).status==Status::TooLarge);
    assert(load(request,{1024*1024,4,512}).status==Status::TooLarge);
    assert(load(request,{1024*1024,1024,1}).status==Status::TooLarge);
    std::ofstream(root/"broken.png")<<"not an image";
    request.relative="broken.png"; assert(load(request,limits).status==Status::Invalid);
    request.root.clear(); assert(load(request,limits).status==Status::Placeholder);
    Catalog catalog{root,{{"cover:one","original.png"}}};
    request=catalog.request(slot,"game:one","cover:one");
    assert(load(request,limits).status==Status::Ready);
    request=catalog.request(slot,"game:two","unknown");
    assert(load(request,limits).status==Status::Placeholder);
    const auto outside=root.parent_path()/"outside-original.png";
    fs::copy_file(root/"original.png",outside,fs::copy_options::overwrite_existing);
    fs::create_symlink(outside,root/"escape.png");
    request=slot.begin("game:one",root,"escape.png");
    assert(load(request,limits).status==Status::Rejected);
    assert(placeholder()->rgba.size()==32*48*4);
    std::cout<<"art gate PASS: decode, placeholder, refresh, stale worker, path, malformed, budgets\n";
}
