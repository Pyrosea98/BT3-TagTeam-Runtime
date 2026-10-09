#include "runtime/ps2_gif_arbiter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <tuple>
#include <algorithm>

namespace { thread_local uint32_t packetOwner=0,packetSource=UINT32_MAX; }
uint32_t ps2xHudPacketOwner() { return packetOwner; }
uint32_t ps2xHudPacketSource() { return packetSource; }

// Observes the ordered stream before renderer rewrites. IMAGE continuations are
// retained across packets. Unknown copied RAM sources remain explicitly unknown.
void ps2xHudObservePacket(const GifArbiterPacket &packet) {
    packetOwner=packet.owner; packetSource=packet.eeSource;
    static const char *directory=std::getenv("PS2X_HUD_UPLOAD_TRACE");
    if(!directory||!*directory||*directory=='0'||packet.pathId==GifPathId::HostDraw) return;
    struct Walk { uint32_t remaining=0,reg=0,nreg=0; unsigned mode=0; uint64_t regs=0; };
    static Walk paths[4];
    static uint64_t bitblt=0,trx=0;
    static unsigned records=0;
    static FILE *index=nullptr;
    static std::set<std::tuple<uint32_t,uint32_t,uint64_t>> seen;
    if(records>=256) return;
    unsigned path=unsigned(packet.pathId);
    if(path>3||!packet.data) return;
    auto &walk=paths[path];
    unsigned offset=0;
    while(offset+16<=packet.size) {
        if(records>=256) return;
        uint64_t lo,hi;
        std::memcpy(&lo,packet.data+offset,8); std::memcpy(&hi,packet.data+offset+8,8);
        if(!walk.remaining) {
            walk.mode=(lo>>58)&3; walk.nreg=(lo>>60)&15; if(!walk.nreg) walk.nreg=16;
            walk.regs=hi; walk.reg=0;
            const unsigned loops=lo&0x7fff;
            walk.remaining=walk.mode==0?loops*walk.nreg:walk.mode==1?(loops*walk.nreg+1)/2:loops;
            offset+=16; continue;
        }
        if(walk.mode>=2) {
            const unsigned bytes=std::min(walk.remaining*16,((packet.size-offset)/16)*16);
            const unsigned dbp=(bitblt>>32)&0x3fff;
            if(dbp==10752||dbp==10880||(dbp>=11392&&dbp<=11424)) {
                const unsigned length=std::min(bytes,65536u);
                uint64_t hash=14695981039346656037ull;
                for(unsigned i=0;i<length;++i) hash=(hash^packet.data[offset+i])*1099511628211ull;
                auto key=std::make_tuple(dbp,length,hash);
                if(seen.insert(key).second) {
                    std::error_code ec; std::filesystem::create_directories(directory,ec);
                    if(ec) return;
                    if(!index) index=std::fopen((std::filesystem::path(directory)/"uploads.jsonl").string().c_str(),"wb");
                    if(!index) return;
                    char name[64]; std::snprintf(name,sizeof name,"upload-%03u-dbp%u.bin",records,dbp);
                    FILE *body=std::fopen((std::filesystem::path(directory)/name).string().c_str(),"wb");
                    if(body) { std::fwrite(packet.data+offset,1,length,body); std::fclose(body); }
                    char source[32];
                    if(packet.eeSource==UINT32_MAX) std::strcpy(source,"null");
                    else std::snprintf(source,sizeof source,"%llu",(unsigned long long)packet.eeSource+offset);
                    std::fprintf(index,"{\"file\":\"%s\",\"path\":%u,\"dbp\":%u,\"dpsm\":%u,\"dbw\":%u,\"width\":%u,\"height\":%u,\"packet_offset\":%u,\"chunk_bytes\":%u,\"saved_bytes\":%u,\"ee_source\":%s,\"builder_ra\":%u,\"fnv1a64\":\"%016llx\"}\n",name,path,dbp,unsigned((bitblt>>56)&63),unsigned((bitblt>>48)&63),unsigned(trx&4095),unsigned((trx>>32)&4095),offset,bytes,length,source,packet.owner,(unsigned long long)hash);
                    std::fflush(index);
                    std::fprintf(stderr,"[hudupload] #%u path=%u dbp=%u bytes=%u EE=%s builder_ra=0x%08x\n",records,path,dbp,length,source,packet.owner);
                    ++records;
                }
            }
            walk.remaining-=bytes/16; offset+=bytes; continue;
        }
        if(walk.mode==0) {
            if(((walk.regs>>((walk.reg%walk.nreg)*4))&15)==14) {
                if((hi&255)==0x50) bitblt=lo;
                if((hi&255)==0x52) trx=lo;
            }
            ++walk.reg;
        }
        --walk.remaining; offset+=16;
    }
}
