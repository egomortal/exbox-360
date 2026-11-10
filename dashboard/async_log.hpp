#pragma once
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace exbox_log {
// Fixed queued + in-flight payload bound: (64+1)*2048 = 130 KiB.
// append serializes whole messages. Saturation deliberately applies blocking
// backpressure; no universal producer latency guarantee is made under overload.
class AsyncLog {
public:
    static constexpr std::size_t slots=64, chunk_bytes=2048;
    struct Sink {void *context=nullptr;bool (*write)(void*,const char*,std::size_t,bool,bool)=nullptr;};
    struct Status {
        std::uint64_t accepted=0,written=0,failed=0,rejected=0,start_failures=0,waits=0;
        std::size_t queued=0,in_flight=0,high_water_chunks=0;
        bool started=false,closed=false;
        bool ok()const{return failed==0&&rejected==0&&start_failures==0;}
    };
    using Launch=std::thread(*)(void(*)(void*),void*);
    AsyncLog()=default;
    AsyncLog(const AsyncLog&)=delete;
    AsyncLog&operator=(const AsyncLog&)=delete;
    ~AsyncLog(){close();}
    static std::thread launch_thread(void(*entry)(void*),void*context){return std::thread(entry,context);}
    bool start(Sink sink,Launch launch=launch_thread){
        std::lock_guard<std::mutex> producer(producer_);
        std::lock_guard<std::mutex> lock(mutex_);
        if(status_.started||status_.closed)return false;
        if(!sink.write){++status_.start_failures;return false;}
        sink_=sink;
        try{worker_=launch(&AsyncLog::entry,this);}
        catch(...){++status_.start_failures;return false;}
        if(!worker_.joinable()){++status_.start_failures;return false;}
        status_.started=true;return true;
    }
    // Prefix is already formatted at call time. Neither timestamp formatting nor
    // caller-owned pointers cross into the writer thread. Empty messages retain
    // their newline; embedded/trailing newlines are preserved without normalization.
    bool append(const char *prefix,const char *message){
        std::lock_guard<std::mutex> producer(producer_);
        {std::lock_guard<std::mutex> lock(mutex_);if(!status_.started||status_.closed){++status_.rejected;return false;}}
        if(!prefix)prefix="";if(!message)message="";
        const char*parts[]={prefix,message,"\n"};
        const std::size_t lengths[]={std::strlen(prefix),std::strlen(message),1};
        std::size_t part=0,offset=0;bool first=true;
        while(part<3){
            std::unique_lock<std::mutex> lock(mutex_);
            if(count_==slots){++status_.waits;space_.wait(lock,[&]{return count_<slots;});}
            Chunk &chunk=queue_[tail_];chunk.size=0;chunk.first=first;
            while(part<3&&chunk.size<chunk_bytes){
                const std::size_t available=lengths[part]-offset;
                const std::size_t room=chunk_bytes-chunk.size;
                const std::size_t take=available<room?available:room;
                if(take)std::memcpy(chunk.bytes.data()+chunk.size,parts[part]+offset,take);
                chunk.size+=take;offset+=take;
                if(offset==lengths[part]){++part;offset=0;}
            }
            chunk.last=part==3;tail_=(tail_+1)%slots;++count_;status_.queued=count_;
            const auto live=count_+status_.in_flight;
            if(live>status_.high_water_chunks)status_.high_water_chunks=live;
            if(chunk.last)++status_.accepted;
            first=false;lock.unlock();ready_.notify_one();
        }
        return true;
    }
    Status status()const{std::lock_guard<std::mutex> lock(mutex_);return status_;}
    Status close(){
        std::lock_guard<std::mutex> producer(producer_);
        {std::lock_guard<std::mutex> lock(mutex_);status_.closed=true;}
        ready_.notify_one();
        if(worker_.joinable())worker_.join();
        return status();
    }
private:
    struct Chunk {std::array<char,chunk_bytes> bytes{};std::size_t size=0;bool first=false,last=false;};
    std::array<Chunk,slots> queue_{};
    std::size_t head_=0,tail_=0,count_=0;
    mutable std::mutex mutex_;
    std::mutex producer_;
    std::condition_variable ready_,space_;
    Sink sink_{};
    Status status_{};
    std::thread worker_;
    static void entry(void*context){static_cast<AsyncLog*>(context)->run();}
    void run(){
        Chunk chunk;bool line_ok=true;
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex_);ready_.wait(lock,[&]{return count_||status_.closed;});
             if(!count_&&status_.closed)break;
             chunk=queue_[head_];head_=(head_+1)%slots;--count_;status_.queued=count_;status_.in_flight=1;}
            space_.notify_one();
            if(chunk.first)line_ok=true;
            bool success=false;
            try{success=sink_.write(sink_.context,chunk.bytes.data(),chunk.size,chunk.first,chunk.last);}catch(...){success=false;}
            line_ok=success&&line_ok;
            {std::lock_guard<std::mutex> lock(mutex_);status_.in_flight=0;
             if(chunk.last){if(line_ok)++status_.written;else ++status_.failed;}}
        }
    }
};
}
