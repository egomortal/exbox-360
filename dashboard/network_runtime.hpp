// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only POSIX networking; no Sce privileged APIs or network configuration writes.
#pragma once
#include "dash_model.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/wireless.h>
#include <netpacket/packet.h>
#elif __has_include(<net/if_dl.h>)
#include <net/if_dl.h>
#include <net/if_types.h>
#if __has_include(<net/if_media.h>)
#include <net/if_media.h>
#endif
#include <net/route.h>
#include <sys/sysctl.h>
#define EXBOX_ROUTE_SYSCTL 1
#endif

namespace exbox_network {
inline std::string address(const sockaddr *sa) {
    if (!sa) return {};
    char out[INET6_ADDRSTRLEN]{};
    const void *src=nullptr;
    if (sa->sa_family==AF_INET) src=&reinterpret_cast<const sockaddr_in *>(sa)->sin_addr;
    if (sa->sa_family==AF_INET6) src=&reinterpret_cast<const sockaddr_in6 *>(sa)->sin6_addr;
    return src && inet_ntop(sa->sa_family,src,out,sizeof out)?out:"";
}
inline std::string gateway(const std::string &name) {
#if defined(__linux__)
    std::ifstream routes("/proc/net/route");std::string line;
    while (std::getline(routes,line)) {
        std::istringstream row(line);std::string iface;unsigned long dest=0,gw=0,flags=0;
        if (!(row>>iface>>std::hex>>dest>>gw>>flags) || iface!=name || dest || !(flags&1) || !(flags&2)) continue;
        sockaddr_in sa{};sa.sin_family=AF_INET;sa.sin_addr.s_addr=static_cast<in_addr_t>(gw);
        return address(reinterpret_cast<const sockaddr *>(&sa));
    }
#elif defined(EXBOX_ROUTE_SYSCTL) && defined(NET_RT_DUMP)
    int mib[]={CTL_NET,PF_ROUTE,0,AF_INET,NET_RT_DUMP,0};size_t size=0;
    if (sysctl(mib,6,nullptr,&size,nullptr,0)!=0 || size>1024*1024) return {};
    std::vector<char> bytes(size);
    if (sysctl(mib,6,bytes.data(),&size,nullptr,0)!=0) return {};
    const unsigned index=if_nametoindex(name.c_str());
    for (size_t at=0;at+sizeof(rt_msghdr)<=size;) {
        const auto *rt=reinterpret_cast<const rt_msghdr *>(bytes.data()+at);
        const size_t length=rt->rtm_msglen;
        if (length<sizeof(*rt) || length>size-at) break;
        if (rt->rtm_version==RTM_VERSION && rt->rtm_index==index && (rt->rtm_flags&RTF_UP) && (rt->rtm_flags&RTF_GATEWAY)) {
            const sockaddr *dest=nullptr,*gw=nullptr;size_t offset=sizeof(*rt);
            for (int i=0;i<RTAX_MAX && offset+2<=length;++i) {
                if (!(rt->rtm_addrs&(1<<i))) continue;
                const auto *sa=reinterpret_cast<const sockaddr *>(bytes.data()+at+offset);
                const size_t len=sa->sa_len;
                if (len>length-offset) break;
                if (len>=sizeof(sockaddr_in)) {
                    if (i==RTAX_DST) dest=sa;
                    if (i==RTAX_GATEWAY) gw=sa;
                }
                offset+=len?((len+sizeof(long)-1)&~(sizeof(long)-1)):sizeof(long);
            }
            if (dest && gw && dest->sa_family==AF_INET && gw->sa_family==AF_INET &&
                reinterpret_cast<const sockaddr_in *>(dest)->sin_addr.s_addr==INADDR_ANY) return address(gw);
        }
        at+=length;
    }
#else
    (void)name;
#endif
    return {};
}
inline dash::NetworkInfo read() {
    dash::NetworkInfo out;
    char host[256]{};
    if (gethostname(host,sizeof host-1)==0) out.hostname=host;
    // UDP connect selects the kernel's outgoing address without sending a packet.
    std::string preferred;
    const int route=socket(AF_INET,SOCK_DGRAM,0);
    if (route>=0) {
        sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(53);
        inet_pton(AF_INET,"1.1.1.1",&remote.sin_addr);
        if (connect(route,reinterpret_cast<const sockaddr *>(&remote),sizeof remote)==0) {
            sockaddr_in local{};socklen_t len=sizeof local;
            if (getsockname(route,reinterpret_cast<sockaddr *>(&local),&len)==0)
                preferred=address(reinterpret_cast<const sockaddr *>(&local));
        }
        close(route);
    }
    ifaddrs *list=nullptr;
    if (getifaddrs(&list)==0) {
        out.interfaces_readable=true;
        int best=-1;
        for (const auto *p=list;p;p=p->ifa_next) {
            if (!p->ifa_addr || !p->ifa_name || (p->ifa_flags&IFF_LOOPBACK) || !(p->ifa_flags&IFF_UP)) continue;
            out.up=true;
            const std::string ip=address(p->ifa_addr);
            if (ip.empty()) continue;
            const int score=ip==preferred?3:p->ifa_addr->sa_family==AF_INET?2:1;
            if (score<=best) continue;
            best=score;out.interface_name=p->ifa_name;out.address=ip;out.mask=address(p->ifa_netmask);
        }
        for (const auto *p=list;p;p=p->ifa_next) {
            if (!p->ifa_name || !p->ifa_addr || out.interface_name!=p->ifa_name) continue;
            const unsigned char *mac=nullptr;
#if defined(__linux__)
            if (p->ifa_addr->sa_family==AF_PACKET) {
                const auto *link=reinterpret_cast<const sockaddr_ll *>(p->ifa_addr);
                if (link->sll_halen==6) mac=link->sll_addr;
            }
#elif defined(EXBOX_ROUTE_SYSCTL)
            if (p->ifa_addr->sa_family==AF_LINK) {
                const auto *link=reinterpret_cast<const sockaddr_dl *>(p->ifa_addr);
                if (link->sdl_alen==6) mac=reinterpret_cast<const unsigned char *>(LLADDR(link));
                // Ethernet framing alone does not prove a wired medium: Wi-Fi
                // drivers may expose IFT_ETHER too. Use SIOCGIFMEDIA below.
#ifdef IFT_IEEE80211
                if (link->sdl_type==IFT_IEEE80211) out.medium=dash::NetworkMedium::Wireless;
#endif
            }
#endif
            if (mac) {
                char value[18]{};
                std::snprintf(value,sizeof value,"%02X-%02X-%02X-%02X-%02X-%02X",
                    unsigned(mac[0]),unsigned(mac[1]),unsigned(mac[2]),unsigned(mac[3]),unsigned(mac[4]),unsigned(mac[5]));
                out.mac=value;
            }
        }
        freeifaddrs(list);
    }
    if (!out.interface_name.empty()) {
        out.gateway=gateway(out.interface_name);
        const int fd=socket(AF_INET,SOCK_DGRAM,0);
        if (fd>=0) {
#if defined(__linux__)
            iwreq wireless{};std::strncpy(wireless.ifr_name,out.interface_name.c_str(),IFNAMSIZ-1);
            if (ioctl(fd,SIOCGIWNAME,&wireless)==0) out.medium=dash::NetworkMedium::Wireless;
            else {
                ifreq link{};std::strncpy(link.ifr_name,out.interface_name.c_str(),IFNAMSIZ-1);
                if (ioctl(fd,SIOCGIFHWADDR,&link)==0 && link.ifr_hwaddr.sa_family==1) out.medium=dash::NetworkMedium::Wired;
            }
            iw_statistics stats{};wireless.u.data.pointer=&stats;wireless.u.data.length=sizeof stats;wireless.u.data.flags=1;
            if (out.medium==dash::NetworkMedium::Wireless && ioctl(fd,SIOCGIWSTATS,&wireless)==0 &&
                (stats.qual.updated&IW_QUAL_DBM) && !(stats.qual.updated&IW_QUAL_LEVEL_INVALID)) {
                const int dbm=int(stats.qual.level)-256;
                out.signal=dbm>=-50?4:dbm>=-60?3:dbm>=-70?2:dbm>=-80?1:0;
            }
#elif defined(SIOCGIFMEDIA) && defined(IFM_IEEE80211) && defined(IFM_ETHER)
            ifmediareq media{};std::strncpy(media.ifm_name,out.interface_name.c_str(),IFNAMSIZ-1);
            if (ioctl(fd,SIOCGIFMEDIA,&media)==0) {
                if (IFM_TYPE(media.ifm_current)==IFM_IEEE80211) out.medium=dash::NetworkMedium::Wireless;
                else if (IFM_TYPE(media.ifm_current)==IFM_ETHER) out.medium=dash::NetworkMedium::Wired;
            }
#endif
            close(fd);
        }
    }
    std::ifstream config("/etc/resolv.conf");std::string line;
    while (std::getline(config,line)) {
        std::istringstream row(line);std::string key,value;
        if (!(row>>key>>value) || key!="nameserver") continue;
        unsigned char parsed[16]{};
        if (inet_pton(AF_INET,value.c_str(),parsed)!=1 && inet_pton(AF_INET6,value.c_str(),parsed)!=1) continue;
        if (out.dns.empty()) out.dns=value;
        else if (out.dns_secondary.empty()) out.dns_secondary=value;
    }
    return out;
}
inline bool tcp(const sockaddr *target,socklen_t length,int timeout=650) {
    const int fd=socket(target->sa_family,SOCK_STREAM,0);
    if (fd<0) return false;
    const int flags=fcntl(fd,F_GETFL,0);
    bool ok=false;
    if (flags>=0 && fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0) {
        const int result=connect(fd,target,length);
        if (result==0) ok=true;
        else if (errno==EINPROGRESS) {
            pollfd wait{fd,POLLOUT,0};
            if (poll(&wait,1,timeout)>0) {
                int error=0;socklen_t size=sizeof error;
                ok=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0 && error==0;
            }
        }
    }
    close(fd);return ok;
}
struct Probe {
    dash::NetworkInfo info;
    dash::NetworkResult operator()(int step) const {
        using R=dash::NetworkResult;
        if (step==0) return !info.interfaces_readable?R::Unavailable:info.up?R::Passed:R::Failed;
        if (step==1) {
            std::string host=info.gateway;
            // A configured DNS server on the measured local subnet is another
            // real LAN target when the route table is not readable.
            if (host.empty()) {
                in_addr local{},mask{},dns{};
                if (inet_pton(AF_INET,info.address.c_str(),&local)==1 &&
                    inet_pton(AF_INET,info.mask.c_str(),&mask)==1 && mask.s_addr!=0) {
                    for (const auto &candidate:{info.dns,info.dns_secondary})
                        if (inet_pton(AF_INET,candidate.c_str(),&dns)==1 && dns.s_addr!=local.s_addr &&
                            (dns.s_addr&mask.s_addr)==(local.s_addr&mask.s_addr)) {host=candidate;break;}
                }
            }
            if (host.empty()) return R::Unavailable;
            sockaddr_in target{};target.sin_family=AF_INET;
            if (inet_pton(AF_INET,host.c_str(),&target.sin_addr)!=1) return R::Unavailable;
            for (const int port:{53,80}) {
                target.sin_port=htons(static_cast<unsigned short>(port));
                if (tcp(reinterpret_cast<const sockaddr *>(&target),sizeof target)) return R::Passed;
            }
            return R::Failed;
        }
        // Ex-Box addition: DNS + HTTPS TCP reachability, not an Xbox LIVE login.
        addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
        addrinfo *resolved=nullptr;
        if (getaddrinfo("example.com","443",&hints,&resolved)!=0) return R::Failed;
        bool ok=false;int attempts=0;
        for (const auto *p=resolved;p && attempts<4 && !ok;p=p->ai_next,++attempts)
            ok=tcp(p->ai_addr,static_cast<socklen_t>(p->ai_addrlen));
        freeaddrinfo(resolved);return ok?R::Passed:R::Failed;
    }
};
// One worker maximum. Restarts queue only the latest request; stale results are
// discarded. The worker owns its data, never Model/SDL, and cannot block shutdown.
class Worker {
    struct Job {
        std::mutex mutex;
        dash::NetworkInfo info;
        dash::NetworkTest test;
        unsigned generation=0;
        bool done=false, testing=false;
    };
    std::shared_ptr<Job> active;
    unsigned requested=0;
    bool pending=false,testing=false;
public:
    void request(const dash::Model &model,bool test) {
        requested=model.network_generation;testing=test;pending=true;
    }
    void tick(dash::Model &model) {
        if (active) {
            bool done=false;
            {
                std::lock_guard<std::mutex> lock(active->mutex);
                if (active->generation==model.network_generation) {
                    model.network=active->info;
                    if (active->testing) model.network_test=active->test;
                }
                done=active->done;
            }
            if (done) active.reset();
        }
        if (active || !pending) return;
        pending=false;
        if (requested!=model.network_generation) return;
        active=std::make_shared<Job>();active->generation=requested;active->testing=testing;
        if (testing) active->test.start();
        const auto job=active;
        try {
            std::thread([job]() {
                Probe probe{read()};
                {std::lock_guard<std::mutex> lock(job->mutex);job->info=probe.info;}
                if (job->testing) {
                    dash::NetworkTest test;test.start();
                    auto publish=[&](const dash::NetworkTest &state) {
                        std::lock_guard<std::mutex> lock(job->mutex);job->test=state;
                    };
                    while (test.running) test.advance(probe,publish);
                }
                std::lock_guard<std::mutex> lock(job->mutex);job->done=true;
            }).detach();
        } catch (const std::system_error &) {
            active->test.running=false;
            active->test.steps.fill(dash::NetworkResult::Unavailable);active->done=true;
        }
    }
};
}
