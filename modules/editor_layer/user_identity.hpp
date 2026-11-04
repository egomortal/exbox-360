// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace exbox::editor {
enum class IdentityStatus {success,invalid_argument,no_such_user};
inline constexpr uint64_t compatibility_xuid=0x0009BABEBABEBABEULL;
inline std::string display_name(std::string_view configured) {
    std::string result;
    for(const unsigned char value:configured) {
        if(value>=32 && value<=126)result.push_back(static_cast<char>(value));
        if(result.size()==15)break;
    }
    return result.empty()?"User":result;
}
inline uint32_t signin_state(uint32_t user){return user==0?2:0;}
struct IdentityReply {IdentityStatus status;bool write_output;uint64_t xuid;};
inline IdentityReply identity(uint32_t user,uint32_t mask,uint32_t profile_type,
                              uint64_t local_xuid,bool output_present) {
    if(!output_present)return {IdentityStatus::invalid_argument,false,0};
    if(user>=4)return {IdentityStatus::invalid_argument,true,0};
    if(user!=0)return {IdentityStatus::no_such_user,true,0};
    const auto selected=mask&profile_type;
    if(selected&6U)return {IdentityStatus::success,true,compatibility_xuid};
    if(selected&1U)return {IdentityStatus::success,true,local_xuid};
    return {IdentityStatus::no_such_user,true,0};
}
// Caller maps the symbolic status to the pinned SDK HRESULT constants.
// Input masks outside the documented set must be rejected by the binding.
inline IdentityStatus signin_info(uint32_t user,std::string_view configured,
                                 std::span<uint8_t> output) {
    if(output.size()<40)return IdentityStatus::invalid_argument;
    std::fill_n(output.begin(),40,uint8_t{0});
    if(user)return IdentityStatus::no_such_user;
    for(unsigned i=0;i<8;++i)output[i]=static_cast<uint8_t>(compatibility_xuid>>(56-8*i));
    output[11]=1;output[15]=2;
    const auto name=display_name(configured);
    std::copy(name.begin(),name.end(),output.begin()+24);
    return IdentityStatus::success;
}
}
