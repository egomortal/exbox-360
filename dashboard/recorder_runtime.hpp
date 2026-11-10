// SPDX-License-Identifier: GPL-3.0-or-later
// Optional Glint companion client. Glint remains a separately supplied payload;
// Ex-Box only talks to its documented localhost HTTP API.
#pragma once
#include "dash_model.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace exbox_recorder {
struct Status {
    bool valid=false, recording=false, paused=false, pause_supported=false, error=false;
    double seconds=0.0;
    std::string name;
};

inline bool json_bool(const std::string &json,const char *key,bool &value) {
    const std::string token=std::string("\"")+key+"\"";
    std::size_t p=json.find(token);if(p==std::string::npos)return false;
    p=json.find(':',p+token.size());if(p==std::string::npos)return false;
    p=json.find_first_not_of(" \t\r\n",p+1);if(p==std::string::npos)return false;
    if(json.compare(p,4,"true")==0){value=true;return true;}
    if(json.compare(p,5,"false")==0){value=false;return true;}
    return false;
}
inline bool json_number(const std::string &json,const char *key,double &value) {
    const std::string token=std::string("\"")+key+"\"";
    std::size_t p=json.find(token);if(p==std::string::npos)return false;
    p=json.find(':',p+token.size());if(p==std::string::npos)return false;
    p=json.find_first_of("-0123456789",p+1);if(p==std::string::npos)return false;
    char *end=nullptr;value=std::strtod(json.c_str()+p,&end);
    return end!=json.c_str()+p && std::isfinite(value);
}
inline std::string json_string(const std::string &json,const char *key) {
    const std::string token=std::string("\"")+key+"\"";
    std::size_t p=json.find(token);if(p==std::string::npos)return {};
    p=json.find(':',p+token.size());if(p==std::string::npos)return {};
    p=json.find('"',p+1);if(p==std::string::npos)return {};
    std::string out;
    for(++p;p<json.size();++p){const char ch=json[p];if(ch=='"')break;if(ch=='\\'&&p+1<json.size())out+=json[++p];else out+=ch;}
    return out;
}
inline Status parse_status(const std::string &json) {
    Status out;bool recording=false,paused=false,error=false;double seconds=0.0;
    if(json_string(json,"backend")!="h264" || !json_bool(json,"recording",recording) ||
       !json_bool(json,"recording_error",error) || !json_number(json,"recording_seconds",seconds))return out;
    out.valid=true;out.recording=recording;out.pause_supported=json_bool(json,"recording_paused",paused);out.paused=out.pause_supported&&paused;
    out.error=error;out.seconds=std::max(0.0,seconds);out.name=json_string(json,"recording_name");return out;
}

inline int connect_local(int timeout_ms) {
    const int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
#ifdef SO_NOSIGPIPE
    int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    const int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0){close(fd);return -1;}
    sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(7231);inet_pton(AF_INET,"127.0.0.1",&target.sin_addr);
    if(connect(fd,reinterpret_cast<const sockaddr*>(&target),sizeof target)!=0 && errno!=EINPROGRESS){close(fd);return -1;}
    pollfd wait{fd,POLLOUT,0};if(poll(&wait,1,timeout_ms)<=0){close(fd);return -1;}
    int error=0;socklen_t size=sizeof error;if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)!=0||error){close(fd);return -1;}
    return fd;
}
inline bool transfer(const char *method,const char *path,std::string &body,int timeout_ms=400) {
    const int fd=connect_local(timeout_ms);if(fd<0)return false;
    const std::string request=std::string(method)+" "+path+" HTTP/1.1\r\nHost: 127.0.0.1:7231\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    std::size_t sent=0;bool ok=true;
    while(sent<request.size()){
        pollfd wait{fd,POLLOUT,0};if(poll(&wait,1,timeout_ms)<=0){ok=false;break;}
#ifdef MSG_NOSIGNAL
        const int flags=MSG_NOSIGNAL;
#else
        const int flags=0;
#endif
        const ssize_t n=send(fd,request.data()+sent,request.size()-sent,flags);
        if(n>0)sent+=std::size_t(n);else if(errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK){ok=false;break;}
    }
    std::string response;char chunk[2048];
    while(ok&&response.size()<32768){pollfd wait{fd,POLLIN,0};const int ready=poll(&wait,1,timeout_ms);if(ready==0){ok=false;break;}if(ready<0){if(errno==EINTR)continue;ok=false;break;}
        const ssize_t n=recv(fd,chunk,sizeof chunk,0);if(n>0)response.append(chunk,std::size_t(n));else if(n==0)break;else if(errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK){ok=false;break;}}
    close(fd);if(!ok||response.compare(0,12,"HTTP/1.1 200")!=0)return false;
    const std::size_t split=response.find("\r\n\r\n");if(split==std::string::npos)return false;body=response.substr(split+4);return true;
}
inline Status query() {std::string body;if(!transfer("GET","/status.json",body))return {};return parse_status(body);}

struct Notice {std::string title,text;bool error=false;};
class Worker {
    enum class Action {Probe,Start,Pause,Resume,Stop};
    struct Job {std::mutex mutex;Action action=Action::Probe;Status status;bool command_ok=false,done=false;};
    std::shared_ptr<Job> active_;
    unsigned next_probe_=0,failures_=0;
    bool starting_=false;
    unsigned starting_until_=0;
    Notice notice_;
    void begin(Action action) {
        if(active_)return;
        active_=std::make_shared<Job>();active_->action=action;const auto job=active_;
        try{std::thread([job]{
            if(job->action!=Action::Probe){std::string body;const char *path=job->action==Action::Start?"/record/start":job->action==Action::Pause?"/record/pause":job->action==Action::Resume?"/record/resume":"/record/stop";job->command_ok=transfer("POST",path,body);}
            job->status=query();std::lock_guard<std::mutex> lock(job->mutex);job->done=true;
        }).detach();}catch(const std::system_error&){active_.reset();}
    }
public:
    void command(dash::Model &model,int requested,unsigned now) {
        if(active_||model.recorder_busy)return;
        model.recorder_busy=true;
        begin(requested==0?Action::Start:requested==1?Action::Pause:requested==2?Action::Resume:Action::Stop);
        if(!active_)model.recorder_busy=false;
        next_probe_=now+250;
    }
    Notice tick(dash::Model &model,unsigned now) {
        if(active_){
            bool done=false;{std::lock_guard<std::mutex> lock(active_->mutex);done=active_->done;}
            if(done){
                const Action action=active_->action;const bool command_ok=active_->command_ok;const Status status=active_->status;active_.reset();
                if(status.valid){
                    failures_=0;model.recorder_available=true;model.recorder_recording=status.recording;model.recorder_paused=status.paused;model.recorder_pause_supported=status.pause_supported;model.recorder_error=status.error;model.recorder_seconds=status.seconds;model.recorder_name=status.name;
                    const bool selected_enabled=(!model.recorder_recording&&model.recorder_control==0)||
                        (model.recorder_recording&&model.recorder_paused&&(model.recorder_control==0||model.recorder_control==2))||
                        (model.recorder_recording&&!model.recorder_paused&&((model.recorder_pause_supported&&model.recorder_control==1)||model.recorder_control==2));
                    if(!selected_enabled)model.recorder_control=!model.recorder_recording||model.recorder_paused?0:model.recorder_pause_supported?1:2;
                    const auto rows=model.host_blade_entries(dash::BladeMedia);
                    model.guide_item[dash::BladeMedia]=std::clamp(model.guide_item[dash::BladeMedia],0,std::max(0,int(rows.size())-1));
                }
                else if(++failures_>=2&&!model.recorder_recording)model.recorder_available=false;
                if(action==Action::Start){
                    if(command_ok){starting_=status.valid&&!status.recording;starting_until_=now+5000;model.recorder_busy=starting_;notice_={"Screen recording",status.recording?"Recording started.":"Starting on the next video keyframe.",false};}
                    else{model.recorder_busy=false;notice_={"Screen recording","Glint did not accept the start request. The Create-button recorder is still available.",true};}
                }else if(action==Action::Pause){
                    model.recorder_busy=false;notice_=command_ok?Notice{"Screen recording","Recording paused.",false}:Notice{"Screen recording","The companion did not accept Pause.",true};
                }else if(action==Action::Resume){
                    model.recorder_busy=false;notice_=command_ok?Notice{"Screen recording","Recording resumed.",false}:Notice{"Screen recording","The companion did not accept Resume.",true};
                }else if(action==Action::Stop){
                    model.recorder_busy=false;starting_=false;
                    notice_=command_ok?Notice{"Recording saved",status.name.empty()?"Saved in /data/glint/recordings.":status.name,false}:Notice{"Screen recording","Glint did not accept the stop request.",true};
                }
                next_probe_=now+(model.recorder_recording||starting_?500:5000);
            }
        }
        if(starting_&&model.recorder_recording){starting_=false;model.recorder_busy=false;notice_={"Screen recording","Recording started.",false};}
        if(starting_&&static_cast<int>(now-starting_until_)>=0){starting_=false;model.recorder_busy=false;notice_={"Screen recording","The recorder did not begin within five seconds. Check Glint before trying again.",true};}
        if(!active_&&static_cast<int>(now-next_probe_)>=0){begin(Action::Probe);next_probe_=now+(model.recorder_recording||starting_?500:5000);}
        Notice out=notice_;notice_={};return out;
    }
};
}
