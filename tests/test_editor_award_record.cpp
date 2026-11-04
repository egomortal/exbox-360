// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/award_record.hpp"
#include <cassert>
using namespace exbox::editor;
int main(){
 std::array<uint8_t,16> id={1,2,3,4,5,6,1,8,9,10,11,12,0x54,0x54,7,0xe7};
 for(unsigned nibble=0;nibble<256;++nibble){id[6]=nibble;auto out=award_record(id,0);assert(bool(out)==((nibble&15)==1));if(out){assert((*out)[0]==0x54&&(*out)[1]==0x54&&(*out)[2]==7&&(*out)[3]==0xe7);for(unsigned i=0;i<16;++i)assert((*out)[i+4]==id[i]);for(unsigned i=20;i<52;++i)assert((*out)[i]==(i==45?2:0));}}
 id[6]=1;assert(award_record(id,0x545407e7));assert(!award_record(id,0x545407e8));
}
