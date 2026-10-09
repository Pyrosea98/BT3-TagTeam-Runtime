int ps2xFusionControlsSelfTest(const char* capture){
    if(ps2x::ui::killPlateName("GOTENKS SUPER SAIYAJIN 3?26?")!="GOTENKS" ||
       ps2x::ui::killPlateName("FREEZER 2A FORMA[7]")!="FREEZER" ||
       ps2x::ui::killPlateName("Super Buu")!="Super Buu")return 36;
    ps2x::ui::TeamDefeatWatch watch;
    if(watch.sample(1,100,1,true) || watch.sample(1,5099,1,true) || !watch.sample(1,5100,1,true) || watch.sample(1,5200,1,true))return 27;
    if(watch.sample(2,6000,1,true) || watch.sample(2,10999,1,true) || !watch.sample(2,11000,1,true))return 28;
    if(watch.sample(3,12000,1,true) || watch.sample(3,16000,1,false) || watch.sample(3,17000,1,true) || watch.sample(3,21999,1,true) || !watch.sample(3,22000,1,true))return 29;
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto u=[&](uint32_t p){return readWord(ram+p);};
    auto put=[&](uint32_t p,uint32_t v){std::memcpy(ram+p,&v,4);};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    constexpr uint32_t ctrl=0x06BCA000,rows=ctrl+0x100,owned=0x06C2F050,quad=0x06C1F000,consumed=0x077CF010;
    constexpr uint32_t done=0x0FFFFFFC,pad=0x06BBA000,follow=0x06BBC000,tick=0x06BB6000;
    uint32_t ports[4];
    // Producer publishes the real pad addresses in a fixture-only header.
    for(unsigned n=0;n<4;++n)ports[n]=u(0x06FE0100+4*n);
    const auto manager=u(0x2FEB14),count=u(0xD8084),actor=u(0xD8040),partner=u(0xD8048);
    if(count!=10 || !actor || !partner || !ports[0])return 3;
    put(0xD8080,1);put(0xD8088,manager);put(0xD808C,count);put(manager,2);
    put(0xC4004,0);put(0x073E1C00,0);put(0x07361850,0);put(0x0768F010,0);
    put(0x333700,0);put(0x3337B8,0);put(0x3337C0,1);put(manager+628,0);
    const auto phase=u(0x2FEB38);put(phase,3);put(phase+260,0x2C6070);
    put(ctrl,0x4D465531);put(ctrl+4,manager);put(ctrl+8,count);put(ctrl+20,0);put(ctrl+28,0);
    put(quad,0x51494E31);put(quad+4,manager);put(quad+8,count);put(quad+12,1);
    put(0x06C0F000,0x51564131);put(0x06C0F004,manager);put(0x06C0F024,4);
    const auto camera=u(0x304270-22176);
    for(unsigned v=0;v<4;++v){std::memcpy(ram+0x06C05000+v*1024,ram+camera,656);
        put(0x06C05200+v*1024,(v%2)*257);put(0x06C05204+v*1024,(v%2)*257+254);
        put(0x06C05208+v*1024,(v/2)*225);put(0x06C0520C+v*1024,(v/2)*225+222);}
    put(0x077CF000,5);put(0x077CF004,manager);put(0x077CF008,count);put(0x077CF00C,1023);put(consumed,4);
    for(unsigned n=0;n<4;++n)for(unsigned off:{304u,308u,312u,316u,328u,332u,336u,340u,348u})put(ports[n]+off,0x11223300+0x11111111*n+off);
    auto call=[&](uint32_t pc,uint32_t argument,uint32_t& result){
        R5900Context c{};c.pc=pc;reg(c,4,argument);reg(c,28,0x304270);reg(c,29,0x01FFE000);reg(c,31,done);
        const bool ok=rt.interpretUntil(ram,&c,done,false);result=uint32_t(_mm_cvtsi128_si32(c.r[2]));return ok && c.pc==done && !rt.isStopRequested();
    };
    unsigned pairs=0,cases=0;
    for(unsigned first=0;first<4;++first)for(unsigned second=0;second<4;++second){
        if(first==second)continue;++pairs;
        uint32_t assignments[4];unsigned remaining=4;
        for(unsigned seat=0;seat<4;++seat){assignments[seat]=seat==first?0:seat==second?2:remaining; if(seat!=first && seat!=second)remaining+=2;
            put(owned+4*seat,assignments[seat]);put(quad+0x40+4*seat,assignments[seat]);}
        for(unsigned mode=0;mode<6;++mode){++cases;
            for(unsigned interval:{150u,1800u})for(unsigned elapsed:{0u,interval-1,interval,2*interval}){
                std::memset(ram+rows,0,640);put(rows,3);put(rows+4,2);put(rows+12,first);put(rows+16,second);put(rows+28,1000);
                put(rows+36,first);put(rows+40,second);put(ctrl+12,mode);put(ctrl+16,1000+elapsed);put(ctrl+24,interval);put(consumed,4);
                uint32_t selected=0;if(!call(pad,actor,selected))return 4;
                const auto turn=(elapsed/interval)&1;
                unsigned attack=first,move=second;
                if(mode==0)attack=move=turn?second:first;
                if(mode==2 || (mode==3 && !turn)){attack=second;move=first;}
                if(mode==4)attack=move=first;if(mode==5)attack=move=second;
                if(attack==move){if(selected!=ports[attack])return 5;}
                else for(unsigned off:{328u,332u,336u,340u,348u})
                    if(u(selected+off)!=((u(ports[attack]+off)&~0x4AF0u)|(u(ports[move]+off)&0x4AF0u)))return 6;
                if(attack!=move)for(unsigned off:{304u,308u,312u,316u})if(u(selected+off)!=u(ports[move]+off))return 7;
                ps2x::ui::ScreenSnapshot screen;screen.phase=ps2x::ui::PreparationPhase::Released;screen.generation=1;screen.teamOneCount=screen.teamTwoCount=5;
                auto h=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);
                if(!h.active || !h.actors[0].fused || h.actors[0].attackSeat!=attack+1 || h.actors[0].moveSeat!=move+1 ||
                   h.actors[0].swapSeconds!=(interval-elapsed%interval+29)/30)return 8;
                for(unsigned seat=0;seat<4;++seat){
                    uint32_t watched;if(!call(follow,assignments[seat],watched))return 13;
                    const auto expected=seat==first || seat==second?0:assignments[seat];
                    if(watched==0xFFFFFFFF)watched=assignments[seat];if(watched!=expected)return 14;
                    put(0x06C0F040+4*seat,watched);
                }
                if(!elapsed){
                    R5900Context context{};reg(context,28,0x304270);reg(context,29,0x01FFD000);
                    nativeOverheadProjection(ram,&context,&rt,h);
                    if(h.viewCount!=4)return 15;
                    for(unsigned seat=0;seat<4;++seat)if(!h.views[seat].valid || h.views[seat].subject!=u(0x06C0F040+4*seat))return 16;
                    if(first<3 && second<3){
                        put(0x06C0F024,3);auto three=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);
                        nativeOverheadProjection(ram,&context,&rt,three);if(three.viewCount!=3)return 30;
                        for(unsigned seat=0;seat<3;++seat)if(!three.views[seat].valid || three.views[seat].subject!=u(0x06C0F040+4*seat))return 31;
                        put(0x06C0F024,4);
                    }
                    // Four-seat fusion cannot select the two-seat shared camera.
                    uint8_t subject;if(ps2x::ui::sharedFusionView({ram,PS2_RAM_SIZE},h,subject))return 17;
                }
                // Defusion restores each participating seat's own input.
                put(rows,0);put(consumed,0);
                for(unsigned seat=0;seat<4;++seat){if(!call(pad,u(0xD8040+4*assignments[seat]),selected) || selected!=ports[seat])return 9;}
                put(rows,3);put(consumed,4);
            }
        }
    }
    // Timer records authenticate an actor pointer plus physical leader, not an index.
    ps2x::ui::ScreenSnapshot screen;screen.phase=ps2x::ui::PreparationPhase::Released;screen.generation=1;screen.teamOneCount=screen.teamTwoCount=5;
    put(0x070DF000,0x46555331);put(0x070DF004,manager);put(0x070DF008,count);put(0x070DF014,1);
    std::memset(ram+0x06BD0000,0,1024*count);put(0x06BD0000,2);put(0x06BD0004,211);put(0x06BD0008,actor);put(0x06BD0010,0);put(0x06BD0064,3000);
    auto timerHud=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);
    if(timerHud.actors[0].fusionSeconds!=8 || timerHud.actors[0].fusionTotal!=100)return 19;
    put(0x06BD0008,partner);if(ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen).actors[0].fusionSeconds)return 20;
    put(0x06BD0008,actor);put(0x070DF014,0);if(ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen).actors[0].fusionSeconds)return 21;
    put(ctrl+20,3);auto hidden=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);if(hidden.actors[0].showSwap || hidden.actors[0].showFusionOwner)return 22;
    put(ctrl+20,0);
    // Idle and defused R3 mean transform; only the requested seat gets a chip.
    std::memset(ram+rows,0,640);put(consumed,0);put(ctrl+28,1);put(0x0711F03C,0);
    uint32_t result=0;if(!call(tick,0,result))return 23;
    for(unsigned i=0;i<count;++i)if(u(rows+64*i+44))return 24;
    put(rows,1);put(rows+16,3);put(owned+12,2);
    auto request=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);
    if(!request.actors[2].fusePrompt || request.actors[0].fusePrompt)return 32;
    put(rows,0);if(ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen).actors[2].fusePrompt)return 33;
    put(ctrl+28,0);if(!call(tick,0,result))return 25;
    for(unsigned i=0;i<count;++i)if(u(rows+64*i+44))return 26;
    // Both guest text sizes must return through the actual interpreter boundary,
    // for both native HUD styles, without executing the guest glyph renderer.
    auto& hudStore=ps2x::ui::hudStore();hudStore.presentedGeneration.store(1);
    for(bool originalHud:{false,true}){
        hudStore.state=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);hudStore.state.showGameHud=originalHud;
        for(unsigned text:{0x07414000u,0x07414800u}){
            if(!ps2x::ui::replacementHudDrawPc({ram,PS2_RAM_SIZE},text,done))return 34;
            if(!call(text,0,result))return 35;
            // A straight cached block ending immediately before the helper
            // must yield to its native interception instead of absorbing it.
            put(text-4,0);if(!call(text-4,0,result) || result)return 37;
        }
    }
    // CPU/human KO combinations and rematch generations do not require a
    // valid actor HUD snapshot or an acknowledged GS presentation for mod text.
    auto& lifecycle=ps2x::ui::uiStore().lifecycle;
    const std::array<uint16_t,5> fighters{1,2,3,4,5};
    put(0x07416000,1);put(0x07416004,manager);put(0x07416008,count);
    for(uint64_t generation:{1ull,2ull}){
        if(!lifecycle.begin(generation,generation,fighters,fighters) || !lifecycle.ready(generation,generation) || !lifecycle.released(generation,generation))return 38;
        const auto lease=lifecycle.snapshot(generation);
        for(bool originalHud:{false,true})for(unsigned killerCpu:{0u,1u})for(unsigned victimCpu:{0u,1u}){
            put(actor+0x1278,killerCpu);put(partner+0x1278,victimCpu);
            hudStore.state={};hudStore.state.showGameHud=originalHud;hudStore.presentedGeneration.store(0);
            for(unsigned text:{0x07414000u,0x07414800u}){
                if(!ps2x::ui::ownedModTextDrawPc({ram,PS2_RAM_SIZE},lease,text,done) || !call(text,0,result))return 39;
                const auto word=u(text);put(text,word^1);
                if(ps2x::ui::ownedModTextDrawPc({ram,PS2_RAM_SIZE},lease,text,done))return 40;
                put(text,word);
            }
        }
        put(0x07416004,manager+16);
        if(ps2x::ui::ownedModTextDrawPc({ram,PS2_RAM_SIZE},lease,0x07414000,done))return 41;
        put(0x07416004,manager);
        auto stale=lease;stale.phase=ps2x::ui::PreparationPhase::Preparing;
        if(ps2x::ui::ownedModTextDrawPc({ram,PS2_RAM_SIZE},stale,0x07414000,done))return 42;
    }
    std::cout<<"PASS exact mod text lease: CPU/human kill combinations, both styles, two generations, inactive HUD/unacknowledged presentation; corrupted code, wrong manager and unreleased owner refused\n";
    // Native-only producer cannot submit glyphs even without host draw ownership.
    hudStore.presentedGeneration.store(0);hudStore.state={};
    const auto drawn=u(0x07416018);put(0x07413400,0x03E00008);put(0x07413404,0);
    if(!call(0x07413400,0,result) || u(0x07416018)!=drawn)return 49;
    std::cout<<"PASS actual EE native-only kill-feed producer returns without glyph submission/counter mutation\n";
    // Production revival snapshot: real row layout and deterministic host clock.
    constexpr uint32_t revive=0x070BF000,reviveRows=0x070BF100;
    put(revive,0x52565631);put(revive+4,manager);put(revive+8,count);put(revive+16,200000);put(revive+20,30);put(revive+24,5);
    put(revive+64,0x42200000);put(revive+80,1);put(revive+84,64);
    std::memset(ram+reviveRows,0,640);put(reviveRows,actor);put(reviveRows+128,partner);put(reviveRows+4,3);put(reviveRows+8,15);
    ps2x::ui::HudSnapshot reviveHud;reviveHud.generation=123;reviveHud.manager=manager;
    reviveHud.actors[0].pointer=actor;reviveHud.actors[0].present=reviveHud.actors[0].alive=true;reviveHud.actors[0].seat=1;
    reviveHud.actors[2].pointer=partner;reviveHud.actors[2].present=true;put(actor+0x1278,0);
    ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,100);
    if(reviveHud.revives[0].state!=1 || reviveHud.revives[0].target!=2 || reviveHud.revives[0].progress!=.5f || reviveHud.revives[0].cost!=2)return 43;
    put(reviveRows+4,0);put(reviveRows+52,1);
    ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,200);if(reviveHud.revives[0].state!=4)return 44;
    put(reviveRows+4,3);ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,1300);
    put(reviveRows+4,0);ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,1400);if(reviveHud.revives[0].state!=5)return 45;
    put(reviveRows+56,1);ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,2200);if(reviveHud.revives[0].state!=2)return 46;
    put(reviveRows+128+44,1);ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,2300);if(reviveHud.revives[2].state!=3)return 47;
    put(revive+4,manager+16);ps2x::ui::captureReviveHud({ram,PS2_RAM_SIZE},reviveHud,count,2400);
    for(const auto& r:reviveHud.revives)if(r.state)return 48;
    std::cout<<"PASS native revive HUD channel/half-progress/cost/completion/cancel/stock refusal/recovery and stale-manager rejection\n";
    put(ctrl+16,1000);put(0x3337B8,0x100);
    if(!call(tick,0,result) || u(ctrl+16)!=1000)return 10;
    put(0x3337B8,0);if(!call(tick,0,result) || u(ctrl+16)!=1001)return 11;
    put(0x3337B8,0x2000);if(!call(tick,0,result) || u(ctrl+16)!=1001)return 12;
    put(0x3337B8,0);put(0x0711F000,0x43504F31);put(0x0711F004,manager);put(0x0711F03C,2);
    if(!call(tick,0,result) || u(ctrl+16)!=1001)return 18;
    std::printf("PASS fusion: %u ordered pairs x6 modes=%u cases; actual PAD/FOLLOW, 5/60s boundaries, merged buttons/analogs, native HUD owner/countdown, all-seat defusion input, pause/cinematic clocks, pointer timer/settings guards, pending-recipient prompt only; idle/defused prompts absent\n",pairs,cases);
    return 0;
}
