#pragma once
#include "ps2_ui_screen_lifecycle.h"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cmath>
#include <chrono>

namespace ps2x::ui {
inline std::string_view killPlateName(std::string_view name){
    auto end=name.find_first_of("[?");
    if(end==std::string_view::npos)end=name.size();
    // Keep the native plate's base identity; roster form/costume annotations
    // can contain Spanish labels even with English menus.
    for(auto suffix:{" super saiyajin"," super saiyan"," 1a forma"," 2a forma"," 3a forma"," forma final"}){
        const std::string_view token=suffix;
        for(size_t i=0;i+token.size()<=end;++i){
            bool match=true;for(size_t j=0;j<token.size();++j){char c=name[i+j];if(c>='A' && c<='Z')c+=32;
                if(c!=token[j]){match=false;break;}}
            if(match){end=i;break;}
        }
    }
    while(end && name[end-1]==' ')--end;
    return end?name.substr(0,end):std::string_view("?");
}
struct HudActor {
    uint16_t character=0;uint8_t seat=0;bool present=false,alive=false;
    uint32_t pointer=0;int32_t hp=0,maxHp=0,ki=0,maxKi=0;
    float fusionLife=-1.f;bool fusionFast=false;
    uint32_t fusionSeconds=0,fusionTotal=0;uint8_t stocks=0;bool transforming=false,charging=false,cinematic=false;
    uint32_t swapSeconds=0,swapTotal=0;uint8_t attackSeat=0,moveSeat=0;
    bool fused=false,fusePrompt=false,showFusionOwner=false,showSwap=false;
};
struct HudRevive {uint8_t owner=255,target=255,state=0,cost=0;float progress=0,opacity=.5f,wave=0;bool ring=true;};
struct HudKill {uint16_t killer=0,victim=0;bool known=false,active=false;};
struct HudProjection {float x=0,y=0,scale=1,distance=0;bool valid=false,onScreen=false;};
struct HudViewport {float x=0,y=0,width=512,height=448;uint8_t subject=0,target=255,seat=1;bool valid=false;std::array<HudProjection,10> points{};};
struct HudSnapshot {
    uint64_t generation=0;uint32_t manager=0,tick=0;
    BattleMode mode=BattleMode::Teams;
    HudPreferences preferences;bool showGameHud=true;float fade=1;std::array<HudViewport,4> views{};uint8_t viewCount=0;
    std::array<HudRevive,10> revives{};
    std::array<HudActor,10> actors{};std::array<HudKill,3> kills{};
    uint8_t introStage=0;uint8_t subject=0,target=255;bool paused=false;bool active=false,training=false,refill=false,idle=false,cinematic=false;
    uint32_t hits=0,damage=0;
    bool showCounters=true,marker=false;float markerX=0,markerY=0;
};
struct HudStore {std::mutex mutex;HudSnapshot state;std::atomic<uint64_t> presentedGeneration{0};std::atomic<unsigned> presentedIntroStage{0};std::atomic<bool> trainingCounterEnabled{false};uint64_t counterGeneration=0;uint32_t hits=0,damage=0;bool resetDown=false;};
inline float overheadDiameter(float distanceScale,uint8_t setting,bool quiet) {
    return std::clamp(44.f*distanceScale*float(setting)/65.f,24.f,46.f)*(quiet?.8f:1.f);
}
inline HudStore& hudStore(){static HudStore store;return store;}
inline bool trainingDamageHook(std::span<const uint8_t> ram) {
    if(ram.size()<0x070BF02C)return false;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;};
    const auto jump=u(0x1CE630);
    return jump==((2u<<26)|(0x07411000u>>2)) ||
        (jump==((2u<<26)|(0x070B7000u>>2)) && u(0x070BF000)==0x52565631u && u(0x070BF028)==0x07411000u);
}
#include "ps2_ui_guest_draw_guards.inc"
inline bool guestHudDrawBoundary(uint32_t pc) {
    return pc==0x073DA200 || pc==0x07272000 || pc==0x072D1800 || pc==0x07413400 || pc==0x07414000 || pc==0x07414800 ||
           (pc>0x070D6800 && pc<0x070D6C00);
}
inline uint32_t replacementHudDrawPc(std::span<const uint8_t> ram,uint32_t pc,uint32_t ra) {
    for(const auto& guard:hudDrawGuards)
        if(pc==guard.pc && uint64_t(guard.base)+guard.size<=ram.size() &&
           std::memcmp(ram.data()+guard.base,guard.bytes,guard.size)==0)
            return guard.next?guard.next:ra;
    return 0;
}
// Exact mod text ownership survives transient per-actor snapshot failures.
// No original game font routine is intercepted by this lease.
inline uint32_t ownedModTextDrawPc(std::span<const uint8_t> ram,const ScreenSnapshot& screen,uint32_t pc,uint32_t ra){
    if((pc!=0x07414000 && pc!=0x07414800) || !screen.generation ||
       screen.phase!=PreparationPhase::Released || ram.size()<0x08000000)return 0;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;};
    const auto manager=u(0x2FEB14),count=u(0xD8084),phase=u(0x2FEB38);
    if(manager<0x100000 || manager>ram.size()-632 || count<2 || count>10 || (count&1) ||
       count!=2u*(std::max)(screen.teamOneCount,screen.teamTwoCount) ||
       u(0xD8080)!=1 || u(0xD8088)!=manager || u(0xD808C)!=count ||
       u(0x07416000)!=1 || u(0x07416004)!=manager || u(0x07416008)!=count ||
       phase<0x100000 || phase>ram.size()-264 || u(phase)!=3 || u(0x333700)!=0 ||
       u(0x073E1C00)!=0 || u(0xC4004)!=0)return 0;
    return replacementHudDrawPc(ram,pc,ra);
}
// Only the render traversal of the two status roots is skipped. Native node
// updates, FIGHT/clash/dialogue roots and menu rendering keep their normal path.
inline uint32_t replacementGameHudRoot(std::span<const uint8_t> ram,const HudSnapshot& h,uint32_t gp) {
    if(h.showGameHud || !h.active || !h.manager || ram.size()<0x08000000 || gp<22324 || gp>=ram.size())return false;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;};
    const uint32_t phase=u(0x2FEB38);
    if(u(0x2FEB14)!=h.manager || u(0x0726F000)!=0x48554431 || u(0x0726F008)!=h.manager ||
       phase<0x100000 || uint64_t(phase)+4>ram.size() || u(phase)!=3 || u(0x333700)!=0)return false;
    const uint32_t root=u(gp-22324);if(root<0x100000 || uint64_t(root)+12>ram.size())return false;
    const bool known=u(0x2188B8)==((2u<<26)|(0x07276000u>>2)) ||
        (u(0x2188B8)==0x27BDFF50 && u(0x2188BC)==0xFFB10098);
    return known?root:0;
}
inline bool hideGameHudNode(std::span<const uint8_t> ram,const HudSnapshot& h,uint32_t gp,uint32_t node) {
    const auto root=replacementGameHudRoot(ram,h,gp);if(!root || !node)return false;
    uint32_t health=0,portrait=0;std::memcpy(&health,ram.data()+root,4);std::memcpy(&portrait,ram.data()+root+8,4);
    return node==health || node==portrait;
}
// Read-only presentation of the native revival rows. States: channel, cost,
// recovery, completed, cancelled, approach, unsafe/busy. No gameplay writes.
inline void captureReviveHud(std::span<const uint8_t> ram,HudSnapshot& h,unsigned count,uint64_t now){
    h.revives={};
    constexpr uint32_t ctrl=0x070BF000,rows=0x070BF100;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;};
    auto f=[&](uint32_t at){float v;std::memcpy(&v,ram.data()+at,4);return v;};
    struct History{uint64_t generation=0,until[10]={};uint32_t target[10]={},commits[10]={};uint8_t outcome[10]={};};
    static History history;
    if(history.generation!=h.generation){history={};history.generation=h.generation;}
    if(u(ctrl)!=0x52565631 || u(ctrl+4)!=h.manager || u(ctrl+8)!=count || u(ctrl+24)<2 || u(ctrl+24)>5 ||
       u(ctrl+20)<8 || u(ctrl+20)>1800 || u(ctrl+16)>10000000 || u(ctrl+16)%100000)return;
    const float radius=f(ctrl+64);if(!std::isfinite(radius) || radius<5 || radius>200)return;
    for(unsigned i=0;i<count;++i){const auto& owner=h.actors[i];auto& out=h.revives[i];const auto row=rows+64*i;
        if(!owner.present || u(row)!=owner.pointer)continue;
        out.owner=uint8_t(i);out.cost=uint8_t(u(ctrl+16)/100000);out.ring=u(ctrl+80)==1 && u(ctrl+84)>0;
        out.opacity=std::clamp(u(ctrl+84)/128.f,0.f,1.f);out.wave=u(ctrl+24)>=3?std::sin(float(u(ctrl+96)&65535)*.0000958738f):0;
        const auto target=u(row+4),commits=u(row+52);
        if(commits!=history.commits[i] && commits>history.commits[i]){history.outcome[i]=4;history.until[i]=now+1000;}
        else if(history.target[i] && !target && commits==history.commits[i]){history.outcome[i]=5;history.until[i]=now+700;}
        history.commits[i]=commits;history.target[i]=target;
        if(u(row+44)){out.target=uint8_t(i);out.state=3;out.progress=1;continue;}
        if(target && target<=count && h.actors[target-1].present && !h.actors[target-1].alive && ((target-1)&1)==(i&1)){
            out.target=uint8_t(target-1);out.state=1;out.progress=std::clamp(float(u(row+8))/u(ctrl+20),0.f,1.f);continue;
        }
        if(now<history.until[i]){out.state=history.outcome[i];continue;}
        if(!owner.alive || !owner.seat || u(owner.pointer+0x1278)!=0)continue;
        float best=1e20f;
        for(unsigned j=0;j<count;++j){const auto& ally=h.actors[j];if(i==j || (i&1)!=(j&1) || !ally.present || ally.alive)continue;
            float distance=0;for(unsigned off:{16u,20u,24u}){const float delta=f(owner.pointer+off)-f(ally.pointer+off);distance+=delta*delta;}
            if(std::isfinite(distance) && distance<best){best=distance;out.target=uint8_t(j);}
        }
        if(out.target==255)continue;
        if(u(row+56))out.state=2;
        else if(best>radius*radius)out.state=6;
        else out.state=7; // close but the native channel has not admitted the action
    }
}
// Guest-thread capture only, before publishing a fixed immutable host snapshot.
// No GS-thread RAM access, new PINE client or guest function execution here.
inline HudSnapshot captureHud(std::span<const uint8_t> ram,const ScreenSnapshot& screen) {
    HudSnapshot h;h.generation=screen.generation;h.mode=screen.mode;
    auto valid=[&](uint32_t p,uint32_t n){return p>=0x100000 && uint64_t(p)+n<=ram.size();};
    auto u=[&](uint32_t p){uint32_t v=0;if(uint64_t(p)+4<=ram.size())std::memcpy(&v,ram.data()+p,4);return v;};
    // Native ownership, fight phase and released holds below are authoritative;
    // the controller may still be receiving its start acknowledgement.
    if((screen.phase!=PreparationPhase::Released && screen.phase!=PreparationPhase::Ready) ||
       !screen.generation || ram.size()<0x08000000)return h;
    h.paused=(u(0x331DC8+0x19F0)&0x100)!=0;
    h.preferences=screen.hud;h.showGameHud=(screen.hudOptions&1)!=0;
    h.showCounters=(screen.hudOptions&4)!=0;
    h.manager=u(0x2FEB14);const auto count=u(0xD8084),phase=u(0x2FEB38);
    if(!valid(h.manager,32) || count<2 || count>10 || (count&1) ||
       u(0xD8080)!=1 || u(0xD8088)!=h.manager || u(0xD808C)!=count ||
       !valid(phase,264) || u(phase)!=3 || u(0x333700)!=0)return h;
    if(u(0x073E1C00)==1 && u(0x073E1C04)==2 && u(0x073E1C08)==h.manager &&
       u(0x073E1C0C)==count && u(0x07361850)==1 && u(0xC4004)==0 && u(0x073E1C18)>=1 && u(0x073E1C18)<=2){
        h.introStage=uint8_t(u(0x073E1C18));return h;
    }
    if(u(0x073E1C00)!=0 || u(0xC4004)!=0)return h;
    const bool cameraOwned=u(0x0711F000)==0x43504F31 && u(0x0711F004)==h.manager && u(0x0711F008)==count;
    h.cinematic=cameraOwned && u(0x0711F03C)!=0;
    constexpr uint32_t participation=0x077CF000;
    const bool participationOwned=u(participation)==5 && u(participation+4)==h.manager && u(participation+8)==count;
    const uint32_t consumed=participationOwned?u(participation+16):0;
    for(unsigned side=0;side<2;++side) {
        const auto n=side?screen.teamTwoCount:screen.teamOneCount;
        const auto offset=side?screen.teamOneCount:0;
        for(unsigned slot=0;slot<n;++slot) {
            const unsigned physical=2*slot+side;if(physical>=count)return HudSnapshot{};
            auto& a=h.actors[physical];a.pointer=u(0xD8040+physical*4);
            if(!valid(a.pointer,0x1600) || u(a.pointer)!=physical || u(a.pointer+8)!=side)return HudSnapshot{};
            const auto hpSlot=u(a.pointer+0x994);if(hpSlot>=5)return HudSnapshot{};
            const auto row=a.pointer+0x9E4+164*hpSlot;
            a.hp=int32_t(u(row));a.maxHp=int32_t(u(row+4));a.ki=int32_t(u(row+12));a.maxKi=int32_t(u(row+16));
            if(a.maxHp<=0 || a.maxHp>0x02000000 || a.maxKi<=0 || a.maxKi>0x02000000)return HudSnapshot{};
            a.hp=std::clamp(a.hp,0,a.maxHp);a.ki=std::clamp(a.ki,0,a.maxKi);
            a.character=screen.fighters[offset+slot];const auto modelId=u(a.pointer+12);
            if(modelId<12){const auto model=u(0x31C640+4*modelId);if(valid(model,0x1670) && u(model+12)<=252)a.character=uint16_t(u(model+12));}
            a.stocks=uint8_t(std::clamp(int32_t(u(row+20))/100000,0,10));
            const auto action=u(a.pointer+2376);a.transforming=action>=236 && action<=240;a.charging=int32_t(u(row+28))>0;
            // Display-only: native authored flags, known entrance/rush/throw
            // participant actions, or the authenticated cinematic member mask.
            a.cinematic=a.transforming || (action>=183 && action<=187) ||
                (action>=301 && action<=303) || (action>=313 && action<=315) ||
                ((ram[a.pointer+4255]|ram[a.pointer+4295])&8)!=0 ||
                (cameraOwned && u(0x0711F034)!=0 && (u(0x0711F038)&(1u<<physical)));
            a.seat=screen.seats[offset+slot];a.present=(consumed&(1u<<physical))==0;a.alive=a.present && a.hp>0 && u(a.pointer+0x14)!=15;
        }
    }
    const auto subject=u(0x073AF108);if(subject<10 && h.actors[subject].present)h.subject=uint8_t(subject);
    const auto target=u(0xD8000+4*h.subject);if(target<10 && h.actors[target].present && h.actors[target].alive)h.target=uint8_t(target);
    constexpr uint32_t feed=0x07416000;
    if((screen.hudOptions&2) && u(feed)==1 && u(feed+4)==h.manager && u(feed+8)==count) {
        h.tick=u(feed+12);
        for(unsigned i=0;i<3;++i){const uint32_t e=0x07416200+i*32;auto& k=h.kills[i];
            k.active=u(e+28)==1 && h.tick-u(e)<=150 && u(e+4)<=252;
            k.victim=uint16_t(u(e+4));k.known=u(e+8)<=252;k.killer=k.known?uint16_t(u(e+8)):0;}
    }
    constexpr uint32_t fusion=0x070DF000;
    if(u(fusion)==0x46555331 && u(fusion+4)==h.manager && u(fusion+8)==count && u(fusion+20))
        for(unsigned i=0;i<count;++i){const auto r=0x06BD0000+i*0x400;const auto leader=u(r+16);
            if(u(r)==2 && leader<count && h.actors[leader].present && h.actors[leader].pointer==u(r+8)){
                auto& a=h.actors[leader];a.fusionSeconds=(u(r+4)+29)/30;a.fusionTotal=(u(r+100)+29)/30;
                const auto rate=u(r+116),total=u(r+100);
                if(u(fusion+52)==1 && u(r+104)==241 && u(r+124)==0x464F5231 && total && rate>=65536 && rate<=4*65536){
                    const uint64_t life=u(r+108),divisor=uint64_t(rate)*30;
                    a.fusionSeconds=uint32_t((life+divisor-1)/divisor);
                      a.fusionSeconds=(std::max)(a.fusionSeconds,((std::min)(u(r+120),30u)+29)/30);
                    a.fusionLife=std::clamp(float(double(life)/(double(total)*65536)),0.f,1.f);a.fusionFast=rate>65536;
                }}}
    constexpr uint32_t coop=0x06BCA000,rows=0x06BCA100;
    if(u(coop)==0x4D465531 && u(coop+4)==h.manager && u(coop+8)==count){
        const auto mode=u(coop+12),hidden=u(coop+20);
        auto interval=u(coop+24);if(interval<150 || interval>1800)interval=600;
        for(unsigned i=0;i<count;++i){const auto r=rows+64*i,status=u(r);
            auto& a=h.actors[i];if(!a.present || !a.alive)continue;
            if(status==1 && u(coop+28)){
                const auto recipient=u(r+16);
                if(recipient<4){const auto p=u(0x06C2F050+4*recipient);if(p<count && h.actors[p].alive)h.actors[p].fusePrompt=true;}}
            if(status!=3 || mode>5)continue;
            const auto first=u(r+12),second=u(r+16);if(first>=4 || second>=4 || first==second)continue;
            const uint32_t elapsed=u(coop+16)-u(r+28),turn=(elapsed/interval)&1;
            a.fused=true;a.showFusionOwner=(hidden&1)==0;
            a.showSwap=(hidden&2)==0 && (mode==0 || mode==3);
            a.swapTotal=(interval+29)/30;a.swapSeconds=(interval-elapsed%interval+29)/30;
            uint8_t attack=uint8_t(first+1),move=uint8_t(second+1);
            if(mode==0)attack=move=uint8_t((turn?second:first)+1);
            else if(mode==2 || (mode==3 && !turn)){attack=uint8_t(second+1);move=uint8_t(first+1);}
            else if(mode==4)attack=move=uint8_t(first+1);
            else if(mode==5)attack=move=uint8_t(second+1);
            a.attackSeat=attack;a.moveSeat=move;
        }
    }
    constexpr uint32_t training=0x070CF000;
    h.training=(screen.mode==BattleMode::Training || screen.mode==BattleMode::TrainingCoop) &&
        u(training)==0x54524E31 && u(training+4)==h.manager && u(training+8)==count;
    if(h.training){h.refill=u(training+12)!=0;h.idle=u(training+24)!=0;}
    captureReviveHud(ram,h,count,uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    h.active=true;return h;
}
// Mirrors multiplayer_fusion.SHARED's authenticated two-seat shared view.
// The renderer bypasses quad camera preparation during this fused interval.
inline bool sharedFusionView(std::span<const uint8_t> ram,const HudSnapshot& h,uint8_t& subject) {
    auto u=[&](uint32_t at){uint32_t v=0;if(uint64_t(at)+4<=ram.size())std::memcpy(&v,ram.data()+at,4);return v;};
    constexpr uint32_t quad=0x06C0F000,fusion=0x06BCA000,rows=0x06BCA100;
    if(!h.active || u(quad)!=0x51564131 || u(quad+4)!=h.manager || u(quad+36)!=2 ||
       u(fusion)!=0x4D465531 || u(fusion+4)!=h.manager || u(fusion+8)!=u(0xD8084))return false;
    const uint32_t owner=u(quad+64);
    if(owner>=10 || owner!=u(quad+68) || !h.actors[owner].present || !h.actors[owner].alive)return false;
    const uint32_t row=rows+64*owner;
    if(u(row)!=3 || u(row+36)>=2 || u(row+40)>=2)return false;
    subject=uint8_t(owner);return true;
}
struct OverheadCounts {unsigned detailed=0,simple=0,filtered=0,offscreen=0,invalid=0,absentOrDead=0;};
inline OverheadCounts overheadCounts(const HudSnapshot& h) {
    OverheadCounts c;if(h.paused)return c;
    for(const auto& v:h.views)if(v.valid)for(unsigned i=0;i<10;++i){const auto& a=h.actors[i];const auto& p=v.points[i];
        if(!a.present || !a.alive){++c.absentOrDead;continue;}
        if(!p.valid){++c.invalid;continue;}
        if(!p.onScreen){++c.offscreen;continue;}
        const bool allied=h.mode!=BattleMode::FreeForAll && ((i&1)==(v.subject&1));
        if(i==v.subject || (i==v.target && h.preferences.detail>=1) || (allied && h.preferences.detail>=2) || h.preferences.detail==3){if(h.preferences.shape)++c.detailed;}
        else if(allied?h.preferences.friends:h.preferences.enemies)++c.simple;
        else ++c.filtered;
    }
    return c;
}
}
