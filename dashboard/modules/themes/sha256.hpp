// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace exbox::themes {

class Sha256 {
    std::array<std::uint32_t,8> state_{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::array<std::uint8_t,64> block_{};
    std::uint64_t bytes_{};
    std::size_t used_{};
    static std::uint32_t rotate(std::uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
    void transform(const std::uint8_t* data){
        static constexpr std::uint32_t k[64]={
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::uint32_t w[64];for(int i=0;i<16;++i)w[i]=(std::uint32_t(data[i*4])<<24)|(std::uint32_t(data[i*4+1])<<16)|(std::uint32_t(data[i*4+2])<<8)|data[i*4+3];
        for(int i=16;i<64;++i){const auto s0=rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3);const auto s1=rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(int i=0;i<64;++i){const auto s1=rotate(e,6)^rotate(e,11)^rotate(e,25);const auto ch=(e&f)^((~e)&g);const auto t1=h+s1+ch+k[i]+w[i];const auto s0=rotate(a,2)^rotate(a,13)^rotate(a,22);const auto maj=(a&b)^(a&c)^(b&c);const auto t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void update(const void* input,std::size_t size){const auto* data=static_cast<const std::uint8_t*>(input);bytes_+=size;while(size){const auto n=std::min(size,block_.size()-used_);for(std::size_t i=0;i<n;++i)block_[used_+i]=data[i];used_+=n;data+=n;size-=n;if(used_==block_.size()){transform(block_.data());used_=0;}}}
    std::string finish(){const std::uint64_t bits=bytes_*8;block_[used_++]=0x80;if(used_>56){while(used_<64)block_[used_++]=0;transform(block_.data());used_=0;}while(used_<56)block_[used_++]=0;for(int i=7;i>=0;--i)block_[used_++]=std::uint8_t(bits>>(i*8));transform(block_.data());static constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);for(auto word:state_)for(int shift=28;shift>=0;shift-=4)out.push_back(hex[(word>>shift)&15]);return out;}
};

inline std::string sha256(std::string_view bytes){Sha256 hash;hash.update(bytes.data(),bytes.size());return hash.finish();}

} // namespace exbox::themes
