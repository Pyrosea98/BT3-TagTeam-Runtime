// Read-only guest telemetry; bounded at3Hz, no PINE client or extra service.
// Gameplay decisions and counters are written exclusively by the EE planner.
void cpuTacticsDiagnostics(const uint8_t* ram,uint64_t generation=0,bool finish=false){
    static std::mutex mutex;std::lock_guard lock(mutex);
    struct State{uint32_t manager=0,starts[2]={},fusion=0;uint64_t generation=0,next=0,last[12]={};
                 uint32_t reason[12]={},actorStarts[12]={};bool paused=false;};
    static State state;
    auto u=[&](uint32_t at){return readWord(ram+at);};
    const auto now=ps2x::ui::uiNow();constexpr uint32_t ctrl=0x074AF000,diag=0x074AF600;
    const bool owned=u(ctrl)==0x43505431 && u(ctrl+4)==u(0x2FEB14) && u(ctrl+8)==u(0xD8084) &&
        u(0xD8080)==1 && u(0xD8088)==u(ctrl+4) && u(0xD808C)==u(ctrl+8) && u(ctrl+8)>=2 && u(ctrl+8)<=10;
    if(state.manager && (!owned || finish || generation!=state.generation)){
        if(owned && u(ctrl+4)==state.manager && (finish || generation==state.generation)){state.starts[0]=u(ctrl+72);state.starts[1]=u(ctrl+76);}
        std::fprintf(stderr,"[cpu-tactics] summary generation=%llu manager=0x%x starts_allies=%u starts_enemies=%u native_fusions_observed=%u\n",
            (unsigned long long)state.generation,state.manager,state.starts[0],state.starts[1],state.fusion);
        state={};
    }
    if(!owned || finish)return;
    if(!state.manager){state.manager=u(ctrl+4);state.generation=generation;
        std::fprintf(stderr,"[cpu-tactics] attached generation=%llu manager=0x%x count=%u interval_updates=10 allies=%u enemies=%u threshold=%u cooldown_samples=%u\n",
            (unsigned long long)generation,state.manager,u(ctrl+8),u(ctrl+12),u(ctrl+16),u(ctrl+28),u(ctrl+40));
        for(auto& r:state.reason)r=0xFFFFFFFF;
    }
    // Counters are cheap to read every frame; detailed actor sampling is3Hz.
    state.starts[0]=u(ctrl+72);state.starts[1]=u(ctrl+76);
    if(now<state.next)return;state.next=now+334;
    static const char* names[]={"waiting-first-evaluation","human","absent-or-consumed","dead-or-invalid-hp","scope-native",
        "no-live-target","score-below-threshold","cooldown","busy-or-pending-hit","invalid-model",
        "no-slot","unreviewed-zero-cost-or-revert","native-veto","initiation-veto","start"};
    const auto threshold=u(ctrl+28);const char* preset=threshold==1?"relentless":threshold==2?"aggressive":"native";
    const bool paused=u(0x073E1C00)!=0 || u(0x0768F010)!=0 || (u(0x333700)&0xFFFF)!=0 ||
        (u(0x3337B8)&0x3900)!=0 || u(0x3337C0)!=1 || u(state.manager+628)!=0 ||
        (u(0x0711F000)==0x43504F31 && u(0x0711F004)==state.manager && u(0x0711F03C)!=0);
    for(unsigned i=0;i<u(ctrl+8);++i){
        const auto r=diag+64*i,reason=u(r),starts=u(r+36),actor=u(0x074AF100+32*i);
        if(actor<0x100000 || actor>PS2_RAM_SIZE-0x1600 || u(0xD8040+4*i)!=actor)continue;
        const bool changed=reason!=state.reason[i] || starts!=state.actorStarts[i] || paused!=state.paused;
        if(!changed && now-state.last[i]<3000)continue;
        const auto cpu=u(actor+0x1278),slot=u(actor+0x994),mid=u(actor+12);
        if(slot>=5 || mid>=12)continue;const auto row=actor+0x9E4+164*slot,model=u(0x31C640+4*mid);
        if(model<0x100000 || model>PS2_RAM_SIZE-0x1670 || u(model+16)!=mid)continue;
        const auto param=u(model+0x91C),cid=u(model+12);
        if(param<0x100000 || param>PS2_RAM_SIZE-0xB0 || cid>=253)continue;
        const bool enemy=u(ctrl+52)!=0 || (i&1)!=u(ctrl+24);const auto bits=u(r+4);
        const char* decision=reason==14?"start":reason==7?"cooldown":reason>=9?"reject":"wait";
        const char* why=paused?"cinematic-or-combat-gate":reason<15?names[reason]:"unknown-reason";
        // Refine a native veto only where the captured policy/resources prove it.
        if(reason==12 && !paused){
            bool affordable=false;for(unsigned f=0;f<4;++f)if(u(r+20)&(1u<<f))
                affordable|=int32_t(u(row+20))>=int32_t(ram[param+0x9C+f]*100000u);
            if(!affordable)why="no-stock";
            if(u(0x0729F000)==0x4E545031 && u(0x0729F004)==state.manager && u(0x0729F008)==u(ctrl+8)){
                const auto override=ram[0x0729F100+cid];
                if(override==2 || (!override && u(0x0729F00C)))why="exception";
                else if(u(0x0729F024)==0)why="chance0";
                else if(!override && u(0x0729F010)){
                    bool allGiants=u(r+20)!=0;for(unsigned f=0;f<4;++f)if(u(r+20)&(1u<<f)){
                        const auto destination=ram[param+0x98+f];allGiants&=destination<253 && ram[0x0729F200+destination]!=0;}
                    if(allGiants)why="giant";
                }
            }
        }
        const bool started=starts!=state.actorStarts[i];
        std::fprintf(stderr,"[cpu-tactics] actor=%u side=%s cpu=%u cid=%u preset=%s score=%u components=low_hp:%u,hp_gap:%u,ki:%u,loss:%u,tier:%u unknown_tier=%u decision=%s reason=%s slot=%d starts=%u action=%u stocks=%u candidates=0x%x eligible=0x%x forms=%u:%u,%u:%u,%u:%u,%u:%u evaluations=%u\n",
            i,enemy?"enemy":"ally",cpu,cid,preset,u(0x074AF100+32*i+12),bits&1?1:0,bits&2?2:0,bits&4?1:0,bits&8?1:0,bits&16?2:0,
            u(r+8),started?"start":decision,started?"successful-start":why,starts?int(u(r+32)):-1,starts,u(actor+2376),u(row+20),u(r+20),u(r+24),
            ram[param+0x98],ram[param+0x9C],ram[param+0x99],ram[param+0x9D],ram[param+0x9A],ram[param+0x9E],ram[param+0x9B],ram[param+0x9F],u(ctrl+32));
        state.reason[i]=reason;state.actorStarts[i]=starts;state.last[i]=now;
    }
    state.paused=paused;
    constexpr uint32_t fusion=0x077DF000;
    if(u(fusion)==1 && u(fusion+4)==state.manager && u(fusion+8)==u(ctrl+8) && u(fusion+16)!=state.fusion){
        state.fusion=u(fusion+16);const auto actor=u(fusion+24),partner=u(fusion+20);
        if(actor>=0x100000 && actor<=PS2_RAM_SIZE-0x1600){
            const auto slot=u(actor+0x994),mid=u(actor+12);
            if(slot<5 && mid<12){const auto m=u(0x31C640+4*mid),row=actor+0x9E4+164*slot;
                if(m>=0x100000 && m<=PS2_RAM_SIZE-0x1670)
                    std::fprintf(stderr,"[cpu-tactics] native-fusion-commit actor=%u cpu=%u partner=%u result_cid=%u hp=%u ki=%u stocks=%u ordinary_planner_starts=%u source=native-fusion-path choice_control=not-implemented\n",
                        u(actor),u(actor+0x1278),partner,u(m+12),u(row),u(row+12),u(row+20),u(ctrl+36));
            }
        }
    }
}
