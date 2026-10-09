// First embedded feature: frozen leaf getter/resolver bodies, selected only
// after an exact code comparison. Live controls remain in guest RAM.
#include "ps2_runtime_macros.h"
#include "runtime/ps2_tagteam_target_pack.h"
#include <array>
#include <cstdio>
#include <cstdlib>
extern void ps2xTagteamTraceTargetSample(uint8_t *ram);
extern void ps2xTagteamTraceNativePhysicalRead(uint8_t *ram,uint32_t site,uint32_t argument,uint32_t result);

namespace {
struct TargetOps {
    uint8_t *rdram;
    R5900Context *ctx;
    PS2Runtime *runtime;
    uint32_t u32(unsigned r) const { return GPR_U32(ctx,r); }
    uint64_t u64(unsigned r) const { return GPR_U64(ctx,r); }
    void s32(unsigned r,uint32_t value) { SET_GPR_S32(ctx,r,value); }
    void u64(unsigned r,uint64_t value) { SET_GPR_U64(ctx,r,value); }
    uint32_t read32(uint32_t address) { return READ32(address); }
    uint8_t read8(uint32_t address) { return READ8(address); }
    uint64_t read64(uint32_t address) { return READ64(address); }
    void write64(uint32_t address,uint64_t value) { WRITE64(address,value); }
    void write32(uint32_t address,uint32_t value) { WRITE32(address,value); }
    void load128(unsigned r,uint32_t address) {
        const auto lo=READ64(address),hi=READ64(address+8);
        if(r)ctx->r[r]=_mm_set_epi64x(int64_t(hi),int64_t(lo));
    }
    void store128(unsigned r,uint32_t address) {
        alignas(16) uint64_t v[2];_mm_store_si128(reinterpret_cast<__m128i*>(v),ctx->r[r]);
        WRITE64(address,v[0]);WRITE64(address+8,v[1]);
    }
};
struct TargetStats {
    std::array<uint64_t,64> native{},oracle{},fallback{};
    ~TargetStats() {
        for(unsigned i=0;i<native.size();++i)
            if(native[i]||oracle[i]||fallback[i])
                std::fprintf(stderr,"[native-targets] entry=%u native=%llu oracle=%llu signature_fallback=%llu\n",
                    i,(unsigned long long)native[i],(unsigned long long)oracle[i],(unsigned long long)fallback[i]);
    }
};
thread_local bool comparing=false;
int index(uint32_t pc) {
    switch(pc) {
    case 0x07368000:return 0;
    case 0x07368200:return 1;
    case 0x07368600:return 2;
    case 0x07368800:return 3;
    case 0x07368808:return 3;
    case 0x077C4000:return 4;
    case 0x070B0400:return 5;
    case 0x070B1000:return 6;
    case 0x07260000:return 7;
    case 0x071A0000:return 8;
    case 0x07243000:return 9;
    case 0x07180000:return 10;
    case 0x070F0000:return 11;
    case 0x070F0400:return 12;
    case 0x07781000:return 13;
    case 0x07788000:return 14;
    case 0x06944000:return 15;
    case 0x06944400:return 16;
    default:{const int elf=ps2x::tagteam::frozen::elfIndex(pc);return elf<0?-1:17+elf;}
    }
}
}

bool ps2xTagteamRunNativeTargets(uint8_t *rdram,R5900Context *ctx,PS2Runtime *runtime) {
    static const int mode=[] {
        const char *value=std::getenv("PS2X_NATIVE_TARGETS");
        if(value && *value=='0')return 0;
        const bool oracle=value && std::strcmp(value,"oracle")==0;
        std::fprintf(stderr,"[native-targets] enabled mode=%s exact_code_match=required\n",oracle?"oracle":"native");
        return oracle?2:1;
    }();
    if(!mode || comparing)return false;
    const int slot=index(ctx->pc);
    if(slot<0)return false;
    static const bool extraLeaves=[] {const char* v=std::getenv("PS2X_NATIVE_LEAVES");return !(v && *v=='0');}();
    if(!extraLeaves && (slot>=4 || ctx->pc==0x07368808))return false;
    ps2xTagteamTraceTargetSample(rdram);
    thread_local TargetStats stats;
    const uint32_t argument=GPR_U32(ctx,4),returnPc=GPR_U32(ctx,31);
    auto traceResult=[&] {
        if(slot==1&&ctx->pc==returnPc)
            ps2xTagteamTraceNativePhysicalRead(rdram,returnPc-8,argument,GPR_U32(ctx,2));
    };
    // Guards preserve five full128-bit registers at SP-80..SP. The HUD
    // resolver also publishes fields in its own48-byte control record.
    const uint32_t sp=GPR_U32(ctx,29)&0x1fffffffu;
    if(sp<80 || sp>=PS2_RAM_SIZE)return false;
    if(mode==2 && stats.oracle[slot]<64) {
        R5900Context expected=*ctx;
        std::array<uint8_t,80> before{},after{};
        std::array<uint8_t,48> hudBefore{},hudAfter{};
        std::array<uint8_t,320> throwsBefore{},throwsAfter{};
        std::memcpy(before.data(),rdram+sp-80,before.size());
        if(slot==7)std::memcpy(hudBefore.data(),rdram+0x0726F000,hudBefore.size());
        if(slot==13)std::memcpy(throwsBefore.data(),rdram+0x0778F000,throwsBefore.size());
        TargetOps ops{rdram,&expected,runtime};
        uint32_t exit=0;
        if(!ps2x::tagteam::frozen::execute(rdram,ctx->pc,ops,exit)) {
            ++stats.fallback[slot];return false;
        }
        expected.pc=exit;
        std::memcpy(after.data(),rdram+sp-80,after.size());
        std::memcpy(rdram+sp-80,before.data(),before.size());
        if(slot==7) {
            std::memcpy(hudAfter.data(),rdram+0x0726F000,hudAfter.size());
            std::memcpy(rdram+0x0726F000,hudBefore.data(),hudBefore.size());
        }
        if(slot==13) {
            std::memcpy(throwsAfter.data(),rdram+0x0778F000,throwsAfter.size());
            std::memcpy(rdram+0x0778F000,throwsBefore.data(),throwsBefore.size());
        }
        // Re-enter the original interpreter, never dispatch this pack recursively.
        comparing=true;
        const bool ok=runtime->interpretUntil(rdram,ctx,exit,true);
        comparing=false;
        if(!ok || ctx->pc!=expected.pc || std::memcmp(ctx->r,expected.r,sizeof(ctx->r)) ||
           std::memcmp(rdram+sp-80,after.data(),after.size()) ||
           (slot==7 && std::memcmp(rdram+0x0726F000,hudAfter.data(),hudAfter.size())) ||
           (slot==13 && std::memcmp(rdram+0x0778F000,throwsAfter.data(),throwsAfter.size()))) {
            std::fprintf(stderr,"[native-targets] ORACLE MISMATCH entry=%u expected_pc=0x%x actual_pc=0x%x\n",slot,exit,ctx->pc);
            runtime->requestStop();
        } else {
            if(++stats.oracle[slot]==64)
                std::fprintf(stderr,"[native-targets] oracle PASS entry=%u samples=64; continuing native\n",slot);
        }
        traceResult();return true;
    }
    TargetOps ops{rdram,ctx,runtime};uint32_t exit=0;
    if(!ps2x::tagteam::frozen::execute(rdram,ctx->pc,ops,exit)) {
        ++stats.fallback[slot];return false;
    }
    ctx->pc=exit;++stats.native[slot];traceResult();return true;
}

bool ps2xTagteamNativeElfEntry(uint32_t pc) { return ps2x::tagteam::frozen::isElfEntry(pc); }
const uint32_t* ps2xTagteamNativeElfEntries(unsigned& count) { count=unsigned(std::size(ps2x::tagteam::frozen::elfEntries));return ps2x::tagteam::frozen::elfEntries; }
