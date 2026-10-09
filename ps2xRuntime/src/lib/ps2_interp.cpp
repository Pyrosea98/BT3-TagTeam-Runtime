// MIPS R5900 interpreter fallback.
//
// BT3 (and games like it) load their per-frame game-state logic as EE code
// OVERLAYS from disc into RAM at runtime (e.g. the jal 0x336a90 in the main
// loop). A static recompiler never sees that code, so those calls would
// otherwise be skipped. This interpreter runs any such non-recompiled code,
// invoking recompiled functions natively whenever execution reaches them, so
// overlays and static code interoperate.
//
// It reuses the runtime's own READ*/WRITE*/GPR_*/SET_GPR_* macros (which expand
// against the locals `rdram`, `ctx`, `runtime`), so its memory and register
// semantics are identical to the recompiled code.

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_coverage.h"   // [coverage]
#include "runtime/ps2_interp_budget.h"
#include "runtime/ps2_interp_decode.h"
#include "runtime/ps2_training_cleanup.h"
#include <fstream>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
extern void ps2xTagteamCodeWritten(uint32_t address, uint32_t size);
extern uint32_t ps2xTagteamBootInsn(uint32_t pc, uint32_t insn, R5900Context *ctx);
extern bool ps2xTagteamNativeElfEntry(uint32_t pc);
extern const uint32_t* ps2xTagteamNativeElfEntries(unsigned& count);
extern bool ps2xTagteamRunNativeTargets(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
extern void ps2xTagteamTraceTargetInstruction(uint8_t *rdram, R5900Context *ctx, uint32_t pc, uint32_t insn);
extern bool ps2xTagteamRunNativeHudDraw(uint8_t *rdram,R5900Context *ctx,PS2Runtime* runtime);
extern bool ps2xTagteamRunNativeTrainingDamage(uint8_t *rdram,R5900Context *ctx,PS2Runtime *runtime);
static thread_local int interpreterBlocksTestOverride=-1;

namespace
{
    // Optional exact instruction histogram, shared by guest fibers on this
    // host thread. Count delay slots too, without double-counting native code.
    // Thread teardown prints the aggregate rather than every nested call.
    struct InterpreterCostProfile {
        std::array<uint64_t, 0x8000> pages{};
        uint64_t total = 0, main = 0, outsideRam = 0;
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point reported = started;
        void instruction(uint32_t pc, bool mainContext) {
            ++total; main += mainContext;
            if ((pc >> 12) < pages.size()) ++pages[pc >> 12];
            else ++outsideRam;
            if((total&65535)==0 && std::chrono::steady_clock::now()-reported>=std::chrono::seconds(20)) {
                report();reported=std::chrono::steady_clock::now();
            }
        }
        void report() {
            if (!total) return;
            std::vector<std::pair<uint64_t, uint32_t>> ranked;
            for (uint32_t i=0; i<pages.size(); ++i)
                if (pages[i]) ranked.emplace_back(pages[i], i << 12);
            std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first>b.first; });
            const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::fprintf(stderr,"[interp-profile] total=%llu main=%llu service=%llu outside_ram=%llu elapsed_s=%.3f delay_slots=included\n",
                (unsigned long long)total,(unsigned long long)main,(unsigned long long)(total-main),(unsigned long long)outsideRam,seconds);
            for (size_t i=0; i<std::min(size_t(30),ranked.size()); ++i)
                std::fprintf(stderr,"[interp-profile-page] rank=%zu base=0x%08x instructions=%llu percent=%.3f\n",
                    i+1,ranked[i].second,(unsigned long long)ranked[i].first,100.0*ranked[i].first/total);
        }
        ~InterpreterCostProfile() {report();}
    };
    InterpreterCostProfile *interpreterCostProfile() {
        static const bool enabled=[] { const char *v=std::getenv("PS2X_INTERP_PROFILE"); return v && *v && *v!='0'; }();
        if (!enabled) return nullptr;
        thread_local InterpreterCostProfile profile;
        return &profile;
    }
    struct InterpreterFrame { uint32_t entry, stop; };
    struct InterpreterTrace {
        std::vector<InterpreterFrame> frames;
        std::array<uint32_t, 16> pcs{};
        uint32_t next = 0, count = 0;
        uint32_t lastEntry = 0, lastStop = 0, lastExit = 0, lastRa = 0;
        uint64_t equalEntryReturns = 0;
    };
    // Key by guest context so fibers sharing a host thread retain separate traces.
    thread_local std::unordered_map<R5900Context *, InterpreterTrace> interpreterTraces;
    bool interpreterTraceEnabled() {
        static const bool enabled = [] {
            const char *v = std::getenv("PS2X_STALL_INTERP");
            return v && v[0] && v[0] != '0';
        }();
        return enabled;
    }
    struct InterpreterTraceScope {
        R5900Context *ctx;
        InterpreterTrace *trace;
        InterpreterTraceScope(R5900Context *context, uint32_t stop) : ctx(context),
            trace(interpreterTraceEnabled() ? &interpreterTraces[context] : nullptr) {
            if (!trace) return;
            trace->frames.push_back({ctx->pc, stop});
            trace->lastEntry = ctx->pc;
            trace->lastStop = stop;
            if (ctx->pc == stop) ++trace->equalEntryReturns;
        }
        ~InterpreterTraceScope() {
            if (!trace) return;
            trace->lastExit = ctx->pc;
            trace->lastRa = uint32_t(_mm_extract_epi32(ctx->r[31], 0));
            trace->frames.pop_back();
        }
        void instruction(uint32_t pc) {
            if (!trace) return;
            trace->pcs[trace->next] = pc;
            trace->next = (trace->next + 1u) % trace->pcs.size();
            if (trace->count < trace->pcs.size()) ++trace->count;
        }
    };
    inline uint32_t fetchInsn(uint8_t *rdram, uint32_t pc)
    {
        return Ps2FastRead32(rdram, pc);
    }

    inline uint32_t signExtend16(uint16_t v)
    {
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
    }

    std::atomic<uint32_t> g_interpUnknownLog{0};
    std::atomic<uint64_t> g_interpInsnCount{0};
    struct InterpreterCounter {
        uint32_t pending=0;
        void instruction(){if(++pending==1024){g_interpInsnCount.fetch_add(pending,std::memory_order_relaxed);pending=0;}}
        ~InterpreterCounter(){if(pending)g_interpInsnCount.fetch_add(pending,std::memory_order_relaxed);}
    };

    Ps2DecodedInstruction decodeInstruction(uint32_t pc,uint32_t word) {
        // Comparison switch until the same-roster live benchmark establishes
        // whether this saves time on this compiler/CPU. No block execution yet.
        static const bool enabled=[] { const char *v=std::getenv("PS2X_INTERP_PREDECODE"); return v && *v=='1'; }();
        if(!enabled)return Ps2DecodedInstruction(word);
        thread_local Ps2InstructionDecodeCache cache;
        return cache.get(pc,word);
    }
    // Execute a single NON-control-flow instruction (also used for delay slots).
    // Returns true if handled; false if the opcode is unknown/unsupported.
    bool execSimple(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t insn, uint32_t pc,
                    const Ps2DecodedInstruction* cached=nullptr)
    {
        const auto d=cached?*cached:decodeInstruction(pc,insn);
        const uint32_t op=d.op, rs=d.rs, rt=d.rt, rd=d.rd, sa=d.sa, funct=d.funct, simm=d.simm;
        const uint16_t imm=d.imm;

        switch (op)
        {
        case 0x00: // SPECIAL
            switch (funct)
            {
            case 0x00: // sll
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rt) << sa));
                return true;
            case 0x02: // srl
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rt) >> sa));
                return true;
            case 0x03: // sra
                SET_GPR_S32(ctx, rd, (int32_t)((int32_t)GPR_U32(ctx, rt) >> sa));
                return true;
            case 0x04: // sllv
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rt) << (GPR_U32(ctx, rs) & 31u)));
                return true;
            case 0x06: // srlv
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rt) >> (GPR_U32(ctx, rs) & 31u)));
                return true;
            case 0x07: // srav
                SET_GPR_S32(ctx, rd, (int32_t)((int32_t)GPR_U32(ctx, rt) >> (GPR_U32(ctx, rs) & 31u)));
                return true;
            case 0x0a: // movz
                if (GPR_U64(ctx, rt) == 0) SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs));
                return true;
            case 0x0b: // movn
                if (GPR_U64(ctx, rt) != 0) SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs));
                return true;
            case 0x0f: // sync
                return true;
            case 0x30: // tge
            case 0x31: // tgeu
            case 0x32: // tlt
            case 0x33: // tltu
            case 0x34: // teq
            case 0x36: // tne
                // Conditional traps: compiler div-by-zero / bounds guards. Real
                // traps in game code are effectively never taken; treat as no-op.
                return true;
            case 0x28: // mfsa (R5900)
                SET_GPR_U64(ctx, rd, ctx->sa);
                return true;
            case 0x29: // mtsa (R5900)
                ctx->sa = GPR_U32(ctx, rs);
                return true;
            case 0x10: // mfhi
                SET_GPR_U64(ctx, rd, ctx->hi);
                return true;
            case 0x11: // mthi
                ctx->hi = GPR_U64(ctx, rs);
                return true;
            case 0x12: // mflo
                SET_GPR_U64(ctx, rd, ctx->lo);
                return true;
            case 0x13: // mtlo
                ctx->lo = GPR_U64(ctx, rs);
                return true;
            case 0x14: // dsllv
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) << (GPR_U32(ctx, rs) & 63u));
                return true;
            case 0x16: // dsrlv
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) >> (GPR_U32(ctx, rs) & 63u));
                return true;
            case 0x17: // dsrav
                SET_GPR_U64(ctx, rd, (uint64_t)((int64_t)GPR_U64(ctx, rt) >> (GPR_U32(ctx, rs) & 63u)));
                return true;
            case 0x18: // mult
            {
                int64_t p = (int64_t)(int32_t)GPR_U32(ctx, rs) * (int64_t)(int32_t)GPR_U32(ctx, rt);
                ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)(p & 0xffffffffu);
                ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)((uint64_t)p >> 32);
                if (rd) SET_GPR_S32(ctx, rd, (int32_t)(uint32_t)(p & 0xffffffffu));
                return true;
            }
            case 0x19: // multu
            {
                uint64_t p = (uint64_t)GPR_U32(ctx, rs) * (uint64_t)GPR_U32(ctx, rt);
                ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)(p & 0xffffffffu);
                ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(p >> 32);
                if (rd) SET_GPR_S32(ctx, rd, (int32_t)(uint32_t)(p & 0xffffffffu));
                return true;
            }
            case 0x1a: // div
            {
                int32_t a = (int32_t)GPR_U32(ctx, rs), b = (int32_t)GPR_U32(ctx, rt);
                if (b != 0 && !(a == (int32_t)0x80000000 && b == -1))
                {
                    ctx->lo = (uint64_t)(int64_t)(a / b);
                    ctx->hi = (uint64_t)(int64_t)(a % b);
                }
                else if (b == 0)
                {
                    ctx->lo = (uint64_t)(int64_t)(a < 0 ? 1 : -1);
                    ctx->hi = (uint64_t)(int64_t)a;
                }
                else
                {
                    ctx->lo = (uint64_t)(int64_t)(int32_t)0x80000000;
                    ctx->hi = 0;
                }
                return true;
            }
            case 0x1b: // divu
            {
                uint32_t a = GPR_U32(ctx, rs), b = GPR_U32(ctx, rt);
                if (b != 0)
                {
                    ctx->lo = (uint64_t)(int64_t)(int32_t)(a / b);
                    ctx->hi = (uint64_t)(int64_t)(int32_t)(a % b);
                }
                else
                {
                    ctx->lo = (uint64_t)(int64_t)(int32_t)0xffffffff;
                    ctx->hi = (uint64_t)(int64_t)(int32_t)a;
                }
                return true;
            }
            case 0x20: // add
            case 0x21: // addu
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rs) + GPR_U32(ctx, rt)));
                return true;
            case 0x22: // sub
            case 0x23: // subu
                SET_GPR_S32(ctx, rd, (int32_t)(GPR_U32(ctx, rs) - GPR_U32(ctx, rt)));
                return true;
            case 0x24: // and
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs) & GPR_U64(ctx, rt));
                return true;
            case 0x25: // or
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs) | GPR_U64(ctx, rt));
                return true;
            case 0x26: // xor
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs) ^ GPR_U64(ctx, rt));
                return true;
            case 0x27: // nor
                SET_GPR_U64(ctx, rd, ~(GPR_U64(ctx, rs) | GPR_U64(ctx, rt)));
                return true;
            case 0x2a: // slt
                SET_GPR_U64(ctx, rd, ((int64_t)GPR_U64(ctx, rs) < (int64_t)GPR_U64(ctx, rt)) ? 1u : 0u);
                return true;
            case 0x2b: // sltu
                SET_GPR_U64(ctx, rd, (GPR_U64(ctx, rs) < GPR_U64(ctx, rt)) ? 1u : 0u);
                return true;
            case 0x2c: // dadd
            case 0x2d: // daddu
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs) + GPR_U64(ctx, rt));
                return true;
            case 0x2e: // dsub
            case 0x2f: // dsubu
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rs) - GPR_U64(ctx, rt));
                return true;
            case 0x38: // dsll
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) << sa);
                return true;
            case 0x3a: // dsrl
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) >> sa);
                return true;
            case 0x3b: // dsra
                SET_GPR_U64(ctx, rd, (uint64_t)((int64_t)GPR_U64(ctx, rt) >> sa));
                return true;
            case 0x3c: // dsll32
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) << (sa + 32u));
                return true;
            case 0x3e: // dsrl32
                SET_GPR_U64(ctx, rd, GPR_U64(ctx, rt) >> (sa + 32u));
                return true;
            case 0x3f: // dsra32
                SET_GPR_U64(ctx, rd, (uint64_t)((int64_t)GPR_U64(ctx, rt) >> (sa + 32u)));
                return true;
            default:
                break;
            }
            break;

        case 0x08: // addi
        case 0x09: // addiu
            SET_GPR_S32(ctx, rt, (int32_t)(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x0a: // slti
            SET_GPR_U64(ctx, rt, ((int64_t)GPR_U64(ctx, rs) < (int64_t)(int32_t)simm) ? 1u : 0u);
            return true;
        case 0x0b: // sltiu
            SET_GPR_U64(ctx, rt, (GPR_U64(ctx, rs) < (uint64_t)(int64_t)(int32_t)simm) ? 1u : 0u);
            return true;
        case 0x0c: // andi
            SET_GPR_U64(ctx, rt, GPR_U64(ctx, rs) & (uint64_t)imm);
            return true;
        case 0x0d: // ori
            SET_GPR_U64(ctx, rt, GPR_U64(ctx, rs) | (uint64_t)imm);
            return true;
        case 0x0e: // xori
            SET_GPR_U64(ctx, rt, GPR_U64(ctx, rs) ^ (uint64_t)imm);
            return true;
        case 0x0f: // lui
            SET_GPR_S32(ctx, rt, (int32_t)((uint32_t)imm << 16));
            return true;
        case 0x10: // COP0: loading animation reads the EE Count register.
            if (rs == 0 && rd == 9 && (insn & 0x7ffu) == 0) {
                SET_GPR_S32(ctx, rt, (int32_t)ctx->cop0_count);
                return true;
            }
            break;
        case 0x18: // daddi
        case 0x19: // daddiu
            SET_GPR_U64(ctx, rt, GPR_U64(ctx, rs) + (uint64_t)(int64_t)(int32_t)simm);
            return true;

        case 0x1c: // R5900 MMI scalar secondary multiply/divide unit
            switch (funct) {
            case 0x00: // madd
            case 0x01: // maddu
            case 0x20: // madd1
            case 0x21: { // maddu1
                uint64_t &hi=(funct&0x20)?ctx->hi1:ctx->hi;
                uint64_t &lo=(funct&0x20)?ctx->lo1:ctx->lo;
                const uint64_t accumulator=(uint64_t(uint32_t(hi))<<32)|uint32_t(lo);
                const uint64_t product=(funct&1)
                    ? uint64_t(GPR_U32(ctx,rs))*uint64_t(GPR_U32(ctx,rt))
                    : uint64_t(int64_t(GPR_S32(ctx,rs))*int64_t(GPR_S32(ctx,rt)));
                const uint64_t result=accumulator+product;
                lo=uint64_t(int64_t(int32_t(result)));
                hi=uint64_t(int64_t(int32_t(result>>32)));
                if(rd)SET_GPR_S32(ctx,rd,int32_t(result));
                return true;
            }
            case 0x10: SET_GPR_U64(ctx,rd,ctx->hi1);return true; // mfhi1
            case 0x11: ctx->hi1=GPR_U64(ctx,rs);return true;
            case 0x12: SET_GPR_U64(ctx,rd,ctx->lo1);return true; // mflo1
            case 0x13: ctx->lo1=GPR_U64(ctx,rs);return true;
            case 0x18: // mult1
            case 0x19: { // multu1
                uint64_t product=funct==0x18
                    ? uint64_t(int64_t(GPR_S32(ctx,rs))*int64_t(GPR_S32(ctx,rt)))
                    : uint64_t(GPR_U32(ctx,rs))*uint64_t(GPR_U32(ctx,rt));
                ctx->lo1=uint64_t(int64_t(int32_t(product)));
                ctx->hi1=uint64_t(int64_t(int32_t(product>>32)));
                if(rd)SET_GPR_S32(ctx,rd,int32_t(product));
                return true;
            }
            case 0x1a: { // div1
                int32_t a=GPR_S32(ctx,rs),b=GPR_S32(ctx,rt);
                if(b==0){ctx->lo1=uint64_t(int64_t(a<0?1:-1));ctx->hi1=uint64_t(int64_t(a));}
                else if(a==INT32_MIN && b==-1){ctx->lo1=uint64_t(int64_t(INT32_MIN));ctx->hi1=0;}
                else {ctx->lo1=uint64_t(int64_t(a/b));ctx->hi1=uint64_t(int64_t(a%b));}
                return true;
            }
            case 0x1b: { // divu1
                uint32_t a=GPR_U32(ctx,rs),b=GPR_U32(ctx,rt);
                ctx->lo1=uint64_t(int64_t(int32_t(b?a/b:UINT32_MAX)));
                ctx->hi1=uint64_t(int64_t(int32_t(b?a%b:a)));return true;
            }
            default:break;
            }
            break;

        // ---- loads ----
        case 0x20: // lb
            SET_GPR_S32(ctx, rt, (int32_t)(int8_t)READ8(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x21: // lh
            SET_GPR_S32(ctx, rt, (int32_t)(int16_t)READ16(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x1e: // lq (128-bit, address forced to 16-byte alignment)
        {
            const uint32_t addr = (GPR_U32(ctx, rs) + simm) & ~0xFu;
            const uint64_t lo = READ64(addr), hi = READ64(addr + 8u);
            if (rt != 0u) ctx->r[rt] = _mm_set_epi64x((int64_t)hi, (int64_t)lo);
            return true;
        }
        case 0x1f: // sq
        {
            const uint32_t addr = (GPR_U32(ctx, rs) + simm) & ~0xFu;
            alignas(16) uint64_t v[2]; _mm_store_si128(reinterpret_cast<__m128i *>(v), ctx->r[rt]);
            WRITE64(addr, v[0]); WRITE64(addr + 8u, v[1]);
            ps2xTagteamCodeWritten(addr,16);
            return true;
        }
        case 0x23: // lw
            SET_GPR_S32(ctx, rt, (int32_t)READ32(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x24: // lbu
            SET_GPR_U64(ctx, rt, (uint64_t)READ8(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x25: // lhu
            SET_GPR_U64(ctx, rt, (uint64_t)READ16(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x27: // lwu
            SET_GPR_U64(ctx, rt, (uint64_t)READ32(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x37: // ld
            SET_GPR_U64(ctx, rt, READ64(GPR_U32(ctx, rs) + simm));
            return true;
        case 0x1a: // ldl (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint64_t w = READ64(addr & ~7u);
            uint32_t b = addr & 7u;
            uint32_t sh = 56u - b * 8u;
            uint64_t cur = GPR_U64(ctx, rt);
            uint64_t res = (w << sh) | (cur & ~(~0ull << sh));
            SET_GPR_U64(ctx, rt, res);
            return true;
        }
        case 0x1b: // ldr (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint64_t w = READ64(addr & ~7u);
            uint32_t b = addr & 7u;
            uint32_t sh = b * 8u;
            uint64_t cur = GPR_U64(ctx, rt);
            uint64_t res = (w >> sh) | (cur & ~(~0ull >> sh));
            SET_GPR_U64(ctx, rt, res);
            return true;
        }
        case 0x22: // lwl (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t w = READ32(addr & ~3u);
            uint32_t b = addr & 3u;
            uint32_t sh = 24u - b * 8u;
            uint32_t cur = GPR_U32(ctx, rt);
            uint32_t res = (w << sh) | (cur & ~(0xffffffffu << sh));
            SET_GPR_S32(ctx, rt, (int32_t)res);
            return true;
        }
        case 0x26: // lwr (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t w = READ32(addr & ~3u);
            uint32_t b = addr & 3u;
            uint32_t sh = b * 8u;
            uint32_t cur = GPR_U32(ctx, rt);
            uint32_t res = (w >> sh) | (cur & ~(0xffffffffu >> sh));
            SET_GPR_S32(ctx, rt, (int32_t)res);
            return true;
        }

        // ---- stores ----
        case 0x28: // sb
            WRITE8(GPR_U32(ctx, rs) + simm, (uint8_t)GPR_U32(ctx, rt));
            ps2xTagteamCodeWritten(GPR_U32(ctx,rs)+simm,1);
            return true;
        case 0x29: // sh
            WRITE16(GPR_U32(ctx, rs) + simm, (uint16_t)GPR_U32(ctx, rt));
            ps2xTagteamCodeWritten(GPR_U32(ctx,rs)+simm,2);
            return true;
        case 0x2b: // sw
            WRITE32(GPR_U32(ctx, rs) + simm, GPR_U32(ctx, rt));
            ps2xTagteamCodeWritten(GPR_U32(ctx,rs)+simm,4);
            return true;
        case 0x3f: // sd
            WRITE64(GPR_U32(ctx, rs) + simm, GPR_U64(ctx, rt));
            ps2xTagteamCodeWritten(GPR_U32(ctx,rs)+simm,8);
            return true;
        case 0x2c: // sdl (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t aligned = addr & ~7u;
            uint32_t b = addr & 7u;
            uint32_t sh = 56u - b * 8u;
            uint64_t w = READ64(aligned);
            uint64_t val = GPR_U64(ctx, rt);
            uint64_t res = (w & ~(~0ull >> sh)) | (val >> sh);
            WRITE64(aligned, res);
            return true;
        }
        case 0x2d: // sdr (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t aligned = addr & ~7u;
            uint32_t b = addr & 7u;
            uint32_t sh = b * 8u;
            uint64_t w = READ64(aligned);
            uint64_t val = GPR_U64(ctx, rt);
            uint64_t res = (w & ~(~0ull << sh)) | (val << sh);
            WRITE64(aligned, res);
            return true;
        }
        case 0x2a: // swl (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t aligned = addr & ~3u;
            uint32_t b = addr & 3u;
            uint32_t sh = 24u - b * 8u;
            uint32_t w = READ32(aligned);
            uint32_t val = GPR_U32(ctx, rt);
            uint32_t res = (w & ~(0xffffffffu >> sh)) | (val >> sh);
            WRITE32(aligned, res);
            return true;
        }
        case 0x2e: // swr (little-endian)
        {
            uint32_t addr = GPR_U32(ctx, rs) + simm;
            uint32_t aligned = addr & ~3u;
            uint32_t b = addr & 3u;
            uint32_t sh = b * 8u;
            uint32_t w = READ32(aligned);
            uint32_t val = GPR_U32(ctx, rt);
            uint32_t res = (w & ~(0xffffffffu << sh)) | (val << sh);
            WRITE32(aligned, res);
            return true;
        }

        case 0x2f: // cache
        case 0x33: // pref
            return true;

        // ---- COP1 (FPU) load/store ----
        case 0x31: // lwc1
        {
            uint32_t v = READ32(GPR_U32(ctx, rs) + simm);
            std::memcpy(&ctx->f[rt], &v, sizeof(float));
            return true;
        }
        case 0x39: // swc1
        {
            uint32_t v;
            std::memcpy(&v, &ctx->f[rt], sizeof(float));
            WRITE32(GPR_U32(ctx, rs) + simm, v);
            return true;
        }

        // ---- COP1 (FPU) ops ----
        case 0x11:
        {
            uint32_t fmt = rs; // rs field selects op class
            uint32_t ft = rt, fs = rd, fdd = sa;
            if (fmt == 0x00) // mfc1
            {
                uint32_t v;
                std::memcpy(&v, &ctx->f[fs], sizeof(float));
                SET_GPR_S32(ctx, rt, (int32_t)v);
                return true;
            }
            if (fmt == 0x04) // mtc1
            {
                uint32_t v = GPR_U32(ctx, rt);
                std::memcpy(&ctx->f[fs], &v, sizeof(float));
                return true;
            }
            if (fmt == 0x02) // cfc1
            {
                SET_GPR_S32(ctx, rt, (int32_t)ctx->fcr31);
                return true;
            }
            if (fmt == 0x06) // ctc1
            {
                ctx->fcr31 = GPR_U32(ctx, rt);
                return true;
            }
            if (fmt == 0x10) // single-precision op
            {
                float a = ctx->f[fs], b = ctx->f[ft], r = 0.0f;
                switch (funct)
                {
                case 0x00: r = a + b; break;              // add.s
                case 0x01: r = a - b; break;              // sub.s
                case 0x02: r = a * b; break;              // mul.s
                case 0x03: r = (b != 0.0f) ? a / b : 0.0f; break; // div.s
                case 0x04: { r = a > 0 ? std::sqrt(a) : 0.0f; break; } // sqrt.s
                case 0x05: r = a < 0 ? -a : a; break;     // abs.s
                case 0x06: r = a; break;                  // mov.s
                case 0x07: r = -a; break;                 // neg.s
                case 0x18: r = a + b; break;              // adda-ish (approx)
                case 0x24: { int32_t iv = (int32_t)a; std::memcpy(&ctx->f[fdd], &iv, sizeof(int32_t)); return true; } // cvt.w.s
                default:
                    // comparisons c.*.s set fcr31 bit 23
                    if (funct >= 0x30)
                    {
                        bool res = false;
                        switch (funct & 0xf)
                        {
                        case 0x2: res = (a == b); break; // c.eq
                        case 0xc: res = (a < b); break;  // c.lt
                        case 0xe: res = (a <= b); break; // c.le
                        case 0x0: res = false; break;    // c.f
                        default: res = (a < b); break;
                        }
                        if (res) ctx->fcr31 |= (1u << 23);
                        else ctx->fcr31 &= ~(1u << 23);
                        return true;
                    }
                    goto fpu_unknown;
                }
                ctx->f[fdd] = r;
                return true;
            }
            if (fmt == 0x14) // cvt.s.w (word->single)
            {
                int32_t iv;
                std::memcpy(&iv, &ctx->f[fs], sizeof(int32_t));
                ctx->f[fdd] = (float)iv;
                return true;
            }
        fpu_unknown:
            break;
        }

        default:
            break;
        }

        uint32_t n = g_interpUnknownLog.fetch_add(1);
        if (n < 60u)
            std::cerr << "[interp] UNKNOWN op=0x" << std::hex << op << " funct=0x" << funct
                      << " insn=0x" << insn << " pc=0x" << pc << std::dec << std::endl;
        return false;
    }
} // namespace

extern bool ps2xTagteamNeedsInterpret(uint32_t);

void ps2xDumpInterpreterDiagnostics(R5900Context *ctx) {
    const auto it = interpreterTraces.find(ctx);
    if (it == interpreterTraces.end()) {
        std::fprintf(stderr, "[stall-interp] no trace for guest context (enable PS2X_STALL_INTERP=1)\n");
        return;
    }
    const InterpreterTrace &t = it->second;
    const uint32_t ra = uint32_t(_mm_extract_epi32(ctx->r[31], 0));
    std::fprintf(stderr, "[stall-interp] pc=0x%x ra=0x%x active=%zu last_entry=0x%x last_return=0x%x last_exit=0x%x last_ra=0x%x equal_entry_returns=%llu\n",
        ctx->pc, ra, t.frames.size(), t.lastEntry, t.lastStop, t.lastExit, t.lastRa,
        static_cast<unsigned long long>(t.equalEntryReturns));
    for (size_t i = 0; i < t.frames.size(); ++i)
        std::fprintf(stderr, "[stall-interp-stack] depth=%zu entry=0x%x return=0x%x current=0x%x ra=0x%x\n",
            i, t.frames[i].entry, t.frames[i].stop, ctx->pc, ra);
    std::fprintf(stderr, "[stall-interp-pcs] oldest_first count=%u", t.count);
    for (uint32_t i = 0; i < t.count; ++i)
        std::fprintf(stderr, " 0x%x", t.pcs[(t.next + t.pcs.size() - t.count + i) % t.pcs.size()]);
    std::fprintf(stderr, "\n");
}

bool PS2Runtime::interpretUntil(uint8_t *rdram, R5900Context *ctx, uint32_t returnPc,
                               bool executeEntry)
{
    InterpreterTraceScope traceScope(ctx, returnPc);
    PS2Runtime *runtime = this;
    ps2cov::noteEeOverlay(ctx->pc);   // [coverage] overlay entry point that has no recompiled function
    Ps2InterpreterBudget budget;
    InterpreterCounter counter;
    bool firstEntry = true;
    const bool mainContext = ctx == &m_cpuContext;
    auto *costProfile = interpreterCostProfile();
    static const bool fightProfileOnly=[] {const char* v=std::getenv("PS2X_INTERP_PROFILE_FIGHT_ONLY");return v && *v=='1';}();
    if(costProfile && fightProfileOnly) {
        auto u=[&](uint32_t at){uint32_t value;std::memcpy(&value,rdram+at,4);return value;};
        const auto phase=u(0x2FEB38);
        if(u(0xD8080)!=1 || phase<0x100000 || phase>=0x02000000-4 || u(phase)!=3 ||
           u(0xC4004)!=0 || u(0x073E1C00)!=0)costProfile=nullptr;
    }
    const auto entered = std::chrono::steady_clock::now();
    auto progressAt = entered;
    static const bool blocksEnabled=[]{const char* v=std::getenv("PS2X_INTERP_BLOCKS");return !(v && *v=='0');}();
    thread_local Ps2StraightBlockCache blocks;

    // A patched block can be resumed by top-level dispatch at the previous
    // call's return address. Execute that entry before checking its stop PC,
    // otherwise a stale link register makes the block return without progress.
    while (ctx->pc != returnPc || (executeEntry && firstEntry))
    {
        firstEntry = false;
        if (isStopRequested()) return false;

        const uint32_t pc = ctx->pc;

        if(pc==0x1CE630u && ps2xTagteamRunNativeTrainingDamage(rdram,ctx,runtime)) {
            budget.nativeProgress();progressAt=std::chrono::steady_clock::now();continue;
        }

        if ((pc==0x2188B8u || pc==0x073DA200u || pc==0x07272000u || pc==0x072D1800u || pc==0x07413400u || pc==0x07414000u || pc==0x07414800u || (pc>0x070D6800u && pc<0x070D6C00u)) &&
            ps2xTagteamRunNativeHudDraw(rdram,ctx,runtime)) {
            budget.nativeProgress();
            progressAt=std::chrono::steady_clock::now();
            continue;
        }

        if (((pc & 0xfffff000u) == 0x07368000u || pc==0x077C4000u || pc==0x070B0400u ||
             pc==0x070B1000u || pc==0x07260000u || pc==0x071A0000u ||
             pc==0x07243000u || pc==0x07180000u || pc==0x070F0000u || pc==0x070F0400u ||
             pc==0x07781000u || pc==0x07788000u || pc==0x06944000u || pc==0x06944400u || (pc<0x400000u && ps2xTagteamNativeElfEntry(pc))) && ps2xTagteamRunNativeTargets(rdram, ctx, runtime))
        {
            budget.nativeProgress();
            progressAt = std::chrono::steady_clock::now();
            continue;
        }

        // If we've reached recompiled code, run it natively (it runs its whole
        // subtree and returns with ctx->pc set to its return target).
        static const bool presenceCacheEnabled=[] {const char* v=std::getenv("PS2X_INTERP_DISPATCH_CACHE");return !(v && *v=='0');}();
        thread_local Ps2GeneratedPresenceCache presenceCache;
        const bool generated=presenceCacheEnabled?presenceCache.get(pc,[&](uint32_t address){return hasFunction(address);}):hasFunction(pc);
        if (generated && !ps2xTagteamNeedsInterpret(pc))
        {
            RecompiledFunction fn = lookupFunction(pc);
            fn(rdram, ctx, runtime);
            budget.nativeProgress();
            progressAt = std::chrono::steady_clock::now();
            continue;
        }

        auto blockEligible=[](uint32_t at){
            return ((at>=0x06000000u && at<0x07FFF000u) || (at>=0x1CE000u && at<0x1DB000u)) &&
                !(at>=0x073D6000u && at<0x073D8000u) && !(at>=0x07784400u && at<0x07784800u);
        };
        if((interpreterBlocksTestOverride<0?blocksEnabled:interpreterBlocksTestOverride!=0) && blockEligible(pc) && ps2StraightInstruction(fetchInsn(rdram,pc))) {
            auto& block=blocks.get(pc,[&](uint32_t at){return fetchInsn(rdram,at);},[&](uint32_t at){
                return !blockEligible(at) || at==0x1CE630u || at==0x2188B8u || at==0x073DA200u || at==0x07272000u || at==0x072D1800u || at==0x07413400u || at==0x07414000u || at==0x07414800u ||
                    (at>0x070D6800u && at<0x070D6C00u) || (at&0xFFFFF000u)==0x07368000u ||
                    at==0x077C4000u || at==0x070B0400u || at==0x070B1000u || at==0x07260000u ||
                    at==0x071A0000u || at==0x07243000u || at==0x07180000u ||
                    at==0x070F0000u || at==0x070F0400u || at==0x07781000u || at==0x07788000u ||
                    at==0x06944000u || at==0x06944400u || (at<0x400000u && ps2xTagteamNativeElfEntry(at)) ||
                    presenceCache.get(at,[&](uint32_t address){return hasFunction(address);});
            });
            // A memory operation can yield a guest fiber; another interpreter
            // may then replace a cache slot. Keep active instructions on this
            // invocation's stack, never execute through a shared cache reference.
            const auto count=block.count;
            std::array<Ps2StraightBlockCache::Instruction,32> active;
            std::copy_n(block.code.begin(),count,active.begin());
            unsigned executed=0;
            for(unsigned i=0;i<count;++i){
                const auto& instruction=active[i];
                if(i && ctx->pc==returnPc)break;
                if(fetchInsn(rdram,instruction.pc)!=instruction.word){if(block.pc==pc)blocks.invalidate(block);break;}
                if(!budget.instruction())return false;
                traceScope.instruction(instruction.pc);counter.instruction();
                if(costProfile)costProfile->instruction(instruction.pc,mainContext);
                if(!execSimple(rdram,ctx,runtime,instruction.word,instruction.pc,&instruction.decoded))return false;
                ctx->pc=instruction.pc+4;++executed;
            }
            if(executed)continue;
        }

        if (!budget.instruction())
        {
            const auto now=std::chrono::steady_clock::now();
            std::fprintf(stderr,"[interp] guard limit at pc=0x%x context=%s uninterrupted=%llu total=%llu native_dispatches=%llu elapsed_s=%.3f since_native_s=%.3f\n",
                pc,mainContext?"main":"service",(unsigned long long)budget.uninterrupted,
                (unsigned long long)budget.total,(unsigned long long)budget.nativeDispatches,
                std::chrono::duration<double>(now-entered).count(),std::chrono::duration<double>(now-progressAt).count());
            return false;
        }

        const uint32_t insn = ps2xTagteamBootInsn(pc,fetchInsn(rdram, pc),ctx);
        if ((pc >= 0x073D6000u && pc < 0x073D8000u) || (pc >= 0x07784400u && pc < 0x07784800u))
            ps2xTagteamTraceTargetInstruction(rdram,ctx,pc,insn);
        traceScope.instruction(pc);
        counter.instruction();
        if (costProfile) costProfile->instruction(pc, mainContext);

        const auto decoded=decodeInstruction(pc,insn);
        const uint32_t op=decoded.op,rs=decoded.rs,rt=decoded.rt,simm=decoded.simm,funct=decoded.funct;
        const uint16_t imm=decoded.imm;

        auto runDelaySlot = [&](uint32_t dsPc) -> bool
        {
            traceScope.instruction(dsPc);
            if (costProfile) costProfile->instruction(dsPc, mainContext);
            uint32_t dsInsn = fetchInsn(rdram, dsPc);
            if (dsInsn == 0) return true; // nop
            return execSimple(rdram, ctx, runtime, dsInsn, dsPc);
        };

        // ---- control-flow opcodes ----
        if (op == 0x00 && (funct == 0x08 || funct == 0x09)) // jr / jalr
        {
            uint32_t target = GPR_U32(ctx, rs);
            if (funct == 0x09)
            {
                uint32_t rd = (insn >> 11) & 31u;
                SET_GPR_U64(ctx, rd, pc + 8u);
            }
            if (!runDelaySlot(pc + 4u)) return false;
            ctx->pc = target;
            continue;
        }
        if (op == 0x00 && funct == 0x0c) // syscall
        {
            // Route through the runtime syscall path by executing the recompiled
            // syscall handler is not available here; syscalls in overlays are rare.
            uint32_t n = g_interpUnknownLog.fetch_add(1);
            if (n < 60u)
                std::cerr << "[interp] syscall in overlay at pc=0x" << std::hex << pc << std::dec << std::endl;
            ctx->pc = pc + 4u;
            continue;
        }
        if (op == 0x02 || op == 0x03) // j / jal
        {
            uint32_t target = (pc & 0xf0000000u) | ((insn & 0x03ffffffu) << 2);
            if (op == 0x03) SET_GPR_U64(ctx, 31, pc + 8u);
            if (!runDelaySlot(pc + 4u)) return false;
            ctx->pc = target;
            continue;
        }
        if (op == 0x01) // REGIMM: bltz/bgez/bltzal/bgezal (+ likely)
        {
            int64_t v = (int64_t)GPR_U64(ctx, rs);
            bool taken = false;
            bool likely = false;
            bool link = false;
            switch (rt)
            {
            case 0x00: taken = (v < 0); break;               // bltz
            case 0x01: taken = (v >= 0); break;              // bgez
            case 0x02: taken = (v < 0); likely = true; break; // bltzl
            case 0x03: taken = (v >= 0); likely = true; break; // bgezl
            case 0x10: taken = (v < 0); link = true; break;   // bltzal
            case 0x11: taken = (v >= 0); link = true; break;  // bgezal
            default: break;
            }
            if (link) SET_GPR_U64(ctx, 31, pc + 8u);
            uint32_t target = pc + 4u + (simm << 2);
            if (taken)
            {
                if (!runDelaySlot(pc + 4u)) return false;
                ctx->pc = target;
            }
            else if (likely)
            {
                ctx->pc = pc + 8u; // nullify delay slot
            }
            else
            {
                if (!runDelaySlot(pc + 4u)) return false;
                ctx->pc = pc + 8u;
            }
            continue;
        }
        if (op == 0x04 || op == 0x05 || op == 0x14 || op == 0x15) // beq/bne (+ likely)
        {
            bool eq = (GPR_U64(ctx, rs) == GPR_U64(ctx, rt));
            bool taken = (op == 0x04 || op == 0x14) ? eq : !eq;
            bool likely = (op == 0x14 || op == 0x15);
            uint32_t target = pc + 4u + (simm << 2);
            if (taken) { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = target; }
            else if (likely) { ctx->pc = pc + 8u; }
            else { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = pc + 8u; }
            continue;
        }
        if (op == 0x06 || op == 0x07 || op == 0x16 || op == 0x17) // blez/bgtz (+ likely)
        {
            int64_t v = (int64_t)GPR_U64(ctx, rs);
            bool cond = (op == 0x06 || op == 0x16) ? (v <= 0) : (v > 0);
            bool likely = (op == 0x16 || op == 0x17);
            uint32_t target = pc + 4u + (simm << 2);
            if (cond) { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = target; }
            else if (likely) { ctx->pc = pc + 8u; }
            else { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = pc + 8u; }
            continue;
        }
        if (op == 0x11 && rs == 0x08) // COP1 BC1 (bc1f/bc1t + likely)
        {
            bool cond = (ctx->fcr31 & (1u << 23)) != 0;
            bool wantTrue = (rt & 1u) != 0;   // ndtf bit
            bool likely = (rt & 2u) != 0;
            bool taken = (cond == wantTrue);
            uint32_t target = pc + 4u + (simm << 2);
            if (taken) { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = target; }
            else if (likely) { ctx->pc = pc + 8u; }
            else { if (!runDelaySlot(pc + 4u)) return false; ctx->pc = pc + 8u; }
            continue;
        }

        // ---- non-control-flow ----
        if (!execSimple(rdram, ctx, runtime, insn, pc))
        {
            return false;
        }
        ctx->pc = pc + 4u;
    }

    return true;
}
#include "ps2_interp_self_test.inc"
