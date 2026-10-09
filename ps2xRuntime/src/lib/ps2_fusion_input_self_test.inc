// Execute original native eligibility branches with deterministic native calls.
int ps2xFusionInputSelfTest(const char* capture){
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto u=[&](uint32_t p){return readWord(ram+p);};
    auto put=[&](uint32_t p,uint32_t v){std::memcpy(ram+p,&v,4);};
    const auto actor=u(0xD8040),mid=u(actor+12),model=u(0x31C640+4*mid);
    constexpr uint32_t done=0x0FFFFFFC,row=0x074DF100,probe=0x06FE0400,control=0x074DF000,param=0x06FE5000;
    const uint32_t sites[]={0x2037CC,0x2037E0,0x2037F0,0x203804,0x203814,0x203830,0x203848,0x203884,0x2038A4,0x2038BC};
    const uint32_t targets[]={0x1CF0C8,0x1DAC78,0x1DC348,0x20E340,0x20E3E8,0x077DD800,0x1CED60,0x1CE108,0x1CE198,0x1CE198};
    uint32_t wrapped[10];for(unsigned i=0;i<10;++i)wrapped[i]=u(sites[i]);
    auto hooks=[&](bool on){for(unsigned i=0;i<10;++i)put(sites[i],on?wrapped[i]:0x0C000000|(targets[i]>>2));};
    auto context=[&](uint32_t pc,unsigned stock,unsigned state,uint32_t returnPc=done){
        R5900Context c{};c.pc=pc;
        for(unsigned r=1;r<32;++r)c.r[r]=_mm_set_epi32(0xA0000000+r,0xB0000000+r,0,0x123000+r);
        auto reg=[&](unsigned r,uint32_t value){c.r[r]=_mm_set_epi32(0xA0000000+r,0xB0000000+r,0,value);};
        reg(4,actor);reg(5,0);reg(6,stock);reg(7,state);reg(8,probe+4);
        reg(28,0x304270);reg(29,0x01FFE000);reg(31,returnPc);
        for(unsigned f=0;f<32;++f)c.f[f]=float(f)+0.375f;
        c.hi=0x123456789ABCDEF;c.lo=0xFEDCBA9876543210;
        c.hi1=0xF123456789ABCDEF;c.lo1=0xA123456789ABCDEF;return c;
    };
    auto same=[&](const R5900Context& a,const R5900Context& b){
        return !std::memcmp(a.r,b.r,sizeof(a.r)) && !std::memcmp(a.f,b.f,sizeof(a.f)) &&
            a.hi==b.hi && a.lo==b.lo && a.hi1==b.hi1 && a.lo1==b.lo1 && a.fcr31==b.fcr31;
    };
    put(model+12,194);put(model+0x91C,param);put(actor+0x994,1);put(actor+0x99C,7777);
    for(unsigned i=0;i<25;++i)ram[param+0xAE+i]=uint8_t(i+1);
    for(unsigned refusal=0;refusal<=9;++refusal)for(unsigned active:{0u,1u}){
        put(0xD8080,active);put(probe,refusal);put(probe+8,refusal==6?4:2);put(actor+0x1300,refusal==9?0:1);
        for(unsigned i=0;i<4;++i){const auto member=actor+0x9A4+164*i;
            put(member,100+i);put(member+0x40,refusal==8?0:80000);put(member+0x70,refusal==7?1:0);put(member+0x54,300000);}
        for(unsigned i=0;i<10;++i)put(probe+0x100+4*i,0);
        put(control+4,0);put(probe+4,0xFFFFFFFF);hooks(false);
        auto expected=context(0x203788,1,1);
        if(!rt.interpretUntil(ram,&expected,done,false)||expected.pc!=done)return 3;
        uint32_t calls[10];for(unsigned i=0;i<10;++i){calls[i]=u(probe+0x100+4*i);put(probe+0x100+4*i,0);}
        put(probe+4,0xFFFFFFFF);hooks(true);auto actual=context(0x074D0000,1,1);
        if(!rt.interpretUntil(ram,&actual,done,false)||actual.pc!=done)return 4;
        if(!same(actual,expected)) {std::fprintf(stderr,"ABI mismatch refusal=%u active=%u\n",refusal,active);return 5;}
        for(unsigned i=0;i<10;++i)if(u(probe+0x100+4*i)!=calls[i])return 22;
        if(u(control+4))return 6;
        if(active){
            if(u(row+4)!=194 || u(row+40)!=300000 || u(row+48)!=7777 || u(row+52)!=1 || u(row+56)!=1)return 7;
            if(u(row+28)!=uint32_t(_mm_extract_epi32(expected.r[2],0)))return 8;
            for(unsigned i=0;i<25;++i)if(ram[row+96+i]!=i+1)return 9;
            if(refusal==0){
                if(u(row+12)!=195 || u(row+24)!=300000 || u(row+84)!=3 || u(row+32)!=2 || u(row+44)!=102){
                    std::fprintf(stderr,"trace acceptance mismatch: result=%u cost=%u searches=%u slot=%08X cid=%08X native_slot=%08X active=%08X\n",
                        u(row+12),u(row+24),u(row+84),u(row+32),u(row+44),u(probe+4),u(control+4));
                    for(unsigned i=0;i<3;++i)std::fprintf(stderr,"candidate%u: query=%08X result=%08X cid=%08X\n",i,u(row+128+24*i),u(row+132+24*i),u(row+136+24*i));
                    std::fprintf(stderr,"gates: 5E=%08X AF=%08X busy=%08X SIDE=%08X stock=%08X state1300=%08X snapshot1300=%08X\n",
                        u(row+64),u(row+68),u(row+72),u(row+76),u(row+80),u(actor+0x1300),u(row+244));
                    for(unsigned i=0;i<10;++i)std::fprintf(stderr,"nativeCalls[%u]=%u ",i,u(probe+0x100+4*i));std::fprintf(stderr,"\n");
                    return 10;
                }
                for(unsigned i=0;i<3;++i){const auto candidate=row+128+24*i;
                    if(u(candidate)!=100+i || u(candidate+4)!=(i==2?2u:0xFFFFFFFFu))return 11;}
                if(u(row+228)!=0 || u(row+236)!=80000)return 12;
            }
            if(refusal==1 && (u(row+64)!=1 || u(row+68)!=0xFFFFFFFF || u(row+84)!=0 || u(row+32)!=0xFFFFFFFF))return 13;
            if(refusal==9 && (u(row+64)!=0xFFFFFFFF || u(row+84)!=0))return 14;
            if(refusal==6 && (u(row+84)!=4 || u(row+224)!=0xFFFFFFFF))return 15;
            if(refusal==7 && (u(row+228)!=1 || u(row+232)!=0xFFFFFFFF))return 16;
            if(refusal==8 && u(row+236)!=0)return 17;
        }
    }
    // Actual native caller stock flag bypasses stock, and state flag bypasses
    // the actor state check. Unreached gate markers must reset each request.
    for(unsigned flag:{0u,1u}){
        put(0xD8080,1);put(actor+0x1300,1);put(probe,5);put(probe+8,2);
        for(unsigned i=0;i<4;++i){put(actor+0x9A4+164*i+0x70,0);put(actor+0x9A4+164*i+0x40,80000);}
        auto c=context(0x074D0000,flag,1);if(!rt.interpretUntil(ram,&c,done,false))return 18;
        if(u(row+80)!=(flag?0u:0xFFFFFFFFu) || u(row+28)!=(flag?0u:1u))return 19;
    }
    // A wrapper called from another caller runs the original target exactly,
    // and cannot append a candidate even when an active-row word is forged.
    put(control+4,row);put(probe,0);const auto before=u(row+64);
    auto expected=context(0x1CF0C8,1,1),actual=context(0x074D2000,1,1);
    if(!rt.interpretUntil(ram,&expected,done,false)||!rt.interpretUntil(ram,&actual,done,false)||!same(actual,expected))return 20;
    if(u(row+64)!=before)return 21;put(control+4,0);
    std::fprintf(stderr,"PASS actual EE fusion gates: original eligibility branch/search, every early refusal, actual stock/state flags, four candidates, raw recipe, correct stock vs ki, reset/unreached markers, cleared active scope; GPR128/FPU/HI0/LO0/HI1/LO1 unchanged\n");
    return 0;
}
