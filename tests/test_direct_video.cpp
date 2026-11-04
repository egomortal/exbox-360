#include "../modules/direct_video/session.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char** argv) {
    try {
        require(argc == 4, "usage: test-direct-video FILE OUTPUT_PREFIX pixels|packets");
        exbox::video::Session session;
        require(!session.open("https://example.invalid/a.mp4", false), "URL accepted");
        require(!session.open("/dev/zero", false), "device accepted");
        require(!session.open("/tmp", false), "directory accepted");
        require(!session.seek(0), "closed seek accepted");
        bool packets = std::string(argv[3]) == "packets";
        require(session.open(argv[1], packets), session.error().c_str());
        require(!session.seek(-1) && !session.seek(std::numeric_limits<double>::infinity()), "invalid seek accepted");
        require(!session.seek(session.duration()+1), "out of range seek accepted");
        std::ofstream pixels(std::string(argv[2])+".rgba",std::ios::binary);
        std::ofstream audio(std::string(argv[2])+".pcm",std::ios::binary);
        size_t frames=0,samples=0; double previous=-1; bool reachedEnd=false;
        for (size_t budget=0; budget<100000; ++budget) {
            OmniMedia::VideoFrame frame; OmniMedia::DemuxPacket packet;
            auto result=packets?session.nextPacket(packet):session.next(frame);
            auto pcm=session.audio();samples+=pcm.size();
            audio.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(int16_t));
            require(result!=exbox::video::Session::Result::Error, session.error().c_str());
            if(result==exbox::video::Session::Result::End) {reachedEnd=true;break;}
            if(result!=exbox::video::Session::Result::Frame) continue;
            ++frames;
            if(packets) {
                require(packet.timeBase.denominator>0 && !packet.bytes.empty(), "invalid packet");
            } else {
                require(frame.seconds>=previous, "presentation timestamps out of order");previous=frame.seconds;
                require(frame.rgba.size()==size_t(frame.width)*frame.height*4, "invalid pixel extent");
                pixels.write(reinterpret_cast<const char*>(frame.rgba.data()),frame.rgba.size());
            }
        }
        require(reachedEnd,"packet/frame budget exhausted before EOF");
        require(frames>0,"no frames");
        require(!session.hasAudio() || samples>0,"missing audio");
        require(session.seek(0), "rewind failed");
        require(session.seek(session.duration()/2), "midpoint seek failed");
        // Packet-mode seek returns dependency preroll. Only decoded pictures may be display-trimmed.
        if(!packets) {
            OmniMedia::VideoFrame frame;
            auto result=session.next(frame);
            for(int retry=0;result==exbox::video::Session::Result::Again && retry<100;++retry)result=session.next(frame);
            require(result==exbox::video::Session::Result::Frame && frame.seconds+0.001>=session.duration()/2,"seek presentation not trimmed");
        }
        for(int i=0;i<10;++i) {session.close();require(session.open(argv[1],packets),"reopen failed");}
        std::cout<<"frames="<<frames<<" pcm_samples="<<samples<<" packet_mode="<<packets<<" reopen=10 seek=pass\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
