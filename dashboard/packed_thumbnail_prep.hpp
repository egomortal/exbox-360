// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// File I/O only. SDL decoding/surfaces and the thumbnail cache stay on the UI.
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

namespace packed_thumb {
constexpr size_t kPending=8, kMaxBytes=10000000, kMaxKey=512, kMaxPath=4096;
struct Request {std::string key,path;uint32_t frames=0;uint64_t generation=0;};
struct Result {
    // Lease keeps the worker from starting another allocation while UI consumes
    // this result. It is destroyed last, after encoded bytes and metadata.
    std::shared_ptr<const int> lease=std::make_shared<const int>(0);
    uint64_t generation=0;std::string key,error;uint32_t frame=0;
    std::vector<unsigned char> jpeg;
};
enum class Enqueue {Queued,Pending,Full,Unavailable};
using Cancel=std::function<bool()>;
struct Hooks { // Test seams; empty in production.
    std::function<void(uint32_t,const Cancel&)> scan;
    std::function<std::thread(std::function<void()>)> launch;
};
class Worker {
    std::mutex mutex_;std::condition_variable cv_;std::thread thread_;
    std::deque<Request> pending_;std::set<std::string> keys_;
    std::unique_ptr<Result> result_;std::weak_ptr<const int> ui_lease_;
    std::atomic<uint64_t> generation_{1};std::atomic<bool> stopping_{false};
    Hooks hooks_;bool started_=false,unavailable_=false,discard_=false;
    static bool u32(FILE* f,uint32_t& v) {
        unsigned char b[4];if(std::fread(b,1,4,f)!=4)return false;
        v=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;
    }
    static bool same_file(const struct stat& a,const struct stat& b) {
        return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&
            a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
            a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
    }
    bool cancelled(uint64_t gen)const {return stopping_.load()||generation_.load()!=gen;}
    std::unique_ptr<Result> read(const Request& req) {
        auto out=std::make_unique<Result>();out->key=req.key;out->generation=req.generation;
        // Identical selection to the old synchronous video_thumb path.
        out->frame=req.frames/3;
        auto fail=[&](const char* error){out->jpeg.clear();out->error=error;return std::move(out);};
        const Cancel cancel=[this,gen=req.generation]{return cancelled(gen);};
        if(cancel())return {};
        std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(req.path.c_str(),"rb"),&std::fclose);
        struct stat before{};
        if(!file||fstat(fileno(file.get()),&before)||!S_ISREG(before.st_mode)||before.st_size<8)return fail("Cannot read regular packed thumbnail");
        char magic[4];uint32_t count=0;
        if(std::fread(magic,1,4,file.get())!=4||magic[0]!='N'||magic[1]!='X'||magic[2]!='E'||magic[3]!='F'||
           !u32(file.get(),count)||count!=req.frames||uint64_t(count)*5+8>uint64_t(before.st_size))return fail("Invalid thumbnail header/count");
        for(uint32_t i=0;i<=out->frame;++i) {
            if(cancel())return {};
            if(hooks_.scan)hooks_.scan(i,cancel);
            if(cancel())return {};
            const off_t offset=ftello(file.get());uint32_t bytes=0;
            if(offset<0||uint64_t(offset)>uint64_t(before.st_size)||uint64_t(before.st_size)-uint64_t(offset)<4||
               !u32(file.get(),bytes)||bytes==0||bytes>=kMaxBytes||uint64_t(bytes)>uint64_t(before.st_size)-uint64_t(offset)-4)return fail("Invalid thumbnail frame bounds");
            if(i==out->frame) {
                out->jpeg.resize(bytes);
                if(std::fread(out->jpeg.data(),1,bytes,file.get())!=bytes)return fail("Truncated thumbnail JPEG");
            } else if(fseeko(file.get(),off_t(bytes),SEEK_CUR)!=0)return fail("Invalid thumbnail offset");
        }
        if(cancel())return {};
        struct stat after{};
        if(fstat(fileno(file.get()),&after)||!same_file(before,after))return fail("Packed thumbnail changed during read");
        return out;
    }
    void run() noexcept {
        for(;;) {
            Request req;std::unique_ptr<Result> stale;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                for(;;) {
                    if(discard_){
                        stale=std::move(result_);discard_=false;
                        lock.unlock();stale.reset();lock.lock();
                        continue;
                    }
                    if(stopping_.load())return;
                    if(!result_&&!pending_.empty()&&ui_lease_.expired())break;
                    // A UI lease has no callback into the worker lifetime.
                    // Only this bounded handoff case uses a timed wait.
                    if(!pending_.empty()&&!ui_lease_.expired())cv_.wait_for(lock,std::chrono::milliseconds(10));
                    else cv_.wait(lock);
                }
                req=std::move(pending_.front());pending_.pop_front();
            }
            stale.reset();std::unique_ptr<Result> value;
            try {value=read(req);}
            catch(...) {
                try {value=std::make_unique<Result>();value->key=req.key;value->generation=req.generation;value->error="Thumbnail preparation unavailable";}
                catch(...) {std::lock_guard<std::mutex> lock(mutex_);unavailable_=true;pending_.clear();keys_.clear();}
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!cancelled(req.generation))result_=std::move(value);
            }
            // Cancelled in-flight bytes are destroyed before another request.
        }
    }
public:
    explicit Worker(Hooks hooks={}):hooks_(std::move(hooks)){}
    Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    ~Worker(){shutdown();}
    Enqueue request(const std::string& key,const std::string& path,uint32_t frames) {
        if(key.empty()||key.size()>kMaxKey||path.empty()||path.size()>kMaxPath||frames==0||frames>100000)return Enqueue::Unavailable;
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_.load()||unavailable_)return Enqueue::Unavailable;
        if(keys_.count(key))return Enqueue::Pending;
        if(pending_.size()>=kPending)return Enqueue::Full;
        try {
            if(!started_){thread_=hooks_.launch?hooks_.launch([this]{run();}):std::thread([this]{run();});if(!thread_.joinable())throw 0;started_=true;}
            pending_.push_back({key,path,frames,generation_.load()});
            try {keys_.insert(key);}catch(...){pending_.pop_back();throw;}
        } catch(...) {unavailable_=true;pending_.clear();keys_.clear();cv_.notify_one();return Enqueue::Unavailable;}
        cv_.notify_one();return Enqueue::Queued;
    }
    std::unique_ptr<Result> poll() {
        std::lock_guard<std::mutex> lock(mutex_);cv_.notify_one();
        if(!result_||result_->generation!=generation_.load())return {};
        auto value=std::move(result_);keys_.erase(value->key);ui_lease_=value->lease;return value;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);++generation_;
        pending_.clear();keys_.clear();discard_=true;cv_.notify_one();
    }
    void shutdown() {
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;++generation_;pending_.clear();keys_.clear();cv_.notify_one();}
        if(thread_.joinable())thread_.join();
        result_.reset();
    }
};
} // namespace packed_thumb
