// Execute the production EE clock against captured native actor/model resources.
int ps2xFusionFormSelfTest(const char* capture){
    PS2Runtime rt;if(!rt.memory().initialize())return 2;
    auto* ram=rt.memory().getRDRAM();std::ifstream file(capture,std::ios::binary);
    file.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE);if(file.gcount()!=PS2_RAM_SIZE)return 2;
    auto u=[&](uint32_t p){return readWord(ram+p);};
    auto put=[&](uint32_t p,uint32_t v){std::memcpy(ram+p,&v,4);};
    auto reg=[](R5900Context& c,unsigned n,uint64_t v){std::memcpy(&c.r[n],&v,8);};
    constexpr uint32_t ctrl=0x070DF000,records=0x06BD0000,unit=65536,done=0x0FFFFFFC;
    const auto manager=u(0x2FEB14),actor=u(0xD8040),phase=u(0x2FEB38);
    const auto modelTable=u(0x06FE0100),finished=u(0x06FE0104),pair=u(0x06FE0108);
    const auto model=u(modelTable+4*u(actor+12));
    if(!actor || !model || u(0xD8084)!=10)return 3;
    put(0xD8080,1);put(0xD8088,manager);put(0xD808C,10);put(manager,2);put(pair,0);
    put(0x073E1C00,0);put(0x0768F010,0);put(0x333700,0);put(0x3337B8,0);put(0x3337C0,1);
    put(manager+628,0);put(phase,3);put(phase+260,0x2C6070);
    put(0x077CF000,5);put(0x077CF004,manager);put(0x077CF008,10);put(0x077CF00C,1023);put(0x077CF010,4);
    put(0x0711F000,0x43504F31);put(0x0711F004,manager);put(0x0711F03C,0);
    std::memset(ram+ctrl,0,256);put(ctrl,0x46555331);put(ctrl+4,manager);put(ctrl+8,10);put(ctrl+12,1);
    put(ctrl+16,3000);put(ctrl+20,1);put(ctrl+52,1);put(ctrl+56,1);
    const auto selected=u(actor+0x994);put(actor+0x9E4+164*selected,10000);put(actor+2376,11);
    auto reset=[&](unsigned cid,unsigned ticks=3000,unsigned kind=241){
        put(0x0768F010,0);put(ctrl+28,0xFFFFFFFF);
        std::memset(ram+records,0,10240);put(records,2);put(records+4,ticks);put(records+8,actor);
        put(records+16,0);put(records+20,2);put(records+56,u(actor+4));put(records+96,u(finished));
        put(records+100,3000);put(records+104,kind);put(model+12,cid);
    };
    auto call=[&](){
        R5900Context c{};c.pc=0x070D2800;reg(c,28,0x304270);reg(c,29,0x01FFE000);reg(c,31,done);
        c.hi=0x123456789ABCDEF0ull;c.lo=0xFEDCBA9876543210ull;
        return rt.interpretUntil(ram,&c,done,false) && c.pc==done && !rt.isStopRequested() &&
               c.hi==0x123456789ABCDEF0ull && c.lo==0xFEDCBA9876543210ull;
    };
    // All tiers and presets: independent integer rate oracle, actual MIPS drain.
    constexpr unsigned ids[6]={110,53,59,58,62,219};
    constexpr unsigned penalties[3][6]={{0,5,10,15,20,25},{0,10,20,30,40,50},{0,20,35,50,60,70}};
    for(unsigned preset=0;preset<3;++preset)for(unsigned tier=0;tier<6;++tier){
        put(ctrl+56,preset);reset(ids[tier]);const unsigned d=100-penalties[preset][tier],rate=(100*unit+d/2)/d;
        for(unsigned n=1;n<=60;++n){if(!call())return 4;
            if(u(records+108)!=3000*unit-n*rate || u(records+116)!=rate || u(records+4)!=(u(records+108)+unit-1)/unit){
                std::cerr<<"form clock mismatch preset="<<preset<<" tier="<<tier<<" frame="<<n<<" status="<<u(records)<<" life="<<u(records+108)<<" rate="<<u(records+116)<<"\n";return 5;
            }
        }
    }
    put(ctrl+56,1);
    for(unsigned first=0;first<4;++first)for(unsigned second=0;second<4;++second){
        if(first==second)continue;
        reset(219);const unsigned physical=2*first,partnerPhysical=2*second;
        const auto a=u(0xD8040+4*physical),r=records+physical*1024,m=u(modelTable+4*u(a+12));
        if(!a || !m)return 29;
        if(physical){std::memcpy(ram+r,ram+records,1024);std::memset(ram+records,0,1024);}
        put(r+8,a);put(r+16,physical);put(r+20,partnerPhysical);put(r+56,u(a+4));
        put(a+0x9E4+164*u(a+0x994),10000);put(a+2376,11);put(m+12,219);
        put(0x077CF010,1u<<partnerPhysical);
        if(!call() || u(r+108)!=2998*unit || u(r+116)!=2*unit)return 30;
    }
    put(0x077CF010,4);
    reset(49);if(!call() || u(records+116)!=72818)return 6;
    auto life=u(records+108);put(model+12,50);if(!call() || u(records+116)!=93623 || u(records+108)!=life)return 7;
    if(!call() || u(records+108)!=life-93623)return 8;
    life=u(records+108);put(model+12,48);if(!call() || u(records+108)!=life || u(records+116)!=unit)return 9;
    if(!call() || u(records+108)!=life-unit)return 10;
    reset(110,90);if(!call())return 11;life=u(records+108);put(model+12,219);
    if(!call() || u(records+108)!=life || u(records+116)!=2*unit)return 12;
    reset(110,30);if(!call())return 13;put(records+108,unit);put(model+12,219);
    if(!call() || u(records+108)!=60*unit)return 14;
    for(unsigned n=0;n<29;++n)if(!call())return 15;
    if(u(records+108)!=2*unit || !call() || u(records+108)!=0)return 16;
    // A one-second configured budget still stays within life 0..1 in Blue.
    reset(110,30);put(records+100,30);if(!call())return 31;put(model+12,219);
    if(!call() || u(records+108)!=30*unit || u(records+120)!=30)return 32;
    for(unsigned n=0;n<29;++n)if(!call() || u(records)!=2 || u(records+108)>30*unit)return 33;
    if(u(records+108)!=0 || u(records+120)!=1 || !call() || u(records+120)!=0)return 34;
    reset(219);if(!call())return 17;life=u(records+108);
    for(unsigned action=236;action<=243;++action){put(actor+2376,action);if(!call() || u(records+108)!=life)return 18;}
    put(actor+2376,11);put(0x0711F03C,1);if(!call() || u(records+108)!=life)return 19;put(0x0711F03C,0);
    put(0x333700,1);if(!call() || u(records+108)!=life)return 20;put(0x333700,0);
    reset(219,3000,242);if(!call() || u(records+4)!=2999 || u(records+124))return 21;
    reset(219);put(records,0);if(!call() || u(records+4)!=3000 || u(records+124))return 22;
    reset(219);put(ctrl+52,0);if(!call() || u(records+4)!=2999 || u(records+124))return 23;put(ctrl+52,1);
    reset(220);if(!call() || u(records+116)!=unit)return 24;
    reset(219);put(records+8,u(0xD8048));if(!call() || u(records))return 25;
    reset(219);if(!call())return 26;
    ps2x::ui::ScreenSnapshot screen;screen.phase=ps2x::ui::PreparationPhase::Released;screen.generation=1;screen.teamOneCount=screen.teamTwoCount=5;
    auto hud=ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen);
    if(hud.actors[0].fusionSeconds!=50 || !hud.actors[0].fusionFast || hud.actors[0].fusionLife<.999f)return 27;
    put(ctrl+20,0);if(ps2x::ui::captureHud({ram,PS2_RAM_SIZE},screen).actors[0].fusionSeconds)return 28;
    std::cout<<"PASS actual MIPS form clock: 18 tiers/presets, Gotenks SSJ3/direct/revert, Blue/grace, transform/scene pauses, Potara/permanent/Off/unknown, stale pointer, HI/LO, projected native HUD\n";
    return 0;
}
