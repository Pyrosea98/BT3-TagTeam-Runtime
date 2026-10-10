// Shared native mod UI assets and layouts for Vulkan and OpenGL.
inline std::vector<uint8_t> read(const std::filesystem::path& path, size_t maximum) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || uint64_t(input.tellg()) > maximum) return {};
    const auto length = size_t(input.tellg()); input.seekg(0);
    std::vector<uint8_t> data(length);
    if (length && !input.read(reinterpret_cast<char*>(data.data()), length)) return {};
    return data;
}
template<class BackendSink> struct UiState {
    BackendSink sink;
    NativeTextFont title, body, numeric, smallTitle, smallBody, uprightBody;
    struct Art {TextureHandle handle=0; unsigned width=0,height=0;};
    std::array<Art,253> portraits;
    std::array<std::string,253> names;
    Art banner,balls,wave,waveFrame,waveInterior,brandLogo,vs,row,help,chip,white,backdrop,teamCyan,teamOrange,spark,titlePlate,selectedRow,portraitBorder;
    unsigned ellipsized=0;
    std::array<Art,7> ballIcons;
    std::array<Art,5> seatIcons;
    std::array<Art,4> hudBars;
    Art hudGold,lockMarker,hudPlate,hudGlass,energyFill;
    std::array<std::array<Art,10>,3> timerDigits;
    HudSnapshot hud;
    bool hudEdges=false;
    uint64_t born=0,lastGeneration=0,lastFrame=0,fadeOut=0,stageBorn=0;
    uint8_t lastStage=0;
    float smoothProgress=0,progressFrom=0,progressTarget=0;
    uint64_t progressBorn=0;
    ScreenSnapshot frameState;
    bool product=false;
    bool creditsEnabled=false,creditsFull=true;
    uint64_t creditsBorn=0,creditsFade=0;
    bool benchmark=std::getenv("PS2X_NATIVE_UI_BENCH")!=nullptr;
    Localizer locale;
    bool valid = false;
    uint32_t w = 0, h = 0;
    std::array<GlyphQuad, 256> scratch;
    template<class Context> UiState(Context& device, const std::filesystem::path& assets) : sink(device) {
        auto load = [&](NativeTextFont& font, const char* stem) {
            auto metrics = read(assets/(std::string(stem)+".gatl"), 200000);
            GlyphAtlas layout;
            if (!layout.load(metrics)) return false;
            std::vector<std::vector<uint8_t>> pageData, palData;
            std::vector<std::span<const uint8_t>> pages, palettes;
            for (unsigned i=0; i<layout.pages(); ++i)
                pageData.push_back(read(assets/(std::string(stem)+"-"+std::to_string(i)+".indices"), 512*256));
            for (auto name : {"gold","white","grey","cyan","yellow","red","green"})
                palData.push_back(read(assets/(std::string(stem)+"-"+name+".rgba"), 1024));
            for (auto& b : pageData) pages.emplace_back(b);
            for (auto& b : palData) palettes.emplace_back(b);
            return font.load(sink, metrics, pages, palettes);
        };
        valid = sink.program && load(title,"title-26") && load(body,"body-16") && load(numeric,"numeric-14");
        valid=valid && load(smallTitle,"title-18") && load(smallBody,"body-12");
        product=std::getenv("PS2X_NATIVE_UI_SLICE")!=nullptr;
        if(product)valid=valid && load(uprightBody,"body-upright-12");
        creditsEnabled=product && std::getenv("PS2X_NATIVE_UI_BOOT_CREDITS")!=nullptr;
        creditsFull=!(std::getenv("PS2X_NATIVE_UI_BOOT_CREDITS") && *std::getenv("PS2X_NATIVE_UI_BOOT_CREDITS")=='0');
        auto art=[&](const char* name) {
            auto bytes=read(assets/(std::string(name)+".rgba"),4*1024*1024);
            Art result; if(bytes.size()<8)return result;
            std::memcpy(&result.width,bytes.data(),4);std::memcpy(&result.height,bytes.data()+4,4);
            if(!result.width || !result.height || result.width>2048 || result.height>2048 ||
                bytes.size()!=8+size_t(result.width)*result.height*4)return Art{};
            result.handle=sink.uploadRgba({bytes.data()+8,bytes.size()-8},result.width,result.height);
            return result;
        };
        if(product) {
            banner=art("banner");balls=art("balls");wave=art("wave");vs=art("vs");
            waveFrame=art("wave-frame");waveInterior=art("wave-interior");
            brandLogo=art("brand-logo");valid=valid && brandLogo.handle;
            for(unsigned i=0;i<5;++i){seatIcons[i]=art(("seat-"+std::to_string(i)).c_str());valid=valid && seatIcons[i].handle;}
            const char* barNames[]={"green","blue","grey","red"};
            hudGold=art("hud-gold");lockMarker=art("lock-marker");hudPlate=art("hud-flat-plate");valid=valid && hudGold.handle && lockMarker.handle && hudPlate.handle;
            for(unsigned i=0;i<4;++i){hudBars[i]=art((std::string("hud-bar-")+barNames[i]).c_str());valid=valid && hudBars[i].handle;}
            const char* timerNames[]={"grey","yellow","red"};
            for(unsigned i=0;i<3;++i)for(unsigned d=0;d<10;++d){timerDigits[i][d]=art((std::string("timer-")+timerNames[i]+"-"+std::to_string(d)).c_str());valid=valid && timerDigits[i][d].handle;}
            selectedRow=art("selected-row");teamCyan=art("team-cyan");teamOrange=art("team-orange");spark=art("spark");titlePlate=art("title-plate");
            for(unsigned i=0;i<7;++i){std::string name="ball-"+std::to_string(i);ballIcons[i]=art(name.c_str());}
            row=art("row-plate-9slice");help=art("help-plate-9slice");chip=art("value-chip");
            for(auto icon:ballIcons)valid=valid && icon.handle;
            valid=valid && waveFrame.handle && waveInterior.handle && selectedRow.handle && teamCyan.handle && teamOrange.handle && spark.handle && titlePlate.handle && banner.handle && balls.handle && wave.handle && vs.handle && row.handle && help.handle && chip.handle;
            for(unsigned i=0;i<253;++i) {char stem[32];std::snprintf(stem,sizeof(stem),"portrait-%03u",i);portraits[i]=art(stem);valid=valid && portraits[i].handle;}
            auto bytes=read(assets/"names.bin",100000);size_t at=0;
            for(auto& name:names) {if(at+2>bytes.size()){valid=false;break;}uint16_t n=uint16_t(bytes[at]|(bytes[at+1]<<8));at+=2;
                if(at+n>bytes.size()){valid=false;break;}name.assign(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;}
            std::array<uint8_t,4> pixel{255,255,255,255};white={sink.uploadRgba(pixel,1,1),1,1};
            pixel={4,7,12,255};portraitBorder={sink.uploadRgba(pixel,1,1),1,1};
            pixel={18,32,46,255};hudGlass={sink.uploadRgba(pixel,1,1),1,1};
            std::vector<uint8_t> gradient(448*4);
            for(unsigned y=0;y<448;++y) {float t=float(y)/447;gradient[y*4]=uint8_t(14+10*t);gradient[y*4+1]=uint8_t(20+34*t);gradient[y*4+2]=uint8_t(40+30*t);gradient[y*4+3]=255;}
            backdrop={sink.uploadRgba(gradient,1,448),1,448};
            std::array<uint8_t,18*4> energy{};
            for(unsigned y=0;y<18;++y) {
                const float light=1.f-std::abs(float(y)-7.f)/11.f;
                energy[y*4]=uint8_t(18+35*light);energy[y*4+1]=uint8_t(82+110*light);
                energy[y*4+2]=uint8_t(119+112*light);energy[y*4+3]=255;
            }
            energyFill={sink.uploadRgba(energy,1,18),1,18};
        }
        uiStore().rendererReady=valid;
        if (const char* language = std::getenv("PS2X_NATIVE_UI_LANGUAGE")) locale.select(language);
        if(creditsEnabled)uiStore().lifecycle.language(locale.language());
        std::fprintf(stderr,"[nativeui] assets %s; uploads=%u language=%.*s\n",valid?"ready":"FAILED",sink.uploads,int(locale.tag().size()),locale.tag().data());
    }
    #include "ui_views.inc"
};
