#pragma once
#include "policy.h"
#include <cstring>
namespace closet_tryon {
// Exact guest_asset.h clothing GUID/category classifier, extracted from P2.
inline bool wearable_only(const Metadata& old,const Metadata& next) noexcept {
 constexpr std::size_t begin=0x160,end=0x300,stride=32;
 constexpr unsigned mask=0x0ff8;
 if(std::memcmp(old.data(),next.data(),begin) ||
    std::memcmp(old.data()+end,next.data()+end,old.size()-end))return false;
 bool changed=false;
 for(std::size_t off=begin;off<end;off+=stride) {
  if(!std::memcmp(old.data()+off,next.data()+off,stride))continue;
  if(std::memcmp(old.data()+off+18,next.data()+off+18,14))return false;
  if(!std::memcmp(old.data()+off,next.data()+off,16))return false;
  for(const auto* data:{old.data()+off,next.data()+off}) {
   const unsigned category=(unsigned(data[16])<<8)|data[17];
   bool nonzero=false;for(unsigned i=0;i<16;++i)nonzero|=data[i]!=0;
   if((category&~mask)||(nonzero&&!category))return false;
  }
  changed=true;
 }
 return changed;
}
}
