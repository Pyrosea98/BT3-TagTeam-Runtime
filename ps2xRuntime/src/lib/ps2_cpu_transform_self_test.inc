// Deterministic production guest planner tests. Stubbed boundaries are explicitly
// separate from the later actual native eligibility check; no window/game boot.
int ps2xCpuTransformSelfTest(const char* capture){
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto u=[&](uint32_t p){return readWord(ram+p);};
    auto put=[&](uint32_t p,uint32_t v){std::memcpy(ram+p,&v,4);};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    constexpr uint32_t ctrl=0x074AF000,rows=0x074AF100,probe=0x06FE0400,done=0x0FFFFFFC;
    const auto manager=u(0x2FEB14),phase=u(0x2FEB38),models=u(0x06FE0200),size=u(0x06FE0204);
    const auto own=u(0xD8040),enemy=u(0xD8044),model=u(models+4*u(own+12));
    const auto originalParam=u(model+0x91c),originalCid=u(model+12);
    std::vector<uint8_t> realApply(ram+0x06FE5000,ram+0x06FE5000+size);
    std::vector<uint8_t> stubApply(ram+0x074A1000,ram+0x074A1000+size);
    const auto hp=[&](uint32_t a){return a+0x9E4+164*u(a+0x994);};
    auto reset=[&](unsigned mode=1,unsigned physical=0){
        put(0xD8080,1);put(0xD8088,manager);put(0xD808C,10);put(manager,2);put(0xC4004,0);
        put(0x073E1C00,0);put(0x0768F010,0);put(0x333700,0);put(0x3337B8,0);put(0x3337C0,1);
        put(manager+628,0);put(phase,3);put(phase+260,0x2C6070);put(0x0711F03C,0);
        put(0x077CF000,5);put(0x077CF004,manager);put(0x077CF008,10);put(0x077CF00C,1023);put(0x077CF010,0);
        std::memset(ram+probe,0,64);put(probe+20,0xFFFFFFFF);
        std::memset(ram+rows,0,384);std::memset(ram+0x074AF600,0,768);put(ctrl+52,0);put(ctrl+72,0);put(ctrl+76,0);
        for(unsigned i=0;i<10;++i){const auto a=u(0xD8040+4*i);put(rows+32*i,a);put(rows+32*i+4,10000);
            put(a+0x1278,i==physical?1:0);put(hp(a),10000);put(hp(a)+4,10000);put(hp(a)+12,40000);
            put(a+2376,11);for(auto off:{2380,2388,2392,2396,2400})put(a+off,0xFFFFFFFF);
            for(auto off:{3480,3500,3512})put(a+off,0);
            put(0xD8000+4*i,i^1);
        }
        put(ctrl+4,manager);put(ctrl+12,physical%2==0?mode:0);put(ctrl+16,physical%2==1?mode:0);put(ctrl+20,1);put(ctrl+24,0);
        put(ctrl+28,3);put(ctrl+32,0);put(ctrl+36,0);put(ctrl+40,24);
        const auto a=u(0xD8040+4*physical),m=u(models+4*u(a+12));
        put(m+12,48);put(m+0x91c,0x06FE1000);
        std::memset(ram+0x06FE1000,0,0xB0);
        ram[0x06FE1098]=49;ram[0x06FE1099]=50;ram[0x06FE109A]=48;ram[0x06FE109B]=255;
        ram[0x06FE109C]=1;ram[0x06FE109D]=2;
    };
    auto call=[&](uint32_t pc=0x074A1000,uint32_t a0=0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0){
        R5900Context c{};c.pc=pc;reg(c,28,0x304270);reg(c,29,0x01FFE000);reg(c,31,done);
        reg(c,4,a0);reg(c,5,a1);reg(c,6,a2);reg(c,7,a3);
        c.hi=0x123456789ABCDEFull;c.lo=0xFEDCBA9876543210ull;
        if(!rt.interpretUntil(ram,&c,done,false) || c.pc!=done || rt.isStopRequested())return -1;
        if(pc==0x074A1000 && (c.hi!=0x123456789ABCDEFull || c.lo!=0xFEDCBA9876543210ull))return -2;
        return int(readWord(reinterpret_cast<uint8_t*>(&c.r[2])));
    };
    auto check=[&](bool okay,const char* label){if(!okay)std::cerr<<"FAIL CPU tactics: "<<label<<" starts="<<u(ctrl+36)<<" score="<<u(rows+12)<<"\n";return okay;};
    reset();if(!check(call()>=0 && u(probe)==1 && u(probe+8)==own && u(probe+12)==1 && !u(probe+16),"best eligible form; stock-check arguments; HI/LO"))return 4;
    reset();put(probe+20,1);if(!check(call()>=0 && u(probe)==1 && u(probe+12)==0,"unavailable stronger form falls back"))return 5;
    reset();put(probe+24,1);if(!check(call()>=0 && !u(probe) && !u(ctrl+36),"native veto wins"))return 6;
    reset(0);if(!check(call()>=0 && !u(probe),"default Native no requests"))return 7;
    reset();put(own+0x1278,0);if(!check(call()>=0 && !u(probe),"human/takeover excluded"))return 8;
    reset(1,1);if(!check(call()>=0 && u(probe)==1 && u(probe+8)==enemy,"enemy scope"))return 9;
    reset();put(model+12,46);ram[0x06FE1098]=47;ram[0x06FE1099]=255;ram[0x06FE109C]=0;
    if(!check(call()>=0 && u(probe)==1 && u(probe+12)==0 && u(ctrl+72)==1 && u(0x074AF600+36)==1,"reviewed Goten zero-cost upward form; telemetry and ally count"))return 31;
    reset();put(model+12,47);ram[0x06FE1098]=46;ram[0x06FE1099]=255;ram[0x06FE109C]=0;
    if(!check(call()>=0 && !u(probe),"reviewed zero-cost revert excluded"))return 32;
    reset();put(model+12,77);ram[0x06FE1098]=78;ram[0x06FE1099]=255;ram[0x06FE109C]=0;
    if(!check(call()>=0 && !u(probe),"unknown zero-cost destination excluded"))return 33;
    // Expanded runtime IDs use the production Python-reviewed table in RAM.
    for(const auto& forms:std::array<std::array<unsigned,4>,6>{{
        {{67,190,192,1}},{{190,67,192,1}},{{192,190,67,255}},
        {{149,177,122,1}},{{122,177,51,255}},{{149,192,51,255}}}}){
        reset();put(model+12,forms[0]);
        for(unsigned f=0;f<4;++f){ram[0x06FE1098+f]=255;ram[0x06FE109C+f]=0;}
        ram[0x06FE1098]=forms[1];ram[0x06FE1099]=forms[2];
        if(!check(call()>=0 && (forms[3]==255?!u(probe):(u(probe)==1 && u(probe+12)==forms[3])),
            "reviewed Black/Vegito best free upgrade, reverse/cross-family refusal"))return 35;
    }
    reset();put(ctrl+52,1);put(ctrl+16,1);
    if(!check(call()>=0 && u(ctrl+76)==1 && !u(ctrl+72),"FFA counts all CPUs as enemies"))return 34;
    reset(2);if(!check(call()>=0 && !u(probe) && u(rows+12)==0,"equal opponent no outmatched request"))return 10;
    reset(2);put(hp(own),4000);if(!check(call()>=0 && u(probe)==1 && u(rows+12)==4,"HP gap plus recent loss trigger"))return 11;
    reset(2);put(hp(enemy)+12,60000);put(ctrl+28,1);if(!check(call()>=0 && u(probe)==1 && u(rows+12)==1,"ki deficit trigger"))return 12;
    reset(2);const auto em=u(models+4*u(enemy+12));put(em+12,50);put(ctrl+28,2);
    if(!check(call()>=0 && u(probe)==1 && u(rows+12)==2,"reviewed higher tier trigger"))return 13;
    put(em+12,77);
    for(auto off:{2376,2380,3480,3500,3512}){
        reset();put(own+off,off==2376?200:1);if(!check(call()>=0 && !u(probe),"busy/pending hit excluded"))return 14;
    }
    for(auto address:{0x333700u,0x073E1C00u,0x0768F010u,0x0711F03Cu}){
        reset();put(address,1);if(!check(call()>=0 && !u(probe) && !u(ctrl+32),"paused/preparing/cinematic excluded"))return 15;
    }
    reset();put(0x077CF010,1);if(!check(call()>=0 && !u(probe),"consumed fighter excluded"))return 16;
    reset();put(hp(enemy),0);if(!check(call()>=0 && !u(probe),"dead target excluded"))return 17;
    reset();put(rows,own+16);if(!check(call()>=0 && !u(probe),"stale actor lease excluded"))return 18;
    reset();put(ctrl+4,manager+16);if(!check(call()>=0 && !u(probe),"stale manager excluded"))return 19;
    reset();put(ctrl+20,10);for(unsigned i=0;i<9;++i)if(call()<0 || u(ctrl+32))return 20;
    if(!check(call()>=0 && u(ctrl+32)==1 && u(probe)==1,"3Hz cadence"))return 21;
    put(own+2376,11);put(ctrl+20,1);if(!check(call()>=0 && u(probe)==1,"successful-start cooldown"))return 22;
    reset();put(ctrl+20,10);if(call(0x074A0000)<0 || u(probe+28)!=1)return 23;
    std::cout<<"PASS production EE planner: best form, fallback, veto, allied/enemy/human scopes, HP/ki/tier/loss, cadence, cooldown, pauses, actor/manager leases, frame continuation and HI/LO (stubbed native boundaries)\n";
    // Restore original resources and production native calls; never count the
    // deterministic boundary stubs above as native resource validation.
    reset();put(model+12,originalCid);put(model+0x91c,originalParam);
    std::memcpy(ram+0x074A1000,realApply.data(),size);
    put(hp(own)+20,0);
    auto denied=call(u(0x06FE0208),own,1,1,1);
    if(!check(denied==0,"actual native zero-stock refusal"))return 24;
    std::cout<<"PASS actual captured native eligibility refuses zero-stock request\n";
    put(hp(own)+20,500000);
    auto allowed=call(u(0x06FE0208),own,1,1,1);
    if(allowed!=1){
        std::cerr<<"native diagnostic: admission="<<call(0x077F9000,own)<<" trait="<<call(0x1CF0C8,own,0x5E)
            <<" command="<<call(0x1DAC78,own,0xAF)<<" native busy="<<call(0x1DC348,own)
            <<" roster="<<call(0x8CCC24,0,ram[originalParam+0x98])
            <<" form="<<call(0x20E180,own,0)<<" cost="<<call(0x20E1B0,own,0)<<" type="<<call(0x20E220,own,0)
            <<" permission="<<u(own+0x1300)<<" policy="<<call(0x07292000,own,0,1,1)<<" slot1="<<call(u(0x06FE0208),own,1,1,1)<<" stocks="<<call(0x1CED60,own,100000)
            <<" NPC denials="<<u(0x0729F014)<<" admission reason="<<u(0x073F7F18)<<"\n";
    }
    if(!check(allowed==1,"actual native funded eligibility"))return 25;
    ram[0x0729F100+originalCid]=2;
    auto policyDenied=call(u(0x06FE0208),own,1,1,1);
    if(policyDenied!=0)std::cerr<<"policy diagnostic cid="<<originalCid<<" cpu="<<u(own+0x1278)<<" magic="<<std::hex<<u(0x0729F000)<<" entry="<<u(0x2033C8)<<std::dec<<" predicate="<<call(0x07292000,own,1,1,1)<<" override="<<unsigned(ram[0x0729F100+originalCid])<<"\n";
    if(!check(policyDenied==0,"actual per-character deny"))return 27;
    ram[0x0729F100+originalCid]=0;put(0x0729F00C,1);
    if(!check(call(u(0x06FE0208),own,1,1,1)==0,"actual global transformation veto"))return 28;
    put(0x0729F00C,0);put(0x0729F024,0);
    if(!check(call(u(0x06FE0208),own,1,1,1)==0,"actual zero chance veto"))return 29;
    put(0x0729F024,256);put(0x0729F010,1);
    const auto destination=ram[originalParam+0x99];ram[0x0729F200+destination]=1;
    if(!check(call(u(0x06FE0208),own,1,1,1)==0,"actual giant-policy veto (classified destination fixture)"))return 30;
    ram[0x0729F200+destination]=0;put(0x0729F010,0);
    if(!check(call()>=0 && u(ctrl+36)==1 && u(own+0x12CC)==1,"actual native planner initiation"))return 26;
    std::cout<<"PASS actual native exceptions/global/chance/giant-policy vetoes, funded eligibility and production planner -> guarded native initiation (animation/resource completion still requires live play)\n";
    cpuTacticsDiagnostics(ram,1);cpuTacticsDiagnostics(ram,1,true);
    return 0;
}
