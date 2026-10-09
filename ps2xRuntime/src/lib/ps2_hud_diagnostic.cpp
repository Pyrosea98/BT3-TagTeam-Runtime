// Developer-only, bounded GS state observer. No guest/VRAM writes or renderer calls.
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_rasterizer.h"
#include "ps2_syscalls.h"
#include "../gfx/image_io.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <tuple>
namespace {
FILE *hudTraceFile=nullptr, *hudIndexFile=nullptr;
GS *diagnosticGS=nullptr;
unsigned diagnosticGeneration=0;
}
void ps2xHudDiagnosticSetGS(GS *gs) { diagnosticGS=gs; }
GS *ps2xHudDiagnosticGS() { return diagnosticGS; }
unsigned ps2xHudDiagnosticGeneration() { return diagnosticGeneration; }
extern uint32_t ps2xHudPacketOwner();
extern uint32_t ps2xHudPacketSource();

bool ps2xHudDiagnosticEnabled() {
    static const bool enabled=[] {
        const char *dump=std::getenv("PS2X_DUMP_TEX");
        const char *trace=std::getenv("PS2X_DRAW_TRACE");
        return (dump&&*dump&&*dump!='0')||(trace&&*trace&&*trace!='0');
    }();
    return enabled;
}

// Called by the arbiter, not by every vertex. Outside the capture window the
// exclusive Vulkan path skips the shadow GS parser entirely.
bool ps2xHudDiagnosticActive() {
    if (!ps2xHudDiagnosticEnabled()) return false;
    static bool armed=false, automaticConsumed=false;
    static uint64_t start=0;
    static auto nextPoll=std::chrono::steady_clock::time_point{};
    const uint64_t tick=ps2_syscalls::GetCurrentVSyncTick();
    if(armed) {
        if(tick-start<60) return true;
        armed=false;
        if(hudTraceFile) std::fflush(hudTraceFile);
        if(hudIndexFile) std::fflush(hudIndexFile);
        std::fprintf(stderr,"[huddiag] capture %u window closed; exclusive Vulkan bypass restored; ready for next trigger\n",diagnosticGeneration);
        return false;
    }
    const auto now=std::chrono::steady_clock::now();
    if(now<nextPoll) return false;
    nextPoll=now+std::chrono::milliseconds(100);
    static const char *trigger=std::getenv("PS2X_GS_DIAG_TRIGGER");
    std::error_code ec;
    if(trigger&&*trigger) {
        if(!std::filesystem::exists(trigger,ec)) return false;
        if(!std::filesystem::remove(trigger,ec)) {
            std::fprintf(stderr,"[huddiag] trigger consumption failed: %s\n",ec.message().c_str()); return false;
        }
    } else {
        if(automaticConsumed) return false;
        automaticConsumed=true;
    }
    armed=true; start=tick; ++diagnosticGeneration;
    std::fprintf(stderr,"[huddiag] capture %u armed at tick=%llu; trigger consumed\n",diagnosticGeneration,(unsigned long long)tick);
    return true;
}

void ps2xHudDiagnosticKick(GS &gs, const GSContext &ctx, const GSPrimReg &prim,
                           const GSVertex *v, unsigned count, unsigned path) {
    if (!ps2xHudDiagnosticActive() || !count) return;
    struct Capture {
        std::filesystem::path dir;
        FILE *trace=nullptr, *index=nullptr;
        uint64_t start=0;
        unsigned rows=0, textures=0;
        bool started=false, done=false;
        unsigned generation=0;
        std::set<std::string> seen;
        std::map<std::tuple<unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned>,unsigned> drawsPerIdentity;
        ~Capture() { if(trace) std::fclose(trace); if(index) std::fclose(index); }
    };
    static Capture capture;
    if(capture.generation!=diagnosticGeneration) {
        if(capture.trace) std::fclose(capture.trace);
        if(capture.index) std::fclose(capture.index);
        capture.trace=nullptr; capture.index=nullptr;
        hudTraceFile=nullptr; hudIndexFile=nullptr;
        capture.started=false; capture.done=false;
        capture.rows=0; capture.textures=0;
        capture.seen.clear(); capture.drawsPerIdentity.clear();
        capture.generation=diagnosticGeneration;
    }
    if (capture.done) return;
    const uint64_t frame=ps2_syscalls::GetCurrentVSyncTick();
    if (!capture.started) {
        std::error_code ec;
        const char *dump=std::getenv("PS2X_DUMP_TEX");
        capture.dir=(dump&&*dump&&*dump!='0')?dump:"hud-diagnostic";
        capture.dir/=std::string("capture-")+std::to_string(capture.generation);
        std::filesystem::create_directories(capture.dir,ec);
        if(ec) { capture.done=true; std::fprintf(stderr,"[huddiag] directory failed: %s\n",ec.message().c_str()); return; }
        capture.trace=std::fopen((capture.dir/"draws.jsonl").string().c_str(),"wb");
        capture.index=std::fopen((capture.dir/"textures.jsonl").string().c_str(),"wb");
        if(!capture.trace||!capture.index) { capture.done=true; std::fprintf(stderr,"[huddiag] capture %u cannot open output files in %s\n",capture.generation,capture.dir.string().c_str()); return; }
        hudTraceFile=capture.trace; hudIndexFile=capture.index;
        capture.start=frame; capture.started=true;
        std::fprintf(stderr,"[huddiag] capture started frame=%llu (60 vsync ticks, 32 draws/identity, 32768 rows, 128 textures max)\n",(unsigned long long)frame);
    }
    if(frame-capture.start>=60||capture.rows>=32768) {
        capture.done=true; std::fflush(capture.trace); std::fflush(capture.index);
        std::fprintf(stderr,"[huddiag] capture complete: draws=%u textures=%u\n",capture.rows,capture.textures);
        return;
    }
    // Sprite/line draws are HUD candidates, not a proven HUD pass. Keep all
    // primitive kinds in the trace so a HUD built from triangles is not omitted.
    const auto &t=ctx.tex0;
    auto identity=std::make_tuple(t.tbp0,unsigned(t.psm),t.cbp,unsigned(t.cpsm),unsigned(t.csa),unsigned(t.tw),unsigned(t.th));
    // A foliage burst cannot consume the total cap before another CLUT's HUD.
    if(capture.drawsPerIdentity.size()>=1024&&!capture.drawsPerIdentity.contains(identity)) return;
    if(capture.drawsPerIdentity[identity]++>=32) return;
    std::fprintf(capture.trace,"{\"frame\":%llu,\"path\":%u,\"primitive\":%u,\"textured\":%u,\"fst\":%u,\"tbp\":%u,\"psm\":%u,\"clut\":%u,\"cpsm\":%u,\"csa\":%u,\"fbp\":%u,\"alpha_reg\":\"%016llx\",\"caller_pc\":null,\"vertices\":[",
        (unsigned long long)frame,path,unsigned(prim.type),unsigned(prim.tme),unsigned(prim.fst),t.tbp0,unsigned(t.psm),t.cbp,unsigned(t.cpsm),unsigned(t.csa),ctx.frame.fbp,(unsigned long long)ctx.alpha);
    for(unsigned i=0;i<count;++i) {
        std::fprintf(capture.trace,"%s{\"x\":%.3f,\"y\":%.3f,\"u\":%u,\"v\":%u,\"s\":%.6g,\"t\":%.6g,\"q\":%.6g,\"rgba\":[%u,%u,%u,%u]}",i?",":"",v[i].x-ctx.xyoffset.ofx/16.0f,v[i].y-ctx.xyoffset.ofy/16.0f,unsigned(v[i].u),unsigned(v[i].v),v[i].s,v[i].t,v[i].q,unsigned(v[i].r),unsigned(v[i].g),unsigned(v[i].b),unsigned(v[i].a));
    }
    char packetAddress[32];
    if(ps2xHudPacketSource()==UINT32_MAX) std::strcpy(packetAddress,"null");
    else std::snprintf(packetAddress,sizeof packetAddress,"%u",ps2xHudPacketSource());
    std::fprintf(capture.trace,"],\"builder_ra\":%u,\"last_packet_ee_source\":%s}\n",ps2xHudPacketOwner(),packetAddress); ++capture.rows;
    if((capture.rows%32)==0) std::fflush(capture.trace);
    const char *dump=std::getenv("PS2X_DUMP_TEX");
    if(!dump||!*dump||*dump=='0'||!prim.tme||capture.textures>=128) return;
    // Initial HUD discovery scope: indexed atlases only, <=512x512; the
    // runtime's existing deferred decoder handles their GS layout and CLUT.
    if((t.psm!=GS_PSM_T4&&t.psm!=GS_PSM_T8)||t.tw>9||t.th>9||!t.tbw||gs.vramSize()!=4*1024*1024) return;
    char key[128];
    std::snprintf(key,sizeof key,"tbp%u_psm%u_%ux%u_cbp%u_cpsm%u_csm%u_csa%u",t.tbp0,unsigned(t.psm),1u<<t.tw,1u<<t.th,t.cbp,unsigned(t.cpsm),unsigned(t.csm),unsigned(t.csa));
    if(!capture.seen.insert(key).second) return;
    // Own the decoder input so even decoder scratch behavior cannot alter GS VRAM.
    std::vector<uint8_t> snapshot(gs.vramData(),gs.vramData()+gs.vramSize()),rgba;
    TexDecodeReq req; req.tex0=t; req.texa=gs.texaReg(); req.texclut=gs.texclutReg();
    req.texW=1u<<t.tw; req.texH=1u<<t.th;
    int width=0; gs.rasterizer().decodeDeferred(req,snapshot.data(),snapshot.size(),width,rgba);
    if(width!=req.texW||rgba.size()!=size_t(width)*req.texH*4) return;
    const std::string file=std::string(key)+".png";
    if(!ps2x::gfx::GsWritePngRGBA8((capture.dir/file).string().c_str(),rgba.data(),width,req.texH)) return;
    ++capture.textures;
    std::fprintf(capture.index,"{\"file\":\"%s\",\"frame\":%llu,\"ee_address\":null,\"tbp\":%u,\"psm\":%u,\"width\":%d,\"height\":%d,\"clut\":%u,\"cpsm\":%u,\"csm\":%u,\"csa\":%u}\n",file.c_str(),(unsigned long long)frame,t.tbp0,unsigned(t.psm),width,req.texH,t.cbp,unsigned(t.cpsm),unsigned(t.csm),unsigned(t.csa));
    std::fflush(capture.index); std::fflush(capture.trace);
}
