int ps2xV11PromptSelfTest(){
    using namespace ps2x::ui;
    PS2Runtime rt;if(!rt.memory().initialize())return 1;
    auto* ram=rt.memory().getRDRAM();const std::span<const uint8_t> memory{ram,PS2_RAM_SIZE};
    auto put=[&](uint32_t p,uint32_t value){std::memcpy(ram+p,&value,4);};
    constexpr uint32_t stub[]={0x24020000,0x03E00008,0,0};
    for(const auto& g:v11PromptGuards){std::memcpy(ram+g.base,g.bytes,g.size);std::memcpy(ram+g.probe,stub,sizeof(stub));}
    HudSnapshot h;h.active=true;h.manager=0x1800000;h.generation=10;h.viewCount=1;
    for(unsigned i=0;i<2;++i){h.actors[i].pointer=0x1900000+i*0x2000;h.actors[i].present=h.actors[i].alive=true;h.views[0].points[i].valid=true;}
    h.views[0].valid=true;h.views[0].subject=0;
    put(0x2FEB14,h.manager);put(0x073D680C,100);
    put(0x07257000,0x424D5331);put(0x07257004,h.manager);put(0x07257008,2);put(0x07257010,2);put(0x07257058,1);
    put(0x0724F000,0x42434C31);put(0x0724F004,h.manager);put(0x0724F010,1);put(0x0724F014,100);put(0x0724F018,7);
    put(0x07257500,h.actors[0].pointer);put(0x07257504,100);put(0x07257508,7);put(0x0725750C,0x07256CA0);
    std::memcpy(ram+0x07256CA0,"ASISTENCIA FALLIDA - OCUPADO",27);
    put(0x0694F000,0x54485231);put(0x0694F004,h.manager);put(0x0694F00C,2);put(0x0694F04C,4);
    put(0x0694F100,100);put(0x0694F130,100);put(0x0694F134,2);put(0x0694F138,2);put(0x0694F13C,0x4E505231);
    put(0x06957000,0x4C4B5331);put(0x06957004,h.manager);put(0x06957018,1);
    put(0x06957600,h.actors[0].pointer);put(0x06957604,100);put(0x06957608,2);put(0x0695760C,3);
    auto capture=[&]{auto out=h;captureV11Prompts(memory,out,2);return out;};
    auto out=capture();if(!out.prompts[0].beamWarning || !out.prompts[0].beamText[0] ||
        out.prompts[0].target!=1 || out.prompts[0].threats!=2 || out.prompts[0].warnings!=2 || out.prompts[1].beamText[0])return 2;
    put(0x073D680C,102);put(0x0724F014,102);out=capture();
    if(out.prompts[0].beamText[0] || out.prompts[0].target!=255 || out.prompts[0].threats)return 3;
    put(0x073D680C,100);put(0x0724F014,100);put(0x07257500,h.actors[1].pointer);put(0x0694F004,h.manager+4);put(0x06957608,11);
    out=capture();if(out.prompts[0].beamText[0] || out.prompts[0].target!=255 || out.prompts[0].threats)return 4;
    put(0x07257500,h.actors[0].pointer);put(0x0694F004,h.manager);put(0x06957608,2);
    auto& hs=hudStore();hs.state=h;hs.presentedGeneration=10;uiStore().rendererReady=true;
    auto probe=[&](uint32_t pc){R5900Context c{};c.pc=pc;c.r[31]=_mm_set_epi64x(0,0x0FFFFFFC);c.r[4]=_mm_setzero_si128();
        return ps2xTagteamRunNativeHudDraw(ram,&c,&rt) && c.pc==0x0FFFFFFC && _mm_extract_epi32(c.r[2],0)==1;};
    for(const auto& g:v11PromptGuards){if(!probe(g.probe))return 5;
        ram[g.base]^=1;if(probe(g.probe) || v11PromptGuard(memory,g.kind))return 6;ram[g.base]^=1;}
    hs.state.views[0].points[1].valid=false;if(probe(0x0694D0F0) || probe(0x06955B20))return 7;
    hs.presentedGeneration=0;if(probe(0x072533F0))return 8;
    // The ordinary MIPS stub really returns zero on a runtime without coverage.
    R5900Context fallback{};fallback.pc=0x072533F0;fallback.r[31]=_mm_set_epi64x(0,0x0FFFFFFC);
    if(!rt.interpretUntil(ram,&fallback,0x0FFFFFFC,true) || _mm_extract_epi32(fallback.r[2],0))return 9;
    hs.state={};uiStore().rendererReady=false;
    std::fprintf(stderr,"PASS: v11 prompt ownership, freshness, exact code, viewport fallback and MIPS probe ABI\n");
    return 0;
}
