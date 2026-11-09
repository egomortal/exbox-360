// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string>

namespace dash {
enum class NetworkResult { Idle, Running, Passed, Failed, Unavailable };
enum class NetworkMedium { Unknown, Wired, Wireless };
struct NetworkInfo {
    std::string interface_name, address, mask, gateway, dns, dns_secondary, mac, hostname;
    NetworkMedium medium=NetworkMedium::Unknown;
    bool interfaces_readable=false;
    bool up=false;
    int signal=-1; // unknown; never infer signal strength from connectivity
};
struct NetworkTest {
    std::array<NetworkResult,4> steps{{NetworkResult::Idle,NetworkResult::Idle,
                                    NetworkResult::Idle,NetworkResult::Unavailable}};
    int next=0;
    bool running=false;
    void start() { *this=NetworkTest{};running=true; }
    // NetworkMain / TestingScene, 9199: console -> network -> Internet -> LIVE.
    // All three probes run, even if an earlier check fails (a router may filter TCP).
    template<class Probe,class Publish> void advance(Probe &probe,Publish publish) {
        if (!running || next>=3) return;
        steps[size_t(next)]=NetworkResult::Running;publish(*this);
        const NetworkResult result=probe(next);
        steps[size_t(next)]=result==NetworkResult::Passed || result==NetworkResult::Failed
            ? result : NetworkResult::Unavailable;
        ++next;running=next<3;publish(*this);
    }
};
}
