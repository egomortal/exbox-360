// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Local packed-video preparation. UI must release its Ready before requesting
// another clip. No audio prefetch/overlap; Guide retains the same Ready/session.
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <stdexcept>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace packed_prep {
struct Request { std::string key,frames_path,wav_path; uint32_t frame_count=0,fps=0; uint64_t generation=0; };
struct Identity {
    uint64_t size=0,device=0,inode=0; int64_t modified=0,modified_ns=0,changed=0,changed_ns=0;
    bool operator==(const Identity& b) const {return size==b.size&&device==b.device&&inode==b.inode&&modified==b.modified&&modified_ns==b.modified_ns&&changed==b.changed&&changed_ns==b.changed_ns;}
};
struct CloseFile {void operator()(FILE* f)const {if(f)std::fclose(f);}};
using File=std::unique_ptr<FILE,CloseFile>;
using Cancel=std::function<bool()>;
// Adapter owns the existing SDL_WAV allocation/spec in this opaque object.
// No fixed audio cap or codec restriction is introduced by this helper.
struct Audio {std::shared_ptr<void> owner;std::string error;};
using AudioLoader=std::function<Audio(const std::string&,const Cancel&)>;
struct Ready {
    // Declared first so lease is destroyed after audio/file payloads.
    std::shared_ptr<const int> lease=std::make_shared<const int>(0);
    uint64_t generation=0;std::string key;Identity identity;File file;
    std::shared_ptr<const std::vector<int64_t>> offsets;Audio audio;
    // Retain this Ready while any adopted payload is live.
};
enum class State {Idle,Preparing,Ready,Cancelled,Failed,Shutdown};
struct Status {State state=State::Idle;uint64_t generation=0;std::string error;};
struct Hooks {
    // Test seams. Production leaves these empty.
    std::function<void(uint32_t,const Cancel&)> index_entry;
    std::function<std::thread(std::function<void()>)> launch;
};
class Worker {
    struct Cache {std::string path,key;uint32_t count=0;Identity identity;std::shared_ptr<const std::vector<int64_t>> offsets;};
    AudioLoader loader_;Hooks hooks_;std::mutex mutex_;std::condition_variable cv_;std::thread thread_;
    std::optional<Request> pending_;std::unique_ptr<Ready> result_;std::weak_ptr<const int> ui_lease_;
    std::vector<Cache> cache_;Status status_;bool cleanup_pending_=false;std::atomic<uint64_t> generation_{0};std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> scans_{0},hits_{0},loads_{0};
    static bool identify(FILE* file,Identity& out) {
        struct stat s{};if(!file||fstat(fileno(file),&s)!=0||!S_ISREG(s.st_mode)||s.st_size<0)return false;
        out={uint64_t(s.st_size),uint64_t(s.st_dev),uint64_t(s.st_ino),int64_t(s.st_mtim.tv_sec),int64_t(s.st_mtim.tv_nsec),int64_t(s.st_ctim.tv_sec),int64_t(s.st_ctim.tv_nsec)};return true;
    }
    static bool u32(FILE* f,uint32_t& out) {unsigned char b[4];if(std::fread(b,1,4,f)!=4)return false;out=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;}
    bool cancelled(uint64_t id)const {return stopping_.load()||generation_.load()!=id;}
    std::unique_ptr<Ready> prepare(const Request& req,std::string& error) {
        const Cancel cancel=[this,id=req.generation]{return cancelled(id);};
        auto ready=std::make_unique<Ready>();ready->generation=req.generation;ready->key=req.key;
        ready->file.reset(std::fopen(req.frames_path.c_str(),"rb"));
        if(!identify(ready->file.get(),ready->identity)){error="Cannot open regular packed video";return {};}
        for(size_t i=0;i<cache_.size();++i)if(cache_[i].path==req.frames_path&&cache_[i].key==req.key&&cache_[i].count==req.frame_count&&cache_[i].identity==ready->identity){
            ready->offsets=cache_[i].offsets;auto entry=std::move(cache_[i]);cache_.erase(cache_.begin()+i);cache_.push_back(std::move(entry));++hits_;break;
        }
        if(!ready->offsets){
            ++scans_;char magic[4];uint32_t count=0;
            if(std::fread(magic,1,4,ready->file.get())!=4||magic[0]!='N'||magic[1]!='X'||magic[2]!='E'||magic[3]!='F'||!u32(ready->file.get(),count)||count!=req.frame_count){error="Invalid packed-video header/count";return {};}
            // Evict before allocating: at most two retained/in-construction index vectors.
            if(cache_.size()>=2)cache_.erase(cache_.begin());
            auto offsets=std::make_shared<std::vector<int64_t>>();offsets->reserve(count);
            for(uint32_t i=0;i<count;++i){
                if(cancel())return {};
                if(hooks_.index_entry)hooks_.index_entry(i,cancel);
                if(cancel())return {};
                const long offset=std::ftell(ready->file.get());uint32_t length=0;
                if(offset<0||uint64_t(offset)>ready->identity.size||ready->identity.size-uint64_t(offset)<4||!u32(ready->file.get(),length)||length==0||length>10000000u||uint64_t(length)>ready->identity.size-uint64_t(offset)-4||std::fseek(ready->file.get(),long(length),SEEK_CUR)!=0){error="Truncated or invalid packed-video frame";return {};}
                offsets->push_back(int64_t(offset));
            }
            Identity after;if(!identify(ready->file.get(),after)||!(after==ready->identity)){error="Packed video changed during preparation";return {};}
            ready->offsets=offsets;cache_.push_back({req.frames_path,req.key,req.frame_count,ready->identity,offsets});
        }
        if(cancel())return {};
        if(loader_&&!req.wav_path.empty()){
            ++loads_;
            try {ready->audio=loader_(req.wav_path,cancel);}
            catch(const std::exception& e){ready->audio.error=std::string("Audio preparation failed: ")+e.what();}
            catch(...){ready->audio.error="Audio preparation failed";}
        }
        if(cancel())return {};
        return ready;
    }
    void run() noexcept {
        for(;;){
            Request req;std::unique_ptr<Ready> stale;
            {
                std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[this]{return stopping_.load()||pending_.has_value()||cleanup_pending_;});
                stale=std::move(result_);cleanup_pending_=false;
                if(stopping_.load()){lock.unlock();stale.reset();break;}
                if(!pending_){lock.unlock();stale.reset();continue;}
                req=std::move(*pending_);pending_.reset();status_={State::Preparing,req.generation,{}};
            }
            // Dispose previous worker-owned decoded payload before ANY next load.
            stale.reset();std::unique_ptr<Ready> ready;std::string error;
            try {ready=prepare(req,error);}
            catch(const std::exception& e){error=std::string("Preparation failed: ")+e.what();}
            catch(...){error="Preparation failed";}
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!cancelled(req.generation)){
                    if(ready){result_=std::move(ready);status_={State::Ready,req.generation,{}};}
                    else status_={State::Failed,req.generation,error.empty()?"Preparation unavailable":error.substr(0,512)};
                }
            }
            // Stale in-flight result is destroyed here, before next iteration/load.
        }
        cache_.clear();
    }
public:
    explicit Worker(AudioLoader loader={},Hooks hooks={}):loader_(std::move(loader)),hooks_(std::move(hooks)){}
    Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    ~Worker(){shutdown();}
    // Returns generation, or zero for rejected request. Caller releases its Ready first.
    uint64_t request(Request req){
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_.load()){status_={State::Shutdown,generation_.load(),"Worker shut down"};return 0;}
        if(!ui_lease_.expired()){status_={State::Failed,generation_.load(),"Release active Ready before requesting another clip"};return 0;}
        if(req.key.empty()||req.key.size()>4096||req.frames_path.empty()||req.frames_path.size()>4096||req.wav_path.size()>4096||req.frame_count<1||req.frame_count>100000||req.fps<1||req.fps>120){status_={State::Failed,generation_.load(),"Invalid preparation request"};return 0;}
        req.generation=++generation_;const auto id=req.generation;
        pending_=std::move(req);status_={State::Preparing,id,{}};
        if(!thread_.joinable()){
            try {auto task=[this]{run();};thread_=hooks_.launch?hooks_.launch(task):std::thread(task);if(!thread_.joinable())throw std::runtime_error("thread launch returned no thread");}
            catch(...){pending_.reset();status_={State::Failed,id,"Preparation thread unavailable"};return 0;}
        }
        cv_.notify_one();return id;
    }
    void cancel(){std::lock_guard<std::mutex> lock(mutex_);++generation_;pending_.reset();cleanup_pending_=true;status_={State::Cancelled,generation_.load(),{}};cv_.notify_one();}
    std::unique_ptr<Ready> poll(){std::lock_guard<std::mutex> lock(mutex_);if(!result_||result_->generation!=generation_.load())return {};auto out=std::move(result_);ui_lease_=out->lease;status_={State::Idle,out->generation,{}};return out;}
    Status status(){std::lock_guard<std::mutex> lock(mutex_);return status_;}
    void shutdown(){
        {std::lock_guard<std::mutex> lock(mutex_);if(!stopping_.exchange(true)){++generation_;pending_.reset();status_={State::Shutdown,generation_.load(),{}};}cv_.notify_one();}
        if(thread_.joinable())thread_.join();
    }
    uint64_t scans()const{return scans_.load();}uint64_t cache_hits()const{return hits_.load();}uint64_t audio_loads()const{return loads_.load();}
};
}
