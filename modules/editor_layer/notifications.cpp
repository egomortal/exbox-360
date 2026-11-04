// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifications.hpp"
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
namespace rex::kernel::xam {
uint32_t xeXamNotifyCreateListener(uint64_t,uint32_t,uint32_t);
}
namespace {
struct RuntimeNotifications {
    uint32_t create(uint64_t mask,uint32_t system,uint32_t version) {
        return rex::kernel::xam::xeXamNotifyCreateListener(mask,system,version);
    }
    void emit(uint32_t event,uint32_t payload) {
        rex::system::kernel_state()->BroadcastNotification(event,payload);
    }
};
void open(PPCContext& registers,bool internal) {
    const uint64_t areas=registers.r3.u64;
    const uint32_t system=internal?registers.r4.u32:0;
    const uint32_t version=internal?registers.r5.u32:registers.r4.u32;
    RuntimeNotifications services;
    registers.r3.u64=exbox::editor::open_notifications(services,areas,system,version);
}
}
extern "C" void __imp__XamNotifyCreateListener(PPCContext& ctx,uint8_t*) {open(ctx,false);}
extern "C" void __imp__XamNotifyCreateListenerInternal(PPCContext& ctx,uint8_t*) {open(ctx,true);}
