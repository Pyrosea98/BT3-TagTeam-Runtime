#pragma once
#include <cstdint>
#include <algorithm>
namespace ps2x::ui {
// Progress only after the current banner has actually been composed. A pause,
// renderer stall or lost ownership never releases the prepared actor hold.
struct FightIntro {
    uint64_t generation=0,last=0;uint32_t manager=0,elapsed=0;unsigned stage=0;
    bool advance(uint64_t gen,uint32_t owner,uint64_t now,bool paused,bool presented) {
        if(generation!=gen || manager!=owner){generation=gen;manager=owner;stage=1;elapsed=0;last=now;return false;}
        const auto delta=now>=last?std::min<uint64_t>(now-last,250):0;last=now;
        if(paused || !presented)return false;
        elapsed+=uint32_t(delta);
        if(stage==1 && elapsed>=1500){stage=2;elapsed=0;}
        else if(stage==2 && elapsed>=700){stage=3;return true;}
        return false;
    }
};
}
