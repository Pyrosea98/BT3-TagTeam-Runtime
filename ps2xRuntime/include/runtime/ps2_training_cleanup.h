#pragma once
#include <span>
#include <cstring>
#include <algorithm>
#include <cstdint>

// Reset the detached expanded arena before fresh preparation, using the same
// range as nativeMatchBoundary. Its runtime-only receipt proves prior teardown;
// native two-actor storage and empty heap bounds exclude live expanded actors.
inline uint32_t ps2TrainingCleanupCheck(std::span<const uint8_t> ram,uint32_t previousManager,bool detachedReceipt) {
    if(!detachedReceipt || ram.size()!=0x08000000)return 1;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;};
    const auto manager=u(0x2FEB14);
    if(manager<0x100000 || manager>=0x02000000-32 || u(manager)!=2 || u(0xD8080)!=0 ||
       u(0x2FF084) || u(0x2FF08C) || u(0x07FFF10C)!=previousManager)return 2;
    const auto actors=u(manager+4);
    if(actors<0x100000 || actors>0x02000000-2*0x1600 ||
       u(actors)!=0 || u(actors+0x1600)!=1)return 3;
    return 0;
}
inline bool ps2TrainingCleanup(std::span<uint8_t> ram,uint32_t previousManager,bool detachedReceipt,uint32_t& error) {
    error=ps2TrainingCleanupCheck(ram,previousManager,detachedReceipt);
    if(error)return false;
    std::memset(ram.data()+0x02000000,0,0x04000000);
    return true;
}
