// Ex-Box offline-only UI policy (owner request, 2026-11-02).
// Original 12581 root menu: vtable 9201037C; constructor 920E98A8
// stores Marketplace at slot 2 (+20). Remap the original item accessor and
// count together, so focus/navigation use the same compact list as rendering.
// Generated guest code and original scene/texture bytes remain unchanged.
namespace {
bool AeOfflineRoot(PPCContext& ctx,uint8_t* base) {
 const auto* word=GuestDataPtrProbe(ctx.r3.u32,4);
 if(!word || ((uint32_t(word[0])<<24)|(uint32_t(word[1])<<16)|(uint32_t(word[2])<<8)|word[3])!=0x9201037cu)return false;
 (void)base;return true;
}
}
extern "C" void __imp__sub_920E9848(PPCContext&,uint8_t*);
extern "C" void sub_920E9848(PPCContext& ctx,uint8_t* base) {
 if(AeOfflineRoot(ctx,base)) {
  const auto index=ctx.r4.s32;
  if(index<0 || index>=7){ctx.r3.u64=0;return;}
  if(index>=2)ctx.r4.u32=uint32_t(index+1);
 }
 __imp__sub_920E9848(ctx,base);
}
extern "C" void __imp__sub_92204560(PPCContext&,uint8_t*);
extern "C" void sub_92204560(PPCContext& ctx,uint8_t* base) {
 if(AeOfflineRoot(ctx,base)){ctx.r3.u64=7;return;}
 __imp__sub_92204560(ctx,base);
}
// Defence for legacy store launch routes: prevent page 67 (loading) and its
// descendants 68..74 from being pushed. The original photo booth starts at 75.
extern "C" void __imp__sub_920ED0E0(PPCContext&,uint8_t*);
extern "C" void sub_920ED0E0(PPCContext& ctx,uint8_t* base) {
 if(ctx.r4.u32>=67 && ctx.r4.u32<=74){ctx.r3.u64=0;return;}
 __imp__sub_920ED0E0(ctx,base);
}

// The original Y action opens Marketplace from clothing grids.
extern "C" void sub_920CC700(PPCContext& ctx,uint8_t*) { ctx.r3.u64=0; }

#include "../../../private/av2/offline-policy/offline_store_captions.hpp"

// 12581 ControlPackLegendScene sends its four caption buffers through
// 920CD1B0 (921DC230, +1024/+1344/+1664). Its own empty caption at
// 92001F50 suppresses the entire legend slot. Preserve local Use Prop.
extern "C" void __imp__sub_920CD1B0(PPCContext&,uint8_t*);
extern "C" void sub_920CD1B0(PPCContext& ctx,uint8_t* base) {
 bool matches=false;
 for(const auto* caption:kOfflineStoreCaptions) {
  unsigned length=0;while(caption[length])++length;
  const auto* text=GuestDataPtrProbe(ctx.r4.u32,(length+1)*2);
  if(!text)continue;
  bool equal=true;
  for(unsigned j=0;j<=length;++j)
   if(((uint16_t(text[j*2])<<8)|text[j*2+1])!=caption[j]){equal=false;break;}
  if(equal){matches=true;break;}
 }
 if(matches)ctx.r4.u32=0x92001f50u;
 __imp__sub_920CD1B0(ctx,base);
}
