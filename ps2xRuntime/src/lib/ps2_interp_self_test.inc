// Headless checks execute the real interpreter, with no disc, renderer or PINE.
int ps2xInterpreterSelfTest(const char* capture) {
    PS2Runtime rt;
    if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();
    constexpr uint32_t base=0x07000000,data=0x07100000;
    auto word=[&](uint32_t at,uint32_t v){std::memcpy(ram+at,&v,4);};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    auto value=[](const R5900Context& c,unsigned n){uint64_t v;std::memcpy(&v,&c.r[n],8);return v;};
    auto run=[&](int blocks,uint32_t end,R5900Context& c){
        interpreterBlocksTestOverride=blocks;
        return rt.interpretUntil(ram,&c,end,false);
    };
    auto fail=[](const char* test){std::fprintf(stderr,"FAIL interpreter/cleanup: %s\n",test);return 1;};
    // Mixed arithmetic and real guest RAM accesses; ordinary backward branch
    // and its delay slot remain on the baseline branch implementation.
    uint32_t at=base;
    for(unsigned i=0;i<64;++i)word(at+=4,0x24420003); // addiu v0,v0,3
    word(at+=4,0xACE20000); // sw v0,0(a3)
    word(at+=4,0x8CE30000); // lw v1,0(a3)
    word(at+=4,0x2484FFFF); // addiu a0,a0,-1
    const uint32_t branch=at+4;
    word(at+=4,0x14800000|uint16_t(int32_t(base+4-(branch+4))/4));
    word(at+=4,0x24A50001); // delay: addiu a1,a1,1
    const uint32_t end=at+4;
    word(base,0);
    auto initial=[&](){R5900Context c{};c.pc=base;reg(c,4,10000);reg(c,7,data);return c;};
    R5900Context reference=initial(),cached=initial();
    if(!run(0,end,reference)||!run(1,end,cached))return fail("execution failed");
    if(std::memcmp(reference.r,cached.r,sizeof(reference.r)) || reference.pc!=cached.pc ||
       value(cached,2)!=1920000 || value(cached,3)!=1920000 || value(cached,5)!=10000)
        return fail("loop/register/memory/delay parity");
    // An instruction store modifies the next cached instruction in this block.
    word(base,0xACE60004);word(base+4,0x24420001);word(base+8,0x24420002);
    R5900Context modified{};modified.pc=base;reg(modified,7,base);reg(modified,6,0x24420009);
    if(!run(1,base+12,modified)||value(modified,2)!=11)return fail("self-modifying next word");
    word(base,0x24420001);word(base+4,0x24420002);
    R5900Context stop{};stop.pc=base;
    if(!run(1,base+4,stop)||value(stop,2)!=1)return fail("return PC inside block");
    // Branch-likely false annuls its delay slot, even alongside cached runs.
    word(base,0x54000001);word(base+4,0x24420063);word(base+8,0x24420007);
    R5900Context likely{};likely.pc=base;
    if(!run(1,base+12,likely)||value(likely,2)!=7)return fail("annulled branch delay");
    // Exercise cached integer, floating point and 128-bit memory families.
    const uint32_t mixed[]={0x3C023F80,0x34424321,0x2443FFFF,0x00622026,
        0x2C650001,0xACE20000,0x8CE60000,0x7CE20000,0x78E80000,
        0xFCE30010,0xDCE90010,0x44820000,0x44830800,0x46010080,0x44061000};
    R5900Context mixedReference{},mixedCached{};
    mixedReference.pc=mixedCached.pc=base;reg(mixedReference,7,data);reg(mixedCached,7,data);
    for(unsigned i=0;i<std::size(mixed);++i)word(base+i*4,mixed[i]);
    if(!run(0,base+sizeof(mixed),mixedReference)||!run(1,base+sizeof(mixed),mixedCached)||
       std::memcmp(mixedReference.r,mixedCached.r,sizeof(mixedReference.r)) ||
       std::memcmp(mixedReference.f,mixedCached.f,sizeof(mixedReference.f)))return fail("integer/float/128-bit parity");
    std::puts("PASS real interpreter: integer/float/128-bit memory parity, loop/delay slots, self-modifying next word, return PC, branch likely");
    // Repeat alternating paths: output is an isolated interpreter benchmark,
    // not a claim about fight update rate.
    at=base;word(at,0);
    for(unsigned i=0;i<64;++i)word(at+=4,0x24420003);
    word(at+=4,0xACE20000);word(at+=4,0x8CE30000);word(at+=4,0x2484FFFF);
    word(at+=4,0x14800000|uint16_t(int32_t(base+4-(branch+4))/4));word(at+=4,0x24A50001);
    std::array<double,5> baseline{},blocks{};
    for(unsigned sample=0;sample<5;++sample)for(int path=0;path<2;++path){
        auto c=initial();auto start=std::chrono::steady_clock::now();
        if(!run(path,end,c))return fail("benchmark execution");
        (path?blocks:baseline)[sample]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    std::sort(baseline.begin(),baseline.end());std::sort(blocks.begin(),blocks.end());
    std::printf("BENCH real interpreter 690001 instructions: baseline_median_ms=%.3f blocks_median_ms=%.3f speedup=%.3fx\n",baseline[2],blocks[2],baseline[2]/blocks[2]);
    interpreterBlocksTestOverride=-1;
    if(capture){
        std::ifstream file(capture,std::ios::binary);file.read(reinterpret_cast<char*>(ram),0x08000000);
        if(file.gcount()!=0x08000000)return fail("capture length");
        constexpr uint32_t owner=0x123456;
        word(0x07FFF10C,owner);
        std::vector<uint8_t> before(ram,ram+0x08000000);uint32_t error=0;
        if(ps2TrainingCleanup({ram,0x08000000},owner,false,error)||error!=1)return fail("missing teardown receipt");
        word(0x2FF084,0x02000000);
        if(ps2TrainingCleanup({ram,0x08000000},owner,true,error)||error!=2)return fail("live heap bounds");
        word(0x2FF084,0);
        uint32_t manager,actors;std::memcpy(&manager,ram+0x2FEB14,4);std::memcpy(&actors,ram+manager+4,4);
        word(manager,10);
        if(ps2TrainingCleanup({ram,0x08000000},owner,true,error)||error!=2)return fail("expanded manager");
        word(manager,2);word(manager+4,0x02000000);
        if(ps2TrainingCleanup({ram,0x08000000},owner,true,error)||error!=3)return fail("expanded actor storage");
        word(manager+4,actors);
        if(ps2TrainingCleanup({ram,0x08000000},owner+1,true,error)||error!=2)return fail("foreign owner");
        if(std::memcmp(ram,before.data(),before.size()))return fail("rejected cleanup modified RAM");
        if(!ps2TrainingCleanup({ram,0x08000000},owner,true,error))return fail("archived training recovery");
        for(uint32_t i=0;i<0x08000000;++i){
            if(i>=0x02000000 && i<0x06000000){if(ram[i])return fail("arena residue");}
            else if(ram[i]!=before[i])return fail("unowned bytes changed");
        }
        std::puts("PASS failed capture: detached arena reset; all other RAM unchanged; missing receipt/foreign owner/live bounds/expanded manager/expanded actor storage refused");
    }
    return 0;
}

int ps2xNativeLeafSelfTest(const char* capture) {
#if defined(_WIN32)
    _putenv_s("PS2X_NATIVE_TARGETS","oracle");
#else
    setenv("PS2X_NATIVE_TARGETS","oracle",1);
#endif
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),0x08000000);if(file.gcount()!=0x08000000)return 2;
    auto u=[&](uint32_t at){uint32_t v;std::memcpy(&v,ram+at,4);return v;};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    // HUD scope is live only during its publisher; reproduce that scope on the
    // saved roster without changing actor/target/participation/HP identities.
    const uint32_t active=1;std::memcpy(ram+0x0726F01C,&active,4);
    const uint32_t entries[]={0x07368000,0x07368200,0x07368600,0x07368808,
        0x077C4000,0x070B0400,0x070B1000,0x07260000,0x071A0000,0x07243000,0x07180000,
        0x070F0000,0x070F0400,0x07781000,0x07788000,0x06944000,0x06944400};
    for(unsigned slot=0;slot<std::size(entries);++slot){
        for(unsigned sample=0;sample<64;++sample){
            R5900Context c{};
            for(unsigned r=1;r<32;++r)c.r[r]=_mm_set_epi64x(0x1234567890000000ull+r,0xabcdef0000000000ull+r);
            c.pc=entries[slot];reg(c,28,0x2FEB14+22364);reg(c,29,0x01FFE000);
            reg(c,31,slot==4?0x1C1AE4:0x0FFFFFFC);
            const auto physical=sample%10;
            uint32_t argument=physical;
            if(slot==3 || slot==4)argument=u(0xD8040+physical*4);
            if(slot==2)argument=physical+40;
            if(slot==7)argument=sample%3;
            if(slot>=12)argument=u(0xD8040+physical*4);
            if(slot==11)argument=u(0x31C640+4*(sample%12));
            reg(c,4,argument);
            if(slot>=15)reg(c,5,sample%2?5:7);
            if(slot==3)reg(c,8,0xD8080); // wrappers replay LUI/ORI before the untouched resolver tail
            if(!ps2xTagteamRunNativeTargets(ram,&c,&rt)) {
                std::fprintf(stderr,"FAIL native leaf signature entry=0x%x\n",entries[slot]);return 1;
            }
            if(rt.isStopRequested())return 1;
        }
    }
    unsigned elfCount=0,accepted=0,skipped=0;
    const auto* elfEntries=ps2xTagteamNativeElfEntries(elfCount);
    for(unsigned i=0;i<elfCount;++i){
        bool selected=false;
        for(unsigned sample=0;sample<64;++sample){
            R5900Context c{};
            for(unsigned r=1;r<32;++r)c.r[r]=_mm_set_epi64x(0x1234567890000000ull+r,0xabcdef0000000000ull+r);
            c.pc=elfEntries[i];reg(c,28,0x2FEB14+22364);reg(c,29,0x01FFE000);reg(c,31,0x0FFFFFFC);
            reg(c,4,c.pc==0x1da8a0?sample%10:u(0xD8040+4*(sample%10)));reg(c,5,sample*2);
            reg(c,6,u(0xD8040+4*((sample+1)%10)));reg(c,7,u(0xD8040+4*((sample+2)%10)));
            if(c.pc==0x1dac80 || c.pc==0x1dacf0){reg(c,3,(sample*2)>>3);reg(c,5,(sample*2)&7);}
            if(!ps2xTagteamRunNativeTargets(ram,&c,&rt))break;
            selected=true;if(rt.isStopRequested())return 1;
        }
        if(selected)++accepted;else{++skipped;std::fprintf(stderr,"SKIP changed ELF entry=0x%x\n",elfEntries[i]);}
    }
    if(!accepted)return 1;
    std::fprintf(stderr,"PASS production ELF adapter: %u unchanged entries x64 oracle samples; %u changed entries stayed interpreted\n",accepted,skipped);
    std::puts("PASS production native adapter vs real interpreter: 1088 cases,17 entries, actual saved roster, upper registers/PC/stack/HUD/throw-record writes");
    return 0;
}
