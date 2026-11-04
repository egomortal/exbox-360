// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/user_identity.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main(){
 assert(display_name("")=="User");assert(display_name("\x01\x7f\xc3\xa9")=="User");
 assert(display_name("A\nB\tC")=="ABC");assert(display_name("12345678901234567890")=="123456789012345");
 struct Case{uint32_t user,mask,type;IdentityStatus status;uint64_t value;};
 constexpr Case cases[]={
 {0,1,1,IdentityStatus::success,42},{0,2,2,IdentityStatus::success,compatibility_xuid},
 {0,4,4,IdentityStatus::success,compatibility_xuid},{0,7,7,IdentityStatus::success,compatibility_xuid},
 {0,1,7,IdentityStatus::success,42},{0,2,1,IdentityStatus::no_such_user,0},
 {0,3,0,IdentityStatus::no_such_user,0},{1,7,7,IdentityStatus::no_such_user,0},
 {3,1,1,IdentityStatus::no_such_user,0},{4,7,7,IdentityStatus::invalid_argument,0},
 {UINT32_MAX,1,1,IdentityStatus::invalid_argument,0}};
 for(auto c:cases){auto got=identity(c.user,c.mask,c.type,42,true);assert(got.status==c.status && got.write_output && got.xuid==c.value);
  got=identity(c.user,c.mask,c.type,42,false);assert(got.status==IdentityStatus::invalid_argument && !got.write_output);}
 for(uint32_t user:{0U,1U,3U,4U,UINT32_MAX})assert(signin_state(user)==(user==0?2U:0U));
 std::array<uint8_t,48> bytes;bytes.fill(0xcc);
 assert(signin_info(0,"Test",std::span(bytes).first(39))==IdentityStatus::invalid_argument);assert(bytes[0]==0xcc);
 assert(signin_info(0,"Test",bytes)==IdentityStatus::success);
 const std::array<uint8_t,8> expected{0,9,0xba,0xbe,0xba,0xbe,0xba,0xbe};
 assert(std::equal(expected.begin(),expected.end(),bytes.begin()));
 assert(bytes[11]==1 && bytes[15]==2 && bytes[16]==0 && bytes[23]==0);
 assert(std::string(reinterpret_cast<char*>(bytes.data()+24))=="Test");assert(bytes[40]==0xcc);
 assert(signin_info(9,"Test",bytes)==IdentityStatus::no_such_user);
 for(unsigned i=0;i<40;++i)assert(bytes[i]==0);assert(bytes[40]==0xcc);
}
