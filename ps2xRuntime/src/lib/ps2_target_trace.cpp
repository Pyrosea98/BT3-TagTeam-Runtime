#include "ps2_runtime_macros.h"
#include "runtime/ps2_target_trace.h"
#include <cstdlib>
#include <cstdio>
namespace {
bool enabled() {
    static const bool value=[] {const char *v=std::getenv("PS2X_TARGET_TRACE");return v&&*v&&*v!='0';}();
    return value;
}
void output(const char *line) { std::fprintf(stderr,"%s\n",line);std::fflush(stderr); }
ps2x::tagteam::TargetTrace &trace() {thread_local ps2x::tagteam::TargetTrace value(output);return value;}
}
void ps2xTagteamTraceTargetSample(uint8_t *ram) {
    if(enabled())trace().sample(ram,PS2_RAM_SIZE);
}
void ps2xTagteamTraceNativePhysicalRead(uint8_t *ram,uint32_t site,uint32_t argument,uint32_t result) {
    if(enabled())trace().nativePhysicalRead(ram,PS2_RAM_SIZE,site,argument,result);
}
void ps2xTagteamTraceTargetInstruction(uint8_t *ram,R5900Context *ctx,uint32_t pc,uint32_t insn) {
    if(!enabled())return;
    // This is the relocated base resolver beneath throw/beam/dash/contact
    // wrappers. Observe its real return, without changing the wrapper chain.
    if(pc>=0x07784400&&pc<0x07784800) {
        if(insn==0x03E00008u)
            trace().resolverRead(ram,PS2_RAM_SIZE,GPR_U32(ctx,31)-8,GPR_U32(ctx,4),GPR_U32(ctx,2),pc);
        return;
    }
    if(pc==0x073D6000||pc==0x073D7000)trace().sample(ram,PS2_RAM_SIZE);
    if(insn==0x8D2C0148u) { // LW t4,328(t1): raw pad load after real pad resolution
        const uint32_t physical=GPR_U32(ctx,18);
        const uint32_t previous=pc<0x073D7000?0x073D6820+4*physical:GPR_U32(ctx,21)+0x20;
        trace().input(ram,PS2_RAM_SIZE,physical,GPR_U32(ctx,19),GPR_U32(ctx,9),previous,pc);
    }
    // Observe the actual queue layout selected by the emitted load.
    if((insn&0xffff0000u)==0x8EA80000u)
        trace().consumer(ram,PS2_RAM_SIZE,GPR_U32(ctx,18),GPR_U32(ctx,19),GPR_U32(ctx,21)+(insn&65535),pc);
    // Candidate pointer load follows the parity check; registers identify the
    // actual selected physical candidate. HP load is after the slot guard.
    if(insn==0x8D090000u&&GPR_U32(ctx,8)==0xD8040+4*GPR_U32(ctx,14)) {
        uint32_t pointer=0;const uint32_t at=GPR_U32(ctx,8);
        if(at<PS2_RAM_SIZE-4)std::memcpy(&pointer,ram+at,4);
        trace().candidate(ram,PS2_RAM_SIZE,GPR_U32(ctx,18),GPR_U32(ctx,14),pointer,pc,false);
    }
    if(insn==0x8D4A09E4u)
        trace().candidate(ram,PS2_RAM_SIZE,GPR_U32(ctx,18),GPR_U32(ctx,14),GPR_U32(ctx,9),pc,true);
}
