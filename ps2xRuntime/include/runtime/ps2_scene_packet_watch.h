#pragma once
#include <cstdint>

namespace ps2x::ui {
// Sample only at the guest frame boundary. No render lock or RAM writes.
struct ScenePacketWatch {
    uint64_t generation=0,at=0,packets=0,lowSince=0;
    bool healthy=false,fired=false;
    bool sample(uint64_t gen,uint64_t now,uint64_t total,bool eligible,uint64_t grace=2000) {
        if(gen!=generation){*this={};generation=gen;}
        if(!eligible){at=now;packets=total;lowSince=0;return false;}
        if(!at || now<at || total<packets){at=now;packets=total;lowSince=0;return false;}
        const auto elapsed=now-at;if(elapsed<500)return false;
        const double rate=double(total-packets)*1000./double(elapsed);
        at=now;packets=total;
        if(rate>=20000.){healthy=true;lowSince=0;return false;}
        if(!healthy || rate>=8000.){lowSince=0;return false;}
        if(!lowSince){lowSince=now;return false;}
        if(!fired && now-lowSince>=grace){fired=true;return true;}
        return false;
    }
};
struct TeamDefeatWatch {
    uint64_t generation=0,since=0,last=0;unsigned side=0;bool fired=false;
    bool sample(uint64_t gen,uint64_t now,unsigned defeated,bool eligible) {
        if(gen!=generation){*this={};generation=gen;}
        if(!eligible || !defeated || defeated!=side || now<last){since=now;side=eligible?defeated:0;last=now;return false;}
        last=now;if(!fired && now-since>=5000){fired=true;return true;}return false;
    }
};

}
