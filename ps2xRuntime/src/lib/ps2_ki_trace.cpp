// Focused, read-only projectile callback observations. No PINE client.
#include "ps2_runtime_macros.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
namespace {
uint32_t word(const uint8_t *ram,uint32_t at) {
    uint32_t v=0;if(uint64_t(at)+4<=PS2_RAM_SIZE)std::memcpy(&v,ram+at,4);return v;
}
bool valid(uint32_t at,uint32_t bytes) {return at>=0x100000&&uint64_t(at)+bytes<=PS2_RAM_SIZE;}
int physical(const uint8_t *ram,uint32_t model,uint32_t count) {
    for(uint32_t i=0;i<count;++i) {
        const uint32_t a=word(ram,0xD8040+4*i);
        if(valid(a,0x1280)&&word(ram,a+12)==model)return int(i);
    }
    return -1;
}
struct Seen {uint32_t effect=0,target=0,frame=0;bool gate=false;};
thread_local std::array<Seen,64> seen{};
thread_local uint32_t manager=0,lastFrame=0;
}
void ps2xTagteamTraceKiBranch(uint8_t *ram,R5900Context *ctx,uint32_t target,uint32_t site) {
    static const bool enabled=[] {const char *v=std::getenv("PS2X_KI_TRACE");return v&&*v&&*v!='0';}();
    if(!enabled)return;
    const bool initialize=target==0x176788u;
    const bool aim=target==0x2058e0u&&site==0x1310bcu;
    const bool noAim=target==0x121fa8u&&site==0x1310a4u;
    if(!initialize&&!aim&&!noAim)return;
    const uint32_t count=word(ram,0xD8084),m=word(ram,0xD8088),frame=word(ram,0x073D680C);
    if(word(ram,0xD8080)!=1||count<4||count>10||word(ram,0xD808C)!=count||
       !valid(m,8)||word(ram,0x2FEB14)!=m||word(ram,m)!=2)return;
    if(manager!=m||frame<lastFrame)seen={};manager=m;lastFrame=frame;
    uint32_t effect=0,payload=0,owner=0,used=0;
    if(initialize) {
        effect=GPR_U32(ctx,4);const uint32_t descriptor=GPR_U32(ctx,5);
        if(!valid(effect,0x3c)||!valid(descriptor,0x50))return;
        payload=word(ram,effect+0x38);owner=word(ram,descriptor+0x10)>>16;
    } else {
        const uint32_t sp=GPR_U32(ctx,29);
        if(!valid(sp,0xd0)||word(ram,sp+0xA0)!=0x1769ecu)return;
        // 131030 saves its caller's s1(payload) and s3(effect) before reuse.
        payload=word(ram,sp+0x78);effect=word(ram,sp+0x88);
        owner=GPR_U32(ctx,16);used=aim?GPR_U32(ctx,4):GPR_U32(ctx,19);
    }
    if(!valid(effect,0x3c)||!valid(payload,0x5e0))return;
    const int source=physical(ram,owner,count);
    if(source<0)return;
    const uint32_t actor=word(ram,0xD8040+4*source);
    if(word(ram,actor+0x1278)!=0)return;
    const uint32_t selected=word(ram,0xD8000+4*source);
    if(initialize) {
        // Actual callback initialization, not inferred from a contact event.
        for(auto &entry:seen)if(entry.effect==effect)entry={};
    } else {
        auto &entry=seen[(effect>>4)%seen.size()];
        if(entry.effect==effect&&entry.target==used&&entry.gate==aim&&frame-entry.frame<30)return;
        entry={effect,used,frame,aim};
    }
    const uint32_t direction=payload+0x30;
    std::fprintf(stderr,"[kitrace] event=%s frame=%u manager=0x%x source=%d owner_model=%u selected_physical=%u used_model=%u used_physical=%d gate=%s effect=0x%x payload=0x%x callback=0x%x flags=0x%x action=%u site=0x%x ra=0x%x direction_bits=%08x/%08x/%08x\n",
        initialize?"basic-initialize":"basic-steering",frame,m,source,owner,selected,used,
        initialize?-1:physical(ram,used,count),initialize?"not-evaluated":aim?"enabled":"disabled",
        effect,payload,word(ram,word(ram,effect+0x28)),word(ram,payload+0x5b0),
        word(ram,actor+0x948),site,GPR_U32(ctx,31),word(ram,direction),word(ram,direction+4),word(ram,direction+8));
    std::fflush(stderr);
}
