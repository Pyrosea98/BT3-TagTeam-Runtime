#pragma once
#include "ps2_ui_screen_lifecycle.h"
#include <atomic>
#include <cstring>
#include <chrono>
namespace ps2x::ui {
// Dedicated, versioned native UI message on the existing loopback bridge.
// No guest addresses, pointers, filenames or executable commands are accepted.
inline constexpr size_t UiPacketBytes = 2176;
struct UiMenu { std::array<char,2048> text{}; uint32_t selected=0, rows=0; };
struct UiStore {
    ScreenLifecycle lifecycle;
    std::mutex mutex;
    UiMenu menu;
    std::atomic<uint64_t> drawnRevision{0}, drawnGeneration{0};
    std::atomic<bool> rendererReady{false};
    std::atomic<bool> creditsActive{false}, creditsSkip{false};
};
inline UiStore& uiStore() { static UiStore store; return store; }
inline uint64_t uiNow() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline bool uiPacketValid(std::span<const uint8_t> b) {
    if (b.size()!=UiPacketBytes) return false;
    auto word=[&](size_t n) { uint32_t v; std::memcpy(&v,b.data()+n*4,4); return v; };
    if (word(0)!=1 || word(1)>11 || word(4)>100 || word(5)>7 || word(6)>5 || word(7)>5 ||
        word(18)>1 || word(19)>7 || word(20)>8) return false;
    if(word(21)>4 || word(23)>4 || word(24)>7 || (word(22)>>30))return false;
    if(word(25) && (!(word(25)&0x80000000u) || (word(25)&~0x800001ffu) ||
        (word(25)&3)>2 || word(26)<50 || word(26)>130 || word(27)<20 || word(27)>100))return false;
    if(word(28) && (!(word(28)&0x80000000u) || (word(28)&~0x80000103u)))return false;
    for(unsigned i=0;i<10;++i)if(((word(22)>>(3*i))&7)>4)return false;
    for (size_t i=8;i<18;++i) if(word(i)>252) return false;
    // Every fixed text slot must terminate inside its own boundary.
    if (!std::memchr(b.data()+128,0,128)) return false;
    for(size_t i=0;i<16;++i) if(!std::memchr(b.data()+256+i*96,0,96)) return false;
    if(!std::memchr(b.data()+1792,0,256) || !std::memchr(b.data()+2048,0,128)) return false;
    return true;
}
inline bool uiPublish(std::span<const uint8_t> b) {
    if(!uiPacketValid(b)) return false;
    auto word=[&](size_t n) { uint32_t v; std::memcpy(&v,b.data()+n*4,4); return v; };
    auto& s=uiStore(); auto& l=s.lifecycle;
    const uint64_t gen=word(2)|(uint64_t(word(3))<<32), now=uiNow();
    std::array<uint16_t,10> ids{}; for(size_t i=0;i<10;++i) ids[i]=uint16_t(word(8+i));
    const auto current=l.snapshot(now);
    bool ok=false;
    switch(word(1)) {
        case 0: ok=l.heartbeat(gen,now); break;
        case 1: ok=l.begin(gen,now,{ids.data(),word(6)},{ids.data()+word(6),word(7)}); break;
        case 2: ok=l.progress(gen,uint8_t(word(4)),uint8_t(word(5)),now); break;
        case 3: ok=(current.generation==gen && current.phase==PreparationPhase::Ready) || l.ready(gen,now); break;
        case 4: ok=(current.generation==gen && current.phase==PreparationPhase::Released && current.screen==Screen::Hidden) || l.released(gen,now); break;
        case 5: ok=(current.generation==gen && current.phase==PreparationPhase::Failed) || l.fail(gen,now); break;
        case 6: ok=l.teardown(gen,now); break;
        case 7: case 8: case 10:
            ok=l.open(word(1)==8?Screen::About:Screen::Settings,now); break;
        case 9: ok=l.close(now); break;
        case 11: ok=l.startAccepted(gen,now); break;
    }
    if(ok) {
        if(word(1)==1) {
            std::array<uint8_t,10> seats{};
            for(unsigned i=0;i<10;++i)seats[i]=uint8_t((word(22)>>(3*i))&7);
            HudPreferences hud;
            if(word(25)&0x80000000u){auto flags=word(25);hud.names=uint8_t(flags&3);hud.detail=uint8_t((flags>>2)&3);
                hud.portraits=(flags&16)!=0;hud.list=(flags&32)!=0;hud.fusion=(flags&64)!=0;
                hud.friends=(flags&128)!=0;hud.enemies=(flags&256)!=0;hud.scale=uint8_t(word(26));hud.opacity=uint8_t(word(27));}
            if(word(28)&0x80000000u){hud.shape=uint8_t(word(28)&3);hud.kiPips=(word(28)&256)!=0;}
            l.matchDetails(gen,BattleMode(word(21)),uint8_t(word(23)),seats,uint8_t(word(24)),hud);
        }
        l.language(word(18)?Language::Spanish:Language::English);
        std::lock_guard lock(s.mutex);
        std::memcpy(s.menu.text.data(),b.data()+128,2048);
        s.menu.selected=word(19); s.menu.rows=word(20);
    }
    return ok;
}
} // namespace ps2x::ui
