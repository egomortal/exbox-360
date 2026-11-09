#include <rex/ppc/context.h>
#include "frame_tags.h"
extern "C" void sub_920EBD10(PPCContext&,uint8_t*);
extern "C" void av2_legend_capture(PPCContext& ctx,uint8_t* base,uint64_t frame){
 if(!frame||frame>av2::legend::FrameTags::max_frame)return;
 PPCContext query=ctx;query.r3.u64=0x922E7604u;sub_920EBD10(query,base);
 av2_legend_publish(frame,query.r3.u32==2);
}
