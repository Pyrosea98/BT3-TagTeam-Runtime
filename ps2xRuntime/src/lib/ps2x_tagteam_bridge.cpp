// Experimental PINE transport for the existing Python Tag Team controller.
// Enabled only by PS2X_TAGTEAM_PORT; loopback only, no emulator state format.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#undef EXCEPTION_BREAKPOINT
#endif
#include "ps2_runtime.h"
#include "runtime/ps2_bulk_read.h"
#include "runtime/ps2_bridge_perf.h"
#include "runtime/ps2_ui_transport.h"
#include "runtime/ps2_ui_hud.h"
#include "runtime/ps2_gs_pgs.h"
#include "runtime/ps2_scene_packet_watch.h"
#include "runtime/ps2_ui_fight_intro.h"
#include "runtime/ps2_training_cleanup.h"
#include "runtime/ps2_native_seat_pads.h"
#include "runtime/ps2_native_seat_pads_test.h"
#include "runtime/pad_config.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>

extern "C" void *ps2xGuestWaitBegin();
extern "C" void ps2xGuestWaitEnd(void *);
extern "C" void ps2xGuestSleepMs(unsigned ms);
extern bool ps2xTagteamNeedsInterpret(uint32_t pc);
extern bool ps2xTagteamRunNativeHudDraw(uint8_t*,R5900Context*,PS2Runtime*);
extern void sub_002188B8_0x2188b8(uint8_t*,R5900Context*,PS2Runtime*);
extern void ps2xCaptureGuestExit(PS2Runtime*,uint8_t*,R5900Context*,const char*,uint32_t,uint32_t);

namespace {
struct Range { uint32_t begin, end; };
const Range ranges[] = {
#include "tagteam_ranges.inc"
};
std::atomic<bool> enabled{false}, stopping{false};
std::atomic<uint8_t> dirty[0x400000 / 4]{};
PS2Runtime *runtime = nullptr;
std::thread worker;
ps2_native_seats::Service nativeSeatPads;
struct BootWord { uint32_t address, value; };
std::vector<BootWord> recurringBootWords;
std::atomic<unsigned> hudStatusSuppressed{0};
std::atomic<unsigned> hudPromptsRetained{0};
constexpr uint32_t rematchControl = 0x07FFF100u, rematchMagic = 0x4E524D31u;
bool rematchEnabled = false;
bool detachedCleanupReceipt=false;
uint32_t detachedCleanupManager=0;
thread_local bool rebuildingMatch = false;
void mark(uint32_t address, uint32_t size) {
    if (!size || address >= 0x400000 || uint64_t(address) + size > 0x400000) return;
    // Power Scale patches tails embedded in these generated functions beyond
    // the analyzer's original function boundary. Honor their costume cycling
    // and extended-character lookup instructions through the containing body.
    const Range embedded[] = {{0x25efc8u,0x25f0d0u}, {0x25f0d0u,0x25f2e8u},
                              {0x2604c0u,0x261130u}};
    const Range hooks[] = {{0x25f048u,0x25f050u}, {0x25f248u,0x25f2a0u},
                          {0x260d20u,0x260d90u}};
    for (unsigned i=0;i<3;++i) {
        if (address < hooks[i].end && uint64_t(address)+size > hooks[i].begin)
            for (uint32_t p=embedded[i].begin/4;p<embedded[i].end/4;++p)
                dirty[p].store(1,std::memory_order_relaxed);
    }
    // Generated AF9C0 tail-calls B0030 directly, bypassing runtime lookup.
    // AFE70 also embeds the B0030 body beyond its original CSV boundary.
    // Interpret those callers when the projectile entry/body is patched so
    // both direct and embedded paths fetch the injected multi-contact hook.
    if (address < 0x1b0234 && uint64_t(address)+size > 0x1b0030) {
        for(uint32_t p=0x1af9c0/4;p<0x1b0234/4;++p)
            dirty[p].store(1,std::memory_order_relaxed);
    }
    // Generated 205280 tail-calls 1DB7B0 directly, bypassing dirty dispatch.
    // 131030 projectile steering obtains its target model through this path.
    // Interpret only that containing caller when its selector is patched.
    // Enabled by default after the focused live test; explicit 0 is a developer
    // comparison switch. Unpatched games do not enter this propagation branch.
    static const bool kiRoute=[] {const char *v=std::getenv("PS2X_KI_ROUTE");return !v||!*v||*v!='0';}();
    if (kiRoute && address < 0x1db7e4u && uint64_t(address)+size > 0x1db7b8u)
        for(uint32_t p=0x205260u/4;p<0x205298u/4;++p)
            dirty[p].store(1,std::memory_order_relaxed);
    for (const auto &r : ranges) {
        if (r.begin >= address + size) break;
        if (r.end <= address) continue;
        for (uint32_t p = r.begin / 4; p < (r.end + 3) / 4 && p < 0x100000; ++p)
            dirty[p].store(1, std::memory_order_relaxed);
    }
}
uint32_t readWord(const uint8_t *p) { uint32_t v; std::memcpy(&v,p,4); return v; }
void append(std::vector<uint8_t> &out, uint32_t v) {
    auto p = reinterpret_cast<const uint8_t *>(&v); out.insert(out.end(),p,p+4);
}
void string(std::vector<uint8_t> &out, const char *s) {
    uint32_t n = uint32_t(std::strlen(s))+1; append(out,n); out.insert(out.end(),s,s+n);
}
void markPacket(uint8_t *ram) {
    constexpr uint32_t control=0x0768f000, packet=0x07800000;
    const uint32_t count=readWord(ram+control+28), size=readWord(ram+control+32);
    if (count>1024 || size>0x800000 || size<uint64_t(count)*16) return;
    for (uint32_t i=0;i<count;++i) {
        const uint8_t *b=ram+packet+i*16;
        uint32_t at=readWord(b), n=readWord(b+4), old=readWord(b+8), data=readWord(b+12);
        if (!n || uint64_t(at)+n>PS2_RAM_SIZE || uint64_t(old)+n>size || uint64_t(data)+n>size) return;
        mark(at,n);
    }
}
void interpreted(uint8_t *ram, R5900Context *ctx, PS2Runtime *rt) {
    const uint32_t entry=ctx->pc;
    uint32_t ret=uint32_t(_mm_extract_epi32(ctx->r[31],0));
    if (!rt->interpretUntil(ram,ctx,ret,true)) {
        std::fprintf(stderr,"[tagteam] unsupported patched code at 0x%08x\n",ctx->pc);
        if(!rt->isStopRequested())
            ps2xCaptureGuestExit(rt,ram,ctx,"unsupported-interpreted-path",entry,ret);
        rt->requestStop();
    }
}
void nativeStatusDraw(uint8_t* ram,R5900Context* ctx,PS2Runtime* rt) {
    if(ps2xTagteamRunNativeHudDraw(ram,ctx,rt))return;
    // Cover generated calls too. Bypass only this wrapper on the untouched
    // original body; changed guest bodies retain their interpreted shim.
    if(ps2xTagteamNeedsInterpret(0x2188B8))interpreted(ram,ctx,rt);
    else sub_002188B8_0x2188b8(ram,ctx,rt);
}
thread_local bool trainingDamageActive=false;
void nativeTrainingDamage(uint8_t *ram,R5900Context *ctx,PS2Runtime *rt) {
    struct Scope {Scope(){trainingDamageActive=true;}~Scope(){trainingDamageActive=false;}} scope;
    const uint32_t actor=uint32_t(_mm_extract_epi32(ctx->r[4],0));
    uint32_t row=0,physical=255;int32_t before=0;
    auto screen=ps2x::ui::uiStore().lifecycle.snapshot(ps2x::ui::uiNow());
    if(screen.phase==ps2x::ui::PreparationPhase::Released &&
       (screen.mode==ps2x::ui::BattleMode::Training || screen.mode==ps2x::ui::BattleMode::TrainingCoop) &&
       actor>=0x100000 && actor<PS2_RAM_SIZE-0x1600) {
        physical=readWord(ram+actor);const auto slot=readWord(ram+actor+0x994);
        if(physical<10 && readWord(ram+0xD8040+4*physical)==actor && slot<5) {
            row=actor+0x9E4+slot*164;before=int32_t(readWord(ram+row));
        }
    }
    interpreted(ram,ctx,rt);
    if(row && before>0 && readWord(ram+0xD8040+4*physical)==actor) {
        const int32_t after=(std::max)(0,int32_t(readWord(ram+row)));
        if(after<before) {auto& store=ps2x::ui::hudStore();std::lock_guard lock(store.mutex);
            if(store.counterGeneration==screen.generation && store.state.training) {
                ++store.hits;store.damage=uint32_t((std::min)(uint64_t(UINT32_MAX),uint64_t(store.damage)+uint32_t(before-after)));
            }
        }
    }
}
void nativeOverheadProjection(uint8_t *ram,R5900Context *ctx,PS2Runtime *rt,ps2x::ui::HudSnapshot& h) {
    using namespace ps2x::ui;
    static const bool disabled=[](){const auto* v=std::getenv("PS2X_NATIVE_OVERHEAD_HUD");return v && std::strcmp(v,"0")==0;}();
    if(disabled)return;
    static uint64_t generation=0;static unsigned fadeFrames=0;
    if(generation!=h.generation){generation=h.generation;fadeFrames=0;}
    auto u=[&](uint32_t at){return readWord(ram+at);};
    if(!h.active){fadeFrames=0;return;}
    // Cinematic state now reduces plate size/opacity; it never hides status.
    fadeFrames=(std::min)(6u,fadeFrames+1);h.fade=float(fadeFrames)/6.f;
    auto valid=[](uint32_t p,uint32_t n){return p>=0x100000 && uint64_t(p)+n<=PS2_RAM_SIZE;};
    constexpr uint32_t bars=0x073DF000,quad=0x06C0F000;
    if(u(bars)!=1 || u(bars+4)!=h.manager)return;
    for(uint32_t pc:{0x120AB0u,0x120B80u,0x121ED8u,0x1210D8u,0x120AC8u})
        if(!rt->hasFunction(pc) || ps2xTagteamNeedsInterpret(pc))return;
    std::array<uint32_t,4> cameras{};unsigned count=0;
    uint8_t fusedSubject=0;
    if(sharedFusionView({ram,PS2_RAM_SIZE},h,fusedSubject)) {
        const uint32_t gp=uint32_t(_mm_extract_epi32(ctx->r[28],0));if(gp<22176 || gp>=PS2_RAM_SIZE)return;
        cameras[0]=u(gp-22176);h.views[0].subject=fusedSubject;count=1;
    } else if(u(quad)==0x51564131 && u(quad+4)==h.manager){
        count=u(quad+36);if(count<2 || count>4)return;
        for(unsigned i=0;i<count;++i){cameras[i]=0x06C05000+i*1024;h.views[i].subject=uint8_t(u(quad+64+4*i));}
    } else {
        const uint32_t gp=uint32_t(_mm_extract_epi32(ctx->r[28],0));if(gp<22172 || gp>=PS2_RAM_SIZE)return;
        const uint32_t base=u(gp-22172);count=u(0x331DEC)==1?2:1;
        if(!valid(base,1824+2*656))return;
        const unsigned singleSide=u(0x073AF00C)<2?u(0x073AF00C):0;
        for(unsigned i=0;i<count;++i){const unsigned side=count==1?singleSide:i;
            cameras[i]=base+1824+side*656;h.views[i].subject=uint8_t(u(0x073AF108+4*side));}
    }
    const uint32_t sp=uint32_t(_mm_extract_epi32(ctx->r[29],0));if(sp<4096 || !valid(sp-4096,4096))return;
    // Restore the entire borrowed stack, not only the projection result. The
    // projection helpers execute on the guest thread with a copied context.
    std::array<uint8_t,4096> saved{};std::memcpy(saved.data(),ram+sp-4096,saved.size());
    R5900Context service=*ctx;service.r[29]=_mm_set_epi32(0,0,0,int(sp-512));
    auto call=[&](uint32_t pc,uint32_t arg4=0,uint32_t arg5=0,uint32_t arg6=0){
        service.pc=pc;service.r[31]=_mm_set_epi32(0,0,0,0x0FFFFFFC);
        service.r[4]=_mm_set_epi32(0,0,0,int(arg4));service.r[5]=_mm_set_epi32(0,0,0,int(arg5));service.r[6]=_mm_set_epi32(0,0,0,int(arg6));
        rt->lookupFunction(pc)(ram,&service,rt);
    };
    for(unsigned i=0;i<count;++i){
        const uint32_t camera=cameras[i];auto& v=h.views[i];
        if(v.subject>=10 || !h.actors[v.subject].present || !valid(camera,656))continue;
        const auto x0=u(camera+512),x1=u(camera+516),y0=u(camera+520),y1=u(camera+524);
        if(x0>=x1 || x1>511 || y0>=y1 || y1>447)continue;
        v.x=float(x0);v.y=float(y0);v.width=float(x1-x0+1);v.height=float(y1-y0+1);v.valid=true;
        v.seat=uint8_t(i+1);
        const auto target=u(0xD8000+4*v.subject);
        if(target<10 && h.actors[target].present && h.actors[target].alive)v.target=uint8_t(target);
        call(0x120AB0);call(0x120B80,camera+320);
        for(unsigned physical=0;physical<10;++physical){
            const auto& a=h.actors[physical];bool reviveAnchor=false;
            for(const auto& revival:h.revives)reviveAnchor|=revival.state && revival.target==physical;
            if(!a.present || (!a.alive && !reviveAnchor))continue;
            const auto mid=u(a.pointer+12);if(mid>=12)continue;
            const auto model=u(0x31C640+4*mid);if(!valid(model,0x1670) || !u(model+4) || !u(model+8))continue;
            auto anchor=u(model+3436+48*4);anchor=valid(anchor,0xE0)?anchor+64:model+2416;
            call(0x121ED8,sp-192,anchor,bars+128);call(0x1210D8,sp-256,sp-192);
            const bool projected=uint32_t(_mm_extract_epi32(service.r[2],0))!=0;
            const int32_t depth=int32_t(u(sp-248)),w=int32_t(u(sp-244));if(!projected || depth<0 || w<=0)continue;
            auto& point=v.points[physical];point.x=int32_t(u(sp-256))/16.f-1792;point.y=int32_t(u(sp-252))/16.f-1824;
            point.valid=true;point.onScreen=point.x>=v.x && point.x<v.x+v.width && point.y>=v.y && point.y<v.y+v.height;
            // Projection1210D8 preserves homogeneous W (distance) in output+12.
            point.distance=float(w);
        }
        call(0x120AC8);h.viewCount=uint8_t(i+1);
        const float reference=v.points[v.subject].valid?v.points[v.subject].distance:100.f;
        for(auto& point:v.points)if(point.valid)point.scale=std::clamp(std::sqrt(reference/(std::max)(1.f,point.distance)),.7f,1.3f);
    }
    std::memcpy(ram+sp-4096,saved.data(),saved.size());
}
void nativeMatchBoundary(uint8_t *ram, R5900Context *ctx, PS2Runtime *rt) {
    const R5900Context caller = *ctx;
    const uint32_t returnPc = uint32_t(_mm_extract_epi32(ctx->r[31], 0));
    const bool rematch = ctx->pc == 0x12B570u;
    if (returnPc != (rematch ? 0x12BC10u : 0x12BD3Cu)) {
        rebuildingMatch = true;
        interpreted(ram, ctx, rt);
        rebuildingMatch = false;
        return;
    }
    auto put = [&](uint32_t offset, uint32_t value) {
        std::memcpy(ram + rematchControl + offset, &value, 4);
    };
    auto fail = [&](uint32_t error) {
        put(20, error); put(4, 5);
        std::fprintf(stderr, "[native-rematch] stopped at lifecycle boundary: error=%u\n", error);
        rt->requestStop();
    };
    auto invoke = [&](uint32_t pc) {
        R5900Context service = caller;
        service.pc = pc;
        service.r[31] = _mm_set_epi32(0, 0, 0, 0x0FFFFFFC);
        return rt->interpretUntil(ram, &service, 0x0FFFFFFC);
    };
    const uint32_t manager = readWord(ram + 0x2FEB14u);
    if (manager != readWord(ram + rematchControl + 12) || manager < 0x100000u ||
        manager >= 0x2000000u || readWord(ram + 0xD8080u) != 1) {
        fail(1); return;
    }
    // Refuse to tear down actors while an owned transformation job is pending.
    for (uint32_t control : {0x0765F000u, 0x0765E000u}) {
        if (readWord(ram + control + 4) != readWord(ram + control + 8)) {
            fail(2); return;
        }
    }
    rebuildingMatch = true;
    put(8, rematch ? 1 : 2);
    put(4, 6); // controller must retire its worker/input leases before teardown
    auto waitFor = [&](uint32_t wanted) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!rt->isStopRequested() && readWord(ram + rematchControl + 4) != wanted) {
            if (readWord(ram + rematchControl + 4) == 5 || std::chrono::steady_clock::now() >= deadline)
                return false;
            void *scope = ps2xGuestWaitBegin();
            ps2xGuestSleepMs(1u);
            ps2xGuestWaitEnd(scope);
        }
        return !rt->isStopRequested();
    };
    if (!waitFor(7)) { fail(3); rebuildingMatch = false; return; }
    // Existing native teardown invokes the mod's lifecycle detachment hook,
    // restoring native auxiliary pointers before freeing native allocations.
    if (!invoke(0x12B650u)) { fail(4); rebuildingMatch = false; return; }
    for (uint32_t address : {0x2FEB14u, 0x2FEC44u, 0x2FEAA8u, 0x2FEB38u, 0x2FF208u}) {
        if (readWord(ram + address)) { fail(5); rebuildingMatch = false; return; }
    }
    put(4, 2); // detached: controller restores owned hooks/reservations here
    if (!waitFor(3)) { fail(6); rebuildingMatch = false; return; }
    // The controller validated detachment and restored all registered ownership
    // ranges. No old expanded allocation is reused by the next setup.
    std::memset(ram + 0x02000000u, 0, 0x04000000u);
    for (uint32_t address : {0x2FF084u, 0x2FF08Cu}) std::memset(ram + address, 0, 4);
    if (rematch && !invoke(0x12B5E0u)) { fail(7); rebuildingMatch = false; return; }
    *ctx = caller;
    ctx->pc = returnPc;
    detachedCleanupManager=manager;detachedCleanupReceipt=true;
    put(4, 4);
    rebuildingMatch = false;
    std::fprintf(stderr, "[native-rematch] clean %s completed; fresh preparation required\n",
        rematch ? "teardown/setup" : "menu teardown");
}
#if defined(_WIN32)
std::atomic<SOCKET> listener{INVALID_SOCKET}, client{INVALID_SOCKET};
bool receive(SOCKET s, uint8_t *dst, size_t n) {
    while(n && !stopping.load()) { int got=recv(s,reinterpret_cast<char *>(dst),int(n),0); if(got<=0)return false; dst+=got;n-=got; }
    return n==0;
}
bool sendAll(SOCKET s,const std::vector<uint8_t> &out) {
    size_t p=0; while(p<out.size() && !stopping.load()) { int n=send(s,reinterpret_cast<const char *>(out.data()+p),int(out.size()-p),0);if(n<=0)return false;p+=n; }return p==out.size();
}
bool exchange(SOCKET s, uint8_t *ram) {
    uint8_t header[4]; if(!receive(s,header,4))return false;
    uint32_t size=readWord(header);if(size<5 || size>650000)return false;
    std::vector<uint8_t> in(size-4);if(!receive(s,in.data(),in.size()))return false;
    const auto serviceBegin=std::chrono::steady_clock::now();
    auto& perf=ps2x::bridgePerf();
    perf.packets.fetch_add(1,std::memory_order_relaxed);
    perf.receivedBytes.fetch_add(size,std::memory_order_relaxed);
    // Validate every opcode and range before applying any write from this batch.
    size_t p=0;bool valid=true;uint64_t operations=0;
    while(p<in.size()) {
        ++operations;
        uint8_t op=in[p++];
        if(op<8) {
            uint32_t width=1u<<(op&3);if(p+4+(op>=4?width:0)>in.size()){valid=false;break;}
            uint32_t at=readWord(in.data()+p);p+=4;
            if(uint64_t(at)+width>PS2_RAM_SIZE){valid=false;break;}if(op>=4)p+=width;
        } else if(op==16) {
            // Bulk reads cannot be mixed with writes/other operations. Validate
            // complete framing and range before touching RAM or constructing reply.
            if(p!=1 || in.size()!=9 || !ps2BulkReadValid(readWord(in.data()+p),readWord(in.data()+p+4),PS2_RAM_SIZE)) {
                valid=false;break;
            }
            p+=8;
        } else if(op==17) {
            if(p!=1 || in.size()!=1+ps2x::ui::UiPacketBytes ||
               !ps2x::ui::uiPacketValid({in.data()+1,in.size()-1})) {valid=false;break;}
            p=in.size();
        } else if(op==18) {
            if(p!=1 || in.size()!=1) {valid=false;break;}
        } else if(op!=8 && (op<11 || op>15)){valid=false;break;}
    }
    perf.operations.fetch_add(operations,std::memory_order_relaxed);
    if(!valid)perf.rejected.fetch_add(1,std::memory_order_relaxed);
    std::vector<uint8_t> out(5,0);out[4]=valid?0:0xff;p=0;
    if(valid)while(p<in.size()) {
        uint8_t op=in[p++];
        if(op<8) {
            uint32_t width=1u<<(op&3), at=readWord(in.data()+p);p+=4;
            if(op<4)out.insert(out.end(),ram+at,ram+at+width);
            else {
                if(at==0x0768f004 && width==4)markPacket(ram);
                mark(at,width);std::memcpy(ram+at,in.data()+p,width);p+=width;
            }
        } else switch(op) {
            case 17: {
                const bool accepted=ps2x::ui::uiPublish({in.data()+p,in.size()-p});
                p=in.size(); append(out,accepted?1:0);break;
            }
            case 18: {
                auto& ui=ps2x::ui::uiStore(); auto state=ui.lifecycle.snapshot(ps2x::ui::uiNow());
                const uint64_t drawn=ui.drawnRevision.load(), gen=ui.drawnGeneration.load();
                append(out,2);append(out,ui.rendererReady.load()?1:0);
                append(out,uint32_t(gen));append(out,uint32_t(gen>>32));
                append(out,uint32_t(drawn));append(out,uint32_t(drawn>>32));
                append(out,uint32_t(state.revision));append(out,uint32_t(state.screen));break;
            }
            case 8:string(out,"BT3-Recomp TagTeam bridge v1");break;
            case 11:string(out,"Power Scale BETA 1.5.1 / expanded 2x");break;
            case 12:string(out,"SLUS-21678");break;
            case 13:string(out,"9ACFE2DC");break;
            case 14:string(out,"Power Scale BETA 1.5.1");break;
            case 15:append(out,runtime->isStopRequested()?2:0);break;
            case 16: {
                const uint32_t at=readWord(in.data()+p), bytes=readWord(in.data()+p+4);
                p+=8;out.insert(out.end(),ram+at,ram+at+bytes);break;
            }
        }
    }
    const auto serviceEnd=std::chrono::steady_clock::now();
    perf.serviceNs.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(serviceEnd-serviceBegin).count()),std::memory_order_relaxed);
    if(out.size()>450000)return false;
    uint32_t length=uint32_t(out.size());std::memcpy(out.data(),&length,4);
    const bool sent=sendAll(s,out);
    perf.sendNs.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-serviceEnd).count()),std::memory_order_relaxed);
    if(sent)perf.sentBytes.fetch_add(out.size(),std::memory_order_relaxed);
    return sent;
}
void serve(int port) {
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return;
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);listener=s;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(s==INVALID_SOCKET || bind(s,reinterpret_cast<sockaddr *>(&address),sizeof(address)) || listen(s,4)) {
        std::fprintf(stderr,"[tagteam] cannot bind loopback PINE port %d (error %d)\n",port,WSAGetLastError());
    } else {
        std::fprintf(stderr,"[tagteam] PINE ready on 127.0.0.1:%d, %u MiB RAM\n",port,PS2_RAM_SIZE>>20);
        while(!stopping.load()) {
            fd_set ready;FD_ZERO(&ready);FD_SET(s,&ready);timeval wait{0,100000};
            if(select(0,&ready,nullptr,nullptr,&wait)<=0)continue;
            SOCKET c=accept(s,nullptr,nullptr);if(c==INVALID_SOCKET)continue;client=c;
            DWORD timeout=2000;setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));
            int on=1;setsockopt(c,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&on),sizeof(on));
            while(!stopping.load() && exchange(c,runtime->memory().getRDRAM())){}
            client=INVALID_SOCKET;closesocket(c);
        }
    }
    listener=INVALID_SOCKET;if(s!=INVALID_SOCKET)closesocket(s);WSACleanup();
}
#endif
}

bool ps2xTagteamNeedsInterpret(uint32_t pc) {
    return enabled.load(std::memory_order_relaxed) && pc<0x400000 && dirty[pc/4].load(std::memory_order_relaxed);
}
void ps2xTagteamCodeWritten(uint32_t address, uint32_t size) {
    if (!enabled.load(std::memory_order_relaxed) || address < 0x100000 || address >= 0x400000) return;
    if (!dirty[address/4].load(std::memory_order_relaxed)) mark(address,size);
}
uint32_t ps2xTagteamBootInsn(uint32_t pc, uint32_t insn, R5900Context *ctx) {
    if (!enabled.load(std::memory_order_relaxed)) return insn;
    // This exact Power Scale initializer loads an absolute-address module.
    // Wait for the controller to relocate the verified module in its own allocation.
    if (pc==0x33445c && insn==0x0040b02d) {
        const uint32_t base=uint32_t(_mm_extract_epi32(ctx->r[2],0));
        std::fprintf(stderr,"[tagteam] Power Scale module allocator returned 0x%x\n",base);
        uint8_t *ram=runtime->memory().getRDRAM();
        if (const char *snapshot=std::getenv("PS2X_TAGTEAM_BOOT_SNAPSHOT")) {
            std::ofstream output(snapshot,std::ios::binary);
            output.write(reinterpret_cast<const char *>(ram),PS2_RAM_SIZE);
        }
    }
    if (pc==0x334488) {
        const uint32_t base=uint32_t(_mm_extract_epi32(ctx->r[22],0));
        const uint32_t result=uint32_t(_mm_extract_epi32(ctx->r[2],0));
        if (base<PS2_RAM_SIZE-0x4000)
            std::fprintf(stderr,"[tagteam] Power Scale module read %u bytes at 0x%x, entry word 0x%x\n",result,base,readWord(runtime->memory().getRDRAM()+base+0x200));
        uint8_t *ram=runtime->memory().getRDRAM();
        constexpr uint32_t control=0x07fff000;
        uint32_t packet[]={0x42544d52,base,0};
        std::memcpy(ram+control,packet,sizeof(packet));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(readWord(ram+control+8)!=1 && !runtime->isStopRequested() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(readWord(ram+control+8)!=1) {
            std::fprintf(stderr,"[tagteam] Power Scale relocation timed out\n");runtime->requestStop();
        } else std::fprintf(stderr,"[tagteam] Power Scale relocation accepted\n");
    }
    return insn;
}
PS2Runtime::RecompiledFunction ps2xTagteamPatchedFunction(uint32_t pc) {
    static const bool nativeUi=std::getenv("PS2X_NATIVE_UI_SLICE")!=nullptr;
    if(nativeUi && runtime && pc==0x2188B8)return &nativeStatusDraw;
    if(nativeUi && runtime && pc==0x1CE630 && ps2x::ui::hudStore().trainingCounterEnabled.load(std::memory_order_relaxed) &&
       ps2xTagteamNeedsInterpret(pc) && ps2x::ui::trainingDamageHook({runtime->memory().getRDRAM(),PS2_RAM_SIZE}))return &nativeTrainingDamage;
    if (rematchEnabled && !rebuildingMatch && runtime) {
        const uint8_t *ram = runtime->memory().getRDRAM();
        if ((pc == 0x12B570u || pc == 0x12B650u) && readWord(ram + rematchControl) == rematchMagic &&
            readWord(ram + rematchControl + 4) == 1)
            return &nativeMatchBoundary;
    }
    return ps2xTagteamNeedsInterpret(pc)?&interpreted:nullptr;
}
bool ps2xTagteamRunNativeHudDraw(uint8_t *ram,R5900Context *ctx,PS2Runtime* rt) {
    using namespace ps2x::ui;
    static const bool enabledUi=std::getenv("PS2X_NATIVE_UI_SLICE")!=nullptr;
    if(!enabledUi)return false;
    auto& store=hudStore();const auto presented=store.presentedGeneration.load(std::memory_order_acquire);
    if(v11PromptProbePc(ctx->pc)){
        if(!presented || !uiStore().rendererReady.load())return false;
        const auto subject=uint32_t(_mm_extract_epi32(ctx->r[4],0));
        std::lock_guard lock(store.mutex);const auto& h=store.state;
        if(!h.active || h.paused || h.generation!=presented || subject>=10 ||
           readWord(ram+0x2FEB14)!=h.manager)return false;
        bool coverage=false;for(const auto& view:h.views)if(view.valid && view.subject==subject){
            if(ctx->pc==0x072533F0)coverage=true;
            else {coverage=true;for(unsigned i=0;i<10;++i)if(h.actors[i].present && h.actors[i].alive && !view.points[i].valid)coverage=false;}
        }
        if(!coverage)return false;
        const unsigned kind=ctx->pc==0x072533F0?1:ctx->pc==0x0694D0F0?2:3;
        if(!v11PromptGuard({ram,PS2_RAM_SIZE},kind))return false;
        ctx->r[2]=_mm_set_epi64x(0,1);ctx->pc=uint32_t(_mm_extract_epi32(ctx->r[31],0));return true;
    }
    if(ctx->pc==0x2188B8){
        bool hide=false,retain=false;
        {std::lock_guard lock(store.mutex);
            const auto gp=uint32_t(_mm_extract_epi32(ctx->r[28],0)),node=uint32_t(_mm_extract_epi32(ctx->r[4],0));
            hide=hideGameHudNode({ram,PS2_RAM_SIZE},store.state,gp,node);
            retain=!hide && replacementGameHudRoot({ram,PS2_RAM_SIZE},store.state,gp)!=0;
        }
        if(hide){++hudStatusSuppressed;ctx->pc=uint32_t(_mm_extract_epi32(ctx->r[31],0));return true;}
        // Both interpreted and generated callers retain original prompt
        // rendering. Never hold the snapshot mutex across recursive callbacks.
        if(retain && rt){++hudPromptsRetained;sub_002188B8_0x2188b8(ram,ctx,rt);return true;}
        return false;
    }
    if(ctx->pc==0x07414000 || ctx->pc==0x07414800){
        // A beam caption reaches the old text routine only when its native
        // presentation probe declined coverage. Keep that fallback visible.
        const auto caller=uint32_t(_mm_extract_epi32(ctx->r[31],0));
        if(ctx->pc==0x07414000 && caller>=0x07252C00 && caller<0x07253400)return false;
        const auto screen=uiStore().lifecycle.snapshot(uiNow());
        const auto next=ownedModTextDrawPc({ram,PS2_RAM_SIZE},screen,ctx->pc,uint32_t(_mm_extract_epi32(ctx->r[31],0)));
        if(next){ctx->pc=next;return true;}
    }
    if(!guestHudDrawBoundary(ctx->pc) || !presented)return false;
    {std::lock_guard lock(store.mutex);
        if(!store.state.active || store.state.generation!=presented || readWord(ram+0x2FEB14)!=store.state.manager)return false;
        // Original game-style split panels remain when the game HUD is enabled.
        if(ctx->pc==0x072D1800 && store.state.showGameHud)return false;
        if(ctx->pc==0x073DA200 || ctx->pc==0x07272000){
            bool projected=false;for(const auto& v:store.state.views)if(v.valid)for(const auto& p:v.points)projected|=p.valid;
            if(!projected)return false; // no replacement coverage: retain guest bars
        }
    }
    const auto next=replacementHudDrawPc({ram,PS2_RAM_SIZE},ctx->pc,uint32_t(_mm_extract_epi32(ctx->r[31],0)));
    if(!next)return false;ctx->pc=next;return true;
}
bool ps2xTagteamRunNativeTrainingDamage(uint8_t *ram,R5900Context *ctx,PS2Runtime *rt) {
    static const bool nativeUi=std::getenv("PS2X_NATIVE_UI_SLICE")!=nullptr;
    if(!nativeUi || trainingDamageActive || ctx->pc!=0x1CE630u ||
       !ps2x::ui::hudStore().trainingCounterEnabled.load(std::memory_order_relaxed) ||
       !ps2x::ui::trainingDamageHook({ram,PS2_RAM_SIZE}))return false;
    const auto screen=ps2x::ui::uiStore().lifecycle.snapshot(ps2x::ui::uiNow());
    if(screen.phase!=ps2x::ui::PreparationPhase::Released ||
       (screen.mode!=ps2x::ui::BattleMode::Training && screen.mode!=ps2x::ui::BattleMode::TrainingCoop))return false;
    nativeTrainingDamage(ram,ctx,rt);return true;
}
bool ps2xTagteamLifecycleDispatch(uint32_t pc) {
    // These lookups can change as the controller arms/disarms ownership.
    return rematchEnabled && (pc == 0x12B570u || pc == 0x12B650u);
}
void ps2xTagteamStart(PS2Runtime &rt) {
    detachedCleanupReceipt=false;detachedCleanupManager=0;
#if defined(_WIN32)
    const char *env=std::getenv("PS2X_TAGTEAM_PORT");if(!env)return;
    int port=std::atoi(env);if(port<1024 || port>65535)return;
    runtime=&rt;enabled=true;stopping=false;
    const char *rematch = std::getenv("PS2X_NATIVE_REMATCH");
    rematchEnabled = rematch && rematch[0] == '1';
    recurringBootWords.clear();
    if(const char *boot=std::getenv("PS2X_TAGTEAM_BOOT")) {
        std::ifstream file(boot);std::string line;uint32_t at,value;uint8_t *ram=rt.memory().getRDRAM();uint32_t words=0;
        while(std::getline(file,line))if(std::sscanf(line.c_str(),"patch=1,EE,%x,word,%x",&at,&value)==2) {
            if(at>PS2_RAM_SIZE-4 || (at&3)) {std::fprintf(stderr,"[tagteam] invalid bootstrap address\n");rt.requestStop();return;}
            mark(at,4);std::memcpy(ram+at,&value,4);++words;
            // Power Scale reinstalls its conflict predicate when its module
            // entry runs. These two immutable select-screen cheat words must
            // win as they do under PCSX2's per-frame cheat application. Do not
            // replay other bootstrap words: the controller changes them live.
            if ((at == 0x261650u && value == 0x03E00008u) ||
                (at == 0x261654u && value == 0x0000102Du))
                recurringBootWords.push_back({at, value});
        }
        if(!words){std::fprintf(stderr,"[tagteam] missing bootstrap\n");rt.requestStop();return;}
        std::fprintf(stderr,"[tagteam] installed %u bootstrap words\n",words);
    }
    worker=std::thread(serve,port);
#endif
}

#include "ps2_cpu_tactics_diagnostics.inc"
void ps2xTagteamStop() {
    nativeSeatPads.reset();
    if(runtime)cpuTacticsDiagnostics(runtime->memory().getRDRAM(),0,true);
    stopping=true;
#if defined(_WIN32)
    SOCKET c=client.load();if(c!=INVALID_SOCKET)shutdown(c,SD_BOTH);
#endif
    if(worker.joinable())worker.join();enabled=false;runtime=nullptr;
}
void ps2xTagteamFrame(uint8_t *ram, R5900Context *ctx, PS2Runtime *rt) {
    if (!enabled.load(std::memory_order_relaxed)) return;
    static const bool nativePads=[](){const char* v=std::getenv("PS2X_NATIVE_SEAT_PADS");return v && std::strcmp(v,"1")==0;}();
    if(nativePads)nativeSeatPads.frame({ram,PS2_RAM_SIZE},[](unsigned player){
        const auto p=ps2_stubs::PadConfig::instance().poll(player);
        return ps2_native_seats::Packet{p.buttons,p.lx,p.ly,p.rx,p.ry};
    });
    if(rematchEnabled && readWord(ram+rematchControl)==rematchMagic && readWord(ram+rematchControl+4)==8) {
        uint32_t error=0;
        const bool cleaned=ps2TrainingCleanup({ram,PS2_RAM_SIZE},detachedCleanupManager,detachedCleanupReceipt,error);
        std::memcpy(ram+rematchControl+28,&error,4);
        if(cleaned)detachedCleanupReceipt=false;
        const uint32_t result=9;std::memcpy(ram+rematchControl+4,&result,4);
        std::fprintf(stderr,"[native-rematch] fresh-match arena cleanup %s error=%u previous_manager=0x%x\n",cleaned?"verified":"refused",error,detachedCleanupManager);
    }
    static const bool nativeHud=std::getenv("PS2X_NATIVE_UI_SLICE")!=nullptr;
    if(nativeHud) {
        auto state=ps2x::ui::uiStore().lifecycle.snapshot(ps2x::ui::uiNow());
        cpuTacticsDiagnostics(ram,state.generation);
        static ps2x::ui::FightIntro intro;
        // No intro memory reads or polling at boot/menus. The prepared Ready
        // lifecycle owns this transaction; all other phases keep it empty.
        if(state.phase==ps2x::ui::PreparationPhase::Ready && state.generation){
            auto u=[&](uint32_t at){return readWord(ram+at);};
            const uint32_t manager=u(0x2FEB14),count=u(0xD8084),gate=0x073E1C00;
            bool introOwned=
                manager>=0x100000 && manager<PS2_RAM_SIZE-32 && count>=2 && count<=10 && !(count&1) &&
                u(0xD8080)==1 && u(0xD8088)==manager && u(0xD808C)==count &&
                u(gate)==1 && u(gate+4)==2 && u(gate+8)==manager && u(gate+12)==count && u(0x07361850)==1 && u(0xC4004)==0;
            const uint32_t phase=u(0x2FEB38);
            introOwned=introOwned && phase>=0x100000 && phase<PS2_RAM_SIZE-264 && u(phase)==3;
            for(unsigned i=0;introOwned && i<count;++i){const uint32_t actor=u(0xD8040+4*i);
                introOwned=actor>=0x100000 && actor<PS2_RAM_SIZE-0x1600 && actor==u(gate+0x80+4*i) && u(actor)==i;
            }
            if(introOwned){auto& hs=ps2x::ui::hudStore();
                const bool composed=hs.presentedGeneration.load()==state.generation && hs.presentedIntroStage.load()==intro.stage;
                const unsigned previousStage=intro.stage;
                const bool release=intro.advance(state.generation,manager,ps2x::ui::uiNow(),(u(0x331DC8+0x19F0)&0x100)!=0,composed);
                if(previousStage!=intro.stage)std::fprintf(stderr,"[native-intro] stage=%u generation=%llu manager=0x%x\n",intro.stage,(unsigned long long)state.generation,manager);
                const uint32_t stage=intro.stage;std::memcpy(ram+gate+24,&stage,4);
                if(release){const uint32_t request=1;std::memcpy(ram+gate+4,&request,4);
                    std::fprintf(stderr,"[native-intro] Ready/FIGHT finished; requesting guarded actor/input release generation=%llu\n",(unsigned long long)state.generation);}
            }else intro={};
        }else intro={};
        auto captured=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},state);
        static ps2x::ui::ScenePacketWatch sceneWatch,cinematicWatch;
        bool transition=captured.cinematic;
        for(const auto& actor:captured.actors)transition|=actor.present && (actor.cinematic || actor.transforming);
        const bool watchEligible=ps2x_pgs::enabled() && captured.active && !captured.paused && !transition &&
            readWord(ram+captured.manager+628)==0 && readWord(ram+0xC4004)==0;
        if(sceneWatch.sample(state.generation,ps2x::ui::uiNow(),ps2x_pgs::publishedPacketCount(),watchEligible)) {
            std::fprintf(stderr,"[scene-watch] fight GS packets below8000/s for2s after healthy rendering; capturing without stopping guest\n");
            ps2xCaptureGuestExit(rt,ram,ctx,"fight-gs-packet-collapse",ctx->pc,uint32_t(_mm_extract_epi32(ctx->r[31],0)));
        }
        // The reported black scene retains cinematic=1. Do not let that
        // potentially stale flag disable diagnostics forever. Longer grace
        // distinguishes this suspected cinematic stall from the ordinary case.
        const bool cinematicEligible=ps2x_pgs::enabled() && captured.active && !captured.paused &&
            readWord(ram+captured.manager+628)==0 && readWord(ram+0xC4004)==0;
        if(cinematicWatch.sample(state.generation,ps2x::ui::uiNow(),ps2x_pgs::publishedPacketCount(),cinematicEligible,10000) && transition) {
            std::fprintf(stderr,"[scene-watch] GS collapse persists10s with cinematic flags; capturing suspected stall without stopping guest\n");
            ps2xCaptureGuestExit(rt,ram,ctx,"cinematic-gs-packet-collapse",ctx->pc,uint32_t(_mm_extract_epi32(ctx->r[31],0)));
        }
        static ps2x::ui::TeamDefeatWatch defeatWatch;
        unsigned live[2]={};for(const auto& actor:captured.actors)if(actor.present && actor.alive)++live[&actor-&captured.actors[0]&1];
        const unsigned defeated=(!live[0]?1u:0u)|(!live[1]?2u:0u);
        const bool teamMatch=state.mode==ps2x::ui::BattleMode::Teams || state.mode==ps2x::ui::BattleMode::Coop;
        if(defeatWatch.sample(state.generation,ps2x::ui::uiNow(),defeated,watchEligible && teamMatch)) {
            std::fprintf(stderr,"[defeat-watch] team dead/absent for5s in active battle; capturing inputs without changing win state\n");
            ps2xCaptureGuestExit(rt,ram,ctx,"team-defeat-not-resolved",ctx->pc,uint32_t(_mm_extract_epi32(ctx->r[31],0)));
        }
        const auto projectionStart=std::chrono::steady_clock::now();
        if(!captured.paused)nativeOverheadProjection(ram,ctx,rt,captured);
        static uint64_t lastLog=0;static double projectionTotal=0,projectionMax=0;static unsigned samples=0;
        const double projectionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-projectionStart).count();
        projectionTotal+=projectionMs;projectionMax=(std::max)(projectionMax,projectionMs);++samples;
        const auto now=ps2x::ui::uiNow();
        if(now-lastLog>=1000){
            const auto c=ps2x::ui::overheadCounts(captured);
            std::fprintf(stderr,"[overhead] published views=%u detailed=%u simple=%u active=%u lifecycle=%u projection_mean_ms=%.4f projection_max_ms=%.4f\n",captured.viewCount,c.detailed,c.simple,unsigned(captured.active),unsigned(state.phase),projectionTotal/samples,projectionMax);
            std::fprintf(stderr,"[overhead] simple=%u explanation: filtered=%u offscreen=%u invalid_projection=%u absent_or_dead=%u friends=%u enemies=%u cinematic=%u\n",c.simple,c.filtered,c.offscreen,c.invalid,c.absentOrDead,unsigned(captured.preferences.friends),unsigned(captured.preferences.enemies),unsigned(captured.cinematic));
            const unsigned suppressed=hudStatusSuppressed.exchange(0);
            const unsigned retained=hudPromptsRetained.exchange(0);
            if(captured.active && !captured.showGameHud)std::fprintf(stderr,"[native-hud] requested=off suppressed_status_roots=%u retained_prompt_nodes=%u generation=%llu\n",suppressed,retained,(unsigned long long)captured.generation);
            lastLog=now;projectionTotal=projectionMax=0;samples=0;
        }
        auto& store=ps2x::ui::hudStore();std::lock_guard lock(store.mutex);
        store.trainingCounterEnabled.store(captured.training && captured.showCounters,std::memory_order_relaxed);
        if(store.counterGeneration!=state.generation){store.counterGeneration=state.generation;store.hits=store.damage=0;}
        const bool resetDown=(readWord(ram+0x333950)&1)!=0;
        if(captured.training && resetDown && !store.resetDown)store.hits=store.damage=0;
        store.resetDown=resetDown;
        captured.hits=store.hits;captured.damage=store.damage;store.state=captured;
    }
    for (const auto &word : recurringBootWords) {
        if (readWord(ram + word.address) != word.value) {
            mark(word.address, 4);
            std::memcpy(ram + word.address, &word.value, 4);
        }
    }
    if (readWord(ram+0x0768f000)!=0x42545032) return;
    R5900Context service=*ctx;
    service.pc=0x07680000;
    service.r[31]=_mm_set_epi32(0,0,0,0x0ffffffc);
    if (!rt->interpretUntil(ram,&service,0x0ffffffc)) {
        std::fprintf(stderr,"[tagteam] preparation service failed at 0x%08x\n",service.pc);
        rt->requestStop();
    }
}

// Execute the actual generated revert tail and captured production guard.
// No window, PINE server, disc, renderer or saved-state load is involved.
#include "ps2_fusion_controls_self_test.inc"
#include "ps2_v11_prompt_self_test.inc"
#include "ps2_fusion_form_self_test.inc"
#include "ps2_cpu_transform_self_test.inc"
#include "ps2_fusion_input_self_test.inc"
int ps2xEventTailSelfTest(const char* capture){
    extern void FUN_001296b8_0x1296b8(uint8_t*,R5900Context*,PS2Runtime*);
    extern void FUN_001fe358_0x1fe358(uint8_t*,R5900Context*,PS2Runtime*);
    extern void sub_001FDF50_0x1fdf50(uint8_t*,R5900Context*,PS2Runtime*);
    extern void FUN_001dafc0_0x1dafc0(uint8_t*,R5900Context*,PS2Runtime*);
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto put=[&](uint32_t at,uint64_t v){std::memcpy(ram+at,&v,8);};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    constexpr uint32_t scene=0x3337B8,stack=0x01FFE000,done=0x0FFFFFFC;
    const uint32_t actor=readWord(ram+0xD8048);
    if(readWord(ram+0x1296B8)!=(0x08000000u|(0x07382000u>>2)) ||
       readWord(ram+0xD8080)!=1 || readWord(ram+actor)!=2)return 3;
    auto ctx=[&](){R5900Context c{};reg(c,28,0x304270);reg(c,29,stack);reg(c,31,done);return c;};
    // Reproduce the last live write without the installed entry guard.
    enabled=true;put(scene,0);auto unsafe=ctx();unsafe.pc=0x1296B8;
    reg(unsafe,4,2);reg(unsafe,5,77);FUN_001296b8_0x1296b8(ram,&unsafe,&rt);
    uint64_t value=0;std::memcpy(&value,ram+scene,8);
    if(value!=0x2000 || rt.isStopRequested())return 4;
    std::puts("PASS: unguarded production event77/index2 reproduces exact scene0x2000 write");
    mark(0x1296B8,8);
    const PS2Runtime::RecompiledFunction tails[]={FUN_001fe358_0x1fe358,
        sub_001FDF50_0x1fdf50,FUN_001dafc0_0x1dafc0};
    for(unsigned t=0;t<3;++t)for(uint32_t physical:{0u,1u,2u,3u,9u}){
        const uint32_t event=t==2?60:77;
        // Seed other flags to prove that the fix does not repair by clearing.
        put(scene,0x100);put(0x333750,0);put(0x333758,0);put(0x333780,0);put(0x333788,0);
        std::memcpy(ram+actor,&physical,4);
        std::memset(ram+stack,0,64);put(stack+(t==2?8:24),done);
        auto c=ctx();c.pc=t==2?0x1DB020:0x1FE5E4;reg(c,t==2?16:17,actor);
        tails[t](ram,&c,&rt);
        if(c.pc!=done || rt.isStopRequested())return 5;
        std::memcpy(&value,ram+scene,8);if(value!=0x100)return 6;
        const uint32_t at=0x333748+physical*48+8+(event/64)*8;
        if(physical<2){std::memcpy(&value,ram+at,8);if(!(value&(1ull<<(event%64))))return 7;}
    }
    enabled=false;
    std::puts("PASS: three generated event tails honor captured guard; stock0/1 events retained, extras2/3/9 cannot alter scene flags");
    return 0;
}

int ps2xOverheadHudSelfTest(const char* capture){
    using namespace ps2x::ui;PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto u=[&](uint32_t at){return readWord(ram+at);};
    auto put=[&](uint32_t at,uint32_t value){std::memcpy(ram+at,&value,4);};
    if(const char* code=std::getenv("PS2X_START_GATE_TEST_CODE")){
        std::ifstream input(code,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        if(bytes.empty() || bytes.size()>0xB00)return 30;
        std::memcpy(ram+0x073E1000,bytes.data(),bytes.size());
        const uint32_t manager=u(0x2FEB14),count=u(0xD8084),gate=0x073E1C00;
        if(count<2 || count>10)return 31;
        if(const char* quad=std::getenv("PS2X_QUAD_GATE_TEST_CODE")){
            std::ifstream q(quad,std::ios::binary);std::vector<uint8_t> qb((std::istreambuf_iterator<char>(q)),{});
            if(qb.empty() || qb.size()>0x1800)return 36;
            std::memcpy(ram+0x06C10000,qb.data(),qb.size());
            const uint32_t control=0x06C1F000,pads=0x06C14000,mailbox=0x06C15000;
            put(control,0x51494E31);put(control+4,manager);put(control+8,count);put(control+12,1);
            put(control+16,1);put(control+20,0);put(control+28,0);put(0x07361850,1);
            for(unsigned seat=0;seat<2;++seat){for(unsigned off:{304u,308u,312u,316u,328u})put(pads+448*seat+off,0x1234);
                for(unsigned field=0;field<5;++field)put(mailbox+seat*32+field*4,0x5678);}
            R5900Context test{};test.r[28]=_mm_set_epi32(0,0,0,0x304270);test.r[29]=_mm_set_epi32(0,0,0,0x1FFE000);test.pc=0x06C10000;
            if(!rt.interpretUntil(ram,&test,0x0ffffffc))return 37;
            for(unsigned seat=0;seat<2;++seat)for(unsigned off:{304u,308u,312u,316u,328u})if(u(pads+448*seat+off))return 38;
            put(0x07361850,0);test.pc=0x06C10000;
            if(!rt.interpretUntil(ram,&test,0x0ffffffc))return 39;
            for(unsigned seat=0;seat<2;++seat)for(unsigned off:{304u,308u,312u,316u,328u})if(u(pads+448*seat+off)!=0x5678)return 40;
            std::fprintf(stderr,"PASS actual interpreter quad pads: P3/P4 analog/buttons neutral during hold; fresh input restored after release\n");
        }
        for(unsigned request:{0u,2u,1u}){
            put(gate,1);put(gate+4,request);put(gate+8,manager);put(gate+12,count);put(gate+20,0);put(0x07361850,1);put(0xC4004,0);
            for(unsigned i=0;i<count;++i){const uint32_t actor=u(0xD8040+4*i);
                put(gate+0x80+4*i,actor);put(gate+0x40+4*i,i&1);put(actor+0x1278,0);
                for(unsigned off:{0x127Cu,0x1280u,0x1284u})put(actor+off,0x1234);
            }
            R5900Context test{};test.r[28]=_mm_set_epi32(0,0,0,0x304270);test.r[29]=_mm_set_epi32(0,0,0,0x1FFE000);test.pc=0x073E1000;
            if(!rt.interpretUntil(ram,&test,0x0ffffffc))return 32;
            if(u(gate)!=(request==1?0u:1u) || u(gate+20)!=(request==1?1u:0u) || u(0x07361850)!=(request==1?0u:1u))return 33;
            for(unsigned i=0;i<count;++i){const uint32_t actor=u(0xD8040+4*i);
                if(u(actor+0x1278)!=(request==1?(i&1):0u))return 34;
                for(unsigned off:{0x127Cu,0x1280u,0x1284u})if(u(actor+off))return 35;
            }
        }
        std::fprintf(stderr,"PASS actual interpreter start gate: requests0/2 hold every actor and input; request1 restores assignments and clears hold with ACK\n");return 0;
    }
    // Reproduce the released preparation gates only; actor/model/camera data
    // are the actual saved5v5. This does not advance a match or start PINE.
    put(0x073E1C00,0);put(u(0x2FEB38),3);
    ScreenSnapshot screen;screen.generation=1;screen.phase=PreparationPhase::Ready;
    screen.teamOneCount=screen.teamTwoCount=5;screen.hudOptions=6;
    R5900Context ctx{};ctx.r[28]=_mm_set_epi32(0,0,0,0x304270);ctx.r[29]=_mm_set_epi32(0,0,0,0x1FFE000);
    ctx.vu0_vf[0]=_mm_set_ps(1,0,0,0);const auto caller=ctx;
    std::array<uint8_t,4096> stack{};std::memcpy(stack.data(),ram+0x1FFD000,stack.size());
    auto snapshot=captureHud({ram,PS2_RAM_SIZE},screen);if(!snapshot.active)return 3;
    nativeOverheadProjection(ram,&ctx,&rt,snapshot);unsigned points=0;
    if(std::getenv("PS2X_COOP_FUSION_PROJECTION_CHECK")) {
        // Actual saved skeletons and native projection helpers, with a
        // controlled shared-view transition. No match simulation or PINE.
        const uint32_t base=u(0x304270-22172),quad=0x06C0F000,fusion=0x06BCA000;
        if(base<0x100000 || base>PS2_RAM_SIZE-3136)return 41;
        put(quad,0);put(0x331DEC,0);put(0x073AF00C,0);put(0x073AF108,0);
        put(0x304270-22176,base+1824);
        put(0x077CF000,5);put(0x077CF004,snapshot.manager);put(0x077CF008,u(0xD8084));put(0x077CF010,4);
        const auto actor=u(0xD8040),oldMid=u(actor+12);
        auto seed=captureHud({ram,PS2_RAM_SIZE},screen);nativeOverheadProjection(ram,&ctx,&rt,seed);
        std::vector<uint32_t> mids;
        for(unsigned i=0;i<10 && mids.size()<2;++i)if(seed.views[0].points[i].valid)mids.push_back(u(seed.actors[i].pointer+12));
        if(mids.size()!=2)return 45;
        // Both a fused body and a restored body must resolve the current model.
        for(uint32_t mid:mids) {
            put(actor+12,mid);
            auto reference=captureHud({ram,PS2_RAM_SIZE},screen);
            nativeOverheadProjection(ram,&ctx,&rt,reference);
            put(quad,0x51564131);put(quad+4,snapshot.manager);put(quad+36,2);put(quad+64,0);put(quad+68,0);
            put(fusion,0x4D465531);put(fusion+4,snapshot.manager);put(fusion+8,u(0xD8084));
            put(fusion+0x100,3);put(fusion+0x124,0);put(fusion+0x128,1);
            auto fused=captureHud({ram,PS2_RAM_SIZE},screen);nativeOverheadProjection(ram,&ctx,&rt,fused);
            if(fused.viewCount!=1 || fused.actors[2].present || !fused.views[0].points[0].valid){
                std::fprintf(stderr,"[fusion-fixture] mid=%u active=%u views=%u actor2=%u point0=%u reference0=%u camera=0x%x rect=%u,%u,%u,%u\n",mid,unsigned(fused.active),fused.viewCount,unsigned(fused.actors[2].present),unsigned(fused.views[0].points[0].valid),unsigned(reference.views[0].points[0].valid),u(0x304270-22176),u(base+1824+512),u(base+1824+516),u(base+1824+520),u(base+1824+524));return 42;}
            for(unsigned i=0;i<10;++i){const auto& actual=fused.views[0].points[i];const auto& expected=reference.views[0].points[i];
                if(actual.valid!=expected.valid || (actual.valid && (std::abs(actual.x-expected.x)>.01f || std::abs(actual.y-expected.y)>.01f)))return 43;
            }
            put(quad,0);put(fusion,0);
        }
        put(actor+12,oldMid);put(0x077CF010,0);auto restored=captureHud({ram,PS2_RAM_SIZE},screen);
        if(!restored.actors[2].present || std::memcmp(&caller,&ctx,sizeof(ctx)) || std::memcmp(stack.data(),ram+0x1FFD000,stack.size()))return 44;
        std::fprintf(stderr,"PASS actual model replacement/revert projection: shared fused view matches native current camera within0.01pixel; consumed partner hidden/restored; caller/stack unchanged\n");return 0;
    }
    for(const auto& view:snapshot.views)if(view.valid)for(const auto& point:view.points)points+=point.valid;
    if(const char* disabled=std::getenv("PS2X_NATIVE_OVERHEAD_HUD");disabled && std::strcmp(disabled,"0")==0){
        if(points || snapshot.viewCount || std::memcmp(&caller,&ctx,sizeof(ctx)) || std::memcmp(stack.data(),ram+0x1FFD000,stack.size()))return 5;
        std::fprintf(stderr,"PASS overhead disabled: zero views/points, caller and4096-byte stack preserved\n");return 0;
    }
    if(!points || !snapshot.viewCount || std::memcmp(&caller,&ctx,sizeof(ctx)) || std::memcmp(stack.data(),ram+0x1FFD000,stack.size()))return 4;
    unsigned retained=10;
    for(const auto& view:snapshot.views)if(view.valid)for(unsigned i=0;i<10;++i)
        if(view.points[i].valid && snapshot.actors[i].present && snapshot.actors[i].alive)retained=i;
    if(retained==10)return 45;
    auto corpse=snapshot;corpse.actors[retained].alive=false;corpse.views={};
    corpse.revives[0]={uint8_t(retained^2),uint8_t(retained),1,2,.5f,.5f,0,true};
    nativeOverheadProjection(ram,&ctx,&rt,corpse);
    bool corpseProjected=false;for(const auto& view:corpse.views)if(view.valid)corpseProjected|=view.points[retained].valid;
    if(!corpseProjected || std::memcmp(&caller,&ctx,sizeof(ctx)) || std::memcmp(stack.data(),ram+0x1FFD000,stack.size()))return 45;
    std::fprintf(stderr,"PASS actual retained-corpse revival anchor projection; caller and stack unchanged\n");
    double total=0,maximum=0;
    for(unsigned i=0;i<120;++i){auto h=captureHud({ram,PS2_RAM_SIZE},screen);auto start=std::chrono::steady_clock::now();
        nativeOverheadProjection(ram,&ctx,&rt,h);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;maximum=(std::max)(maximum,ms);}
    std::fprintf(stderr,"PASS actual saved5v5 projection: views=%u points=%u caller and4096-byte stack preserved; mean=%.4fms max=%.4fms\n",snapshot.viewCount,points,total/120,maximum);
    if(std::getenv("PS2X_NATIVE_UI_SLICE")){
        auto& store=hudStore();{std::lock_guard lock(store.mutex);store.state=snapshot;}
        put(0x0726F01C,0); // drawing must work independently of update scope
        const uint32_t root=u(0x304270-22324);
        for(unsigned offset:{0u,8u}){
            R5900Context draw=caller;draw.pc=0x2188B8;
            draw.r[4]=_mm_set_epi32(0,0,0,int(u(root+offset)));
            draw.r[31]=_mm_set_epi32(0,0,0,0x123456);
            auto expected=draw;expected.pc=0x123456;
            nativeStatusDraw(ram,&draw,&rt);
            if(std::memcmp(&draw,&expected,sizeof(draw)))return 6;
        }
        R5900Context other=caller;other.pc=0x2188B8;other.r[4]=_mm_set_epi32(0,0,0,int(u(root+4)));
        if(ps2xTagteamRunNativeHudDraw(ram,&other,nullptr))return 7;
        std::fprintf(stderr,"PASS actual saved HUD: native render adapter suppressed both status roots with update scope0; other prompt root retained\n");
    }
    return 0;
}

int ps2xNativeSeatPadsSelfTest(){return ps2_native_seats::selfTest();}
