#include "runtime/ps2_ui_vulkan.h"
#if defined(PS2X_HAVE_PGS)
#include "runtime/ps2_ui_text_draw.h"
#include "runtime/ps2_ui_transport.h"
#include "runtime/ps2_ui_hud.h"
#include "runtime/ps2_ui_overhead_layout.h"
#include "runtime/ps2_wshud.h"
#include <algorithm>
#include <cmath>
#include <string>
#include "context.hpp"
#include "command_buffer.hpp"
#include "query_pool.hpp"
#include "thread_id.hpp"
#include "gfx/image_io.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace ps2x::ui {
namespace {
static const uint32_t vert[] = {
#include "seamvk/ui_text.vert.inc"
};
static const uint32_t frag[] = {
#include "seamvk/ui_text.frag.inc"
};
struct Push { float uv[4], rect[4], options[4], gauges[4], detail[4]; };
class Sink final : public TextSpriteSink {
    Vulkan::Device& device_;
    std::vector<Vulkan::ImageHandle> images_{1};
public:
    Vulkan::CommandBuffer* cmd = nullptr;
    Vulkan::Program* program = nullptr;
    uint32_t width = 0, height = 0;
    float virtualWidth=0, virtualHeight=0, opacity=1;
    unsigned uploads = 0, draws = 0;bool clipped=false;Rect clip{};
    explicit Sink(Vulkan::Device& device) : device_(device) {
        Vulkan::ResourceLayout vl{}, fl{};
        vl.output_mask = 1; vl.push_constant_size = sizeof(Push);
        fl.input_mask = 1; fl.output_mask = 1; fl.push_constant_size = sizeof(Push);
        fl.sets[0].sampled_image_mask = 3;
        fl.sets[0].meta[0].array_size = fl.sets[0].meta[1].array_size = 1;
        program = device_.request_program(vert, sizeof(vert), frag, sizeof(frag), &vl, &fl);
    }
    TextureHandle upload(std::span<const uint8_t> bytes, unsigned w, unsigned h, VkFormat format) {
        Vulkan::ImageInitialData data{bytes.data(), 0, 0};
        auto image = device_.create_image(Vulkan::ImageCreateInfo::immutable_2d_image(w, h, format), &data);
        if (!image) return 0;
        images_.push_back(image); ++uploads; return images_.size() - 1;
    }
    TextureHandle uploadIndices(std::span<const uint8_t> b, uint16_t w, uint16_t h) override {
        return upload(b, w, h, VK_FORMAT_R8_UNORM);
    }
    TextureHandle uploadPalette(std::span<const uint8_t> b) override {
        return upload(b, 256, 1, VK_FORMAT_R8G8B8A8_UNORM);
    }
    void release(TextureHandle h) override { if (h < images_.size()) images_[h].reset(); }
    void begin(Rect r) override {
        if(clipped){const float right=std::min(r.x+r.width,clip.x+clip.width),bottom=std::min(r.y+r.height,clip.y+clip.height);
            r.x=std::max(r.x,clip.x);r.y=std::max(r.y,clip.y);r.width=std::max(0.f,right-r.x);r.height=std::max(0.f,bottom-r.y);}
        VkRect2D sc{}; sc.offset.x = int32_t(r.x*width/virtualWidth); sc.offset.y = int32_t(r.y*height/virtualHeight);
        sc.extent.width = uint32_t(r.width*width/virtualWidth); sc.extent.height = uint32_t(r.height*height/virtualHeight);
        cmd->set_scissor(sc);
        cmd->set_blend_enable(true);
        cmd->set_blend_factors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        cmd->set_blend_op(VK_BLEND_OP_ADD);
    }
    void sprite(const TextSprite& s) override {
        cmd->set_texture(0, 0, images_[s.indices]->get_view(), Vulkan::StockSampler::NearestClamp);
        cmd->set_texture(0, 1, images_[s.palette]->get_view(), Vulkan::StockSampler::NearestClamp);
        Push pc{{s.u0, s.v0, s.u1, s.v1},
                {2*s.x0/virtualWidth-1, 2*s.y0/virtualHeight-1, 2*s.x1/virtualWidth-1, 2*s.y1/virtualHeight-1}, {1,opacity,1,0}};
        cmd->push_constants(&pc, 0, sizeof(pc)); cmd->draw(4); ++draws;
    }
    void rgba(TextureHandle handle, Rect box, float alpha=1, float brightness=1,
              float u0=0,float v0=0,float u1=1,float v1=1) {
        if(!handle || handle>=images_.size())return;
        cmd->set_texture(0,0,images_[handle]->get_view(),Vulkan::StockSampler::LinearClamp);
        cmd->set_texture(0,1,images_[handle]->get_view(),Vulkan::StockSampler::NearestClamp);
        Push pc{{u0,v0,u1,v1},{2*box.x/virtualWidth-1,2*box.y/virtualHeight-1,
            2*(box.x+box.width)/virtualWidth-1,2*(box.y+box.height)/virtualHeight-1},{0,alpha*opacity,brightness,0}};
        cmd->push_constants(&pc,0,sizeof(pc));cmd->draw(4);++draws;
    }
    void end() override {
        VkRect2D full{}; full.extent = {width, height}; cmd->set_scissor(full);
        cmd->set_blend_enable(false);
    }
    void gauge(TextureHandle portrait,Rect box,unsigned shape,unsigned relation,float hp,float ki,float fill,bool portraits,float fade) {
        if(!portrait || portrait>=images_.size())return;
        cmd->set_texture(0,0,images_[portrait]->get_view(),Vulkan::StockSampler::LinearClamp);
        cmd->set_texture(0,1,images_[portrait]->get_view(),Vulkan::StockSampler::NearestClamp);
        Push pc{{0,0,1,1},{2*box.x/virtualWidth-1,2*box.y/virtualHeight-1,
            2*(box.x+box.width)/virtualWidth-1,2*(box.y+box.height)/virtualHeight-1},
            {shape==1?2.f:3.f,fade*opacity,float(relation),0},
            {hp,ki,0,0},{portraits?1.f:0.f,std::min(.45f,fill),3.f/std::max(24.f,box.width),0}};
        cmd->push_constants(&pc,0,sizeof(pc));cmd->draw(4);++draws;
    }
};
std::vector<uint8_t> read(const std::filesystem::path& path, size_t maximum) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || uint64_t(input.tellg()) > maximum) return {};
    const auto length = size_t(input.tellg()); input.seekg(0);
    std::vector<uint8_t> data(length);
    if (length && !input.read(reinterpret_cast<char*>(data.data()), length)) return {};
    return data;
}
struct State {
    Sink sink;
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
    Vulkan::QueryPoolHandle gpuStart,gpuEnd;
    Localizer locale;
    Vulkan::ImageHandle output;
    bool valid = false;
    uint32_t w = 0, h = 0;
    VkImageLayout outputLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    std::array<GlyphQuad, 256> scratch;
    State(Vulkan::Device& device, const std::filesystem::path& assets) : sink(device) {
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
            result.handle=sink.upload({bytes.data()+8,bytes.size()-8},result.width,result.height,VK_FORMAT_R8G8B8A8_UNORM);
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
            std::array<uint8_t,4> pixel{255,255,255,255};white={sink.upload(pixel,1,1,VK_FORMAT_R8G8B8A8_UNORM),1,1};
            pixel={4,7,12,255};portraitBorder={sink.upload(pixel,1,1,VK_FORMAT_R8G8B8A8_UNORM),1,1};
            pixel={18,32,46,255};hudGlass={sink.upload(pixel,1,1,VK_FORMAT_R8G8B8A8_UNORM),1,1};
            std::vector<uint8_t> gradient(448*4);
            for(unsigned y=0;y<448;++y) {float t=float(y)/447;gradient[y*4]=uint8_t(14+10*t);gradient[y*4+1]=uint8_t(20+34*t);gradient[y*4+2]=uint8_t(40+30*t);gradient[y*4+3]=255;}
            backdrop={sink.upload(gradient,1,448,VK_FORMAT_R8G8B8A8_UNORM),1,448};
            std::array<uint8_t,18*4> energy{};
            for(unsigned y=0;y<18;++y) {
                const float light=1.f-std::abs(float(y)-7.f)/11.f;
                energy[y*4]=uint8_t(18+35*light);energy[y*4+1]=uint8_t(82+110*light);
                energy[y*4+2]=uint8_t(119+112*light);energy[y*4+3]=255;
            }
            energyFill={sink.upload(energy,1,18,VK_FORMAT_R8G8B8A8_UNORM),1,18};
        }
        uiStore().rendererReady=valid;
        if (const char* language = std::getenv("PS2X_NATIVE_UI_LANGUAGE")) locale.select(language);
        if(creditsEnabled)uiStore().lifecycle.language(locale.language());
        std::fprintf(stderr,"[nativeui] Vulkan assets %s; uploads=%u language=%.*s\n",valid?"ready":"FAILED",sink.uploads,int(locale.tag().size()),locale.tag().data());
    }
    #include "seamvk/ui_views.inc"
    Vulkan::ImageHandle render(Vulkan::Device& device, const Vulkan::ImageHandle& source) {
        if (!valid || !source) return source;
        if(product && !visible())return source;
        auto width=source->get_width(), height=source->get_height();
        if (!output || w!=width || h!=height) {
            auto ci=Vulkan::ImageCreateInfo::render_target(width,height,VK_FORMAT_R8G8B8A8_UNORM);
            ci.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            output=device.create_image(ci); w=width; h=height;
            outputLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        if (!output) return source;
        auto cmd=device.request_command_buffer();
        if(benchmark)gpuStart=cmd->write_timestamp(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
        if (outputLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            cmd->image_barrier(*output,outputLayout,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        Vulkan::RenderPassInfo pass{}; pass.num_color_attachments=1;
        pass.color_attachments[0]=&output->get_view(); pass.clear_attachments=1; pass.store_attachments=1;
        cmd->begin_render_pass(pass);
        cmd->set_opaque_state(); cmd->set_program(sink.program); cmd->set_cull_mode(VK_CULL_MODE_NONE);
        cmd->set_depth_test(false,false); cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        VkViewport vp{}; vp.width=float(w); vp.height=float(h); vp.maxDepth=1; cmd->set_viewport(vp);
        VkRect2D sc{}; sc.extent={w,h}; cmd->set_scissor(sc);
        cmd->set_texture(0,0,source->get_view(),Vulkan::StockSampler::NearestClamp);
        cmd->set_texture(0,1,source->get_view(),Vulkan::StockSampler::NearestClamp);
        Push background{{0,0,1,1},{-1,-1,1,1},{0,1,1,0}};
        cmd->push_constants(&background,0,sizeof(background)); cmd->draw(4);
        sink.cmd=cmd.get(); sink.width=w; sink.height=h;
        uint32_t pw=0,ph=0;ps2x_wshud::presentSize(pw,ph);
        sink.virtualWidth=pw?float(pw):float(w);sink.virtualHeight=ph?float(ph):float(h);
        Rect viewport{0,0,sink.virtualWidth,sink.virtualHeight};
        if(product)drawProduct(viewport);
        else {
            title.draw(locale,TextId::ModdedModes,Palette::Gold,32,60,viewport,scratch);
            body.draw(locale,TextId::PreparingMatch,Palette::White,32,99,viewport,scratch);
            for(unsigned i=0;i<7;++i) {
                body.draw(locale,TextId::Fusion,Palette(i),32,148+i*32,viewport,scratch);
                body.draw(locale,TextId::Ready,Palette(i),180,148+i*32,viewport,scratch);
                numeric.draw("0123456789 : 25.0%",Palette(i),300,148+i*32,viewport,scratch);
            }
        }
        cmd->end_render_pass(); sink.cmd=nullptr;
        if(benchmark)gpuEnd=cmd->write_timestamp(VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        cmd->image_barrier(*output,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        device.submit(cmd);
        // PGS readback restores this layout, including when a ring slot is busy.
        outputLayout=VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        return output;
    }
};
std::unique_ptr<State> live;
bool attempted=false;
} // namespace
Vulkan::ImageHandle vulkanComposite(Vulkan::Device& device, const Vulkan::ImageHandle& source) {
    const char* assets=std::getenv("PS2X_NATIVE_UI_ASSETS");
    const char* enabled=std::getenv("PS2X_NATIVE_UI_TEST");
    if (!assets || !*assets || !enabled || *enabled!='1') return source;
    if (!attempted) { attempted=true; live=std::make_unique<State>(device,assets); }
    return live?live->render(device,source):source;
}
void vulkanUiFrameStamp(uint64_t& generation,uint64_t& revision) {generation=live?live->frameState.generation:0;revision=live?live->frameState.revision:0;}
void vulkanUiShutdown() { live.reset(); attempted=false; }
} // namespace ps2x::ui

int ps2xNativeUiVulkanSelfTest(const char* assets, const char* output) {
    using namespace Vulkan;
    Util::register_thread_index(0);
    if (!Context::init_loader(nullptr)) return 2;
    Context context; context.set_num_thread_indices(1);
    if (!context.init_instance_and_device(nullptr,0,nullptr,0)) return 3;
    Device device; device.set_context(context); device.init_frame_contexts(4);
    {
        ps2x::ui::State state(device,assets);
        if (!state.valid) return 4;
        std::filesystem::create_directories(output);
        std::vector<uint8_t> pixels(640*448*4);
        for (size_t i=0;i<pixels.size();i+=4) { pixels[i]=18; pixels[i+1]=23; pixels[i+2]=32; pixels[i+3]=255; }
        ImageInitialData init{pixels.data(),0,0};
        auto background=device.create_image(ImageCreateInfo::immutable_2d_image(640,448,VK_FORMAT_R8G8B8A8_UNORM),&init);
        const unsigned uploads=state.sink.uploads;
        struct Case {std::string name; const char* language; unsigned one=0,two=0,kind=0,mode=0,humans=1;unsigned wide=0;};
        std::vector<Case> cases;
        if(state.product) {
            for(auto language:{"en","es"}) {
                for(unsigned one=1;one<=5;++one)for(unsigned two=1;two<=5;++two)
                    cases.push_back({std::string("loading-")+language+"-"+std::to_string(one)+"v"+std::to_string(two),language,one,two,0});
                for(unsigned kind=1;kind<=3;++kind)cases.push_back({std::string(kind==1?"settings-":kind==2?"about-":"failure-")+language,language,5,5,kind});
                for(unsigned one=2;one<=5;++one)for(unsigned two=1;two<=3;++two)
                    cases.push_back({std::string("coop-")+language+"-"+std::to_string(one)+"v"+std::to_string(two),language,one,two,6,1,std::min(one,4u)});
                for(unsigned total=2;total<=10;++total)cases.push_back({std::string("ffa-")+language+"-"+std::to_string(total),language,std::min(total-1,5u),total-std::min(total-1,5u),6,2,std::min(total,4u)});
                cases.push_back({std::string("training-")+language+"-1v1",language,1,1,6,3,1});
                cases.push_back({std::string("training-coop-")+language+"-2v1",language,2,1,6,4,2});
                cases.push_back({std::string("exhibition-")+language,language,5,5,6,0,0});
                cases.push_back({std::string("credits-full-")+language,language,0,0,7});
                cases.push_back({std::string("credits-strip-")+language,language,0,0,8});
                cases.push_back({std::string("credits-first-")+language,language,0,0,23});
                for(unsigned wide:{0u,1u,2u})for(unsigned kind=9;kind<=22;++kind)
                    cases.push_back({std::string("hud-")+language+"-"+std::to_string(kind)+(wide==2?"-4x3":wide==1?"-21x9":"-16x9"),language,5,5,kind,kind==10?2u:kind==11?3u:0u,4,wide});
                for(unsigned wide:{0u,1u})for(unsigned kind:{25u,26u,27u})
                    cases.push_back({std::string("fusion-native-")+language+"-"+std::to_string(kind)+(wide?"-21x9":"-16x9"),language,5,5,kind,0,4,wide});
                for(unsigned wide:{0u,1u})for(unsigned kind:{28u,29u,30u})
                    cases.push_back({std::string("revive-native-")+language+"-"+std::to_string(kind)+(wide?"-21x9":"-16x9"),language,5,5,kind,0,2,wide});
                for(unsigned kind:{31u,32u,33u,34u})cases.push_back({std::string("v11-prompts-")+language+"-"+std::to_string(kind),language,5,5,kind,0,4,kind==32 || kind==34?1u:0u});
            }
        } else for(auto language:{"en","es"})cases.push_back({std::string("vulkan-ui-")+language,language});
        if(state.product)cases.push_back({"aspect-en","en",5,5,4});
        if(state.product)cases.push_back({"ellipsis-en","en",5,5,5});
        // Offline captures use packets emitted by the real Python menu model,
        // not a second C++ approximation of its labels and pagination.
        std::vector<uint8_t> menuFixtures;
        constexpr size_t menuRecord=64+16+2048;
        if(const char* path=std::getenv("PS2X_UI_MENU_FIXTURES")) {
            menuFixtures=ps2x::ui::read(path,2*1024*1024);
            if(menuFixtures.empty() || menuFixtures.size()%menuRecord)return 27;
            for(size_t at=0;at<menuFixtures.size();at+=menuRecord) {
                const auto* record=menuFixtures.data()+at;
                if(!std::memchr(record,0,64))return 27;
                uint32_t meta[4];std::memcpy(meta,record+64,16);
                if(meta[0]>1 || meta[1]>1 || meta[2]>8 || meta[3]>=std::max(1u,meta[2]))return 27;
                cases.push_back({reinterpret_cast<const char*>(record),meta[0]?"es":"en",unsigned(at/menuRecord),0,24});
            }
        }
        uint64_t generation=100;
        for(const auto& test:cases) {
            if(std::getenv("PS2X_V11_HUD_TEST_ONLY") && test.kind<31)continue;
            if(std::getenv("PS2X_FUSION_HUD_TEST_ONLY") && (test.kind<25 || test.kind>=28))continue;
            if(std::getenv("PS2X_REVIVE_HUD_TEST_ONLY") && test.kind<28)continue;
            if(std::getenv("PS2X_NATIVE_UI_TEST_MENUS_ONLY") && test.kind!=24)continue;
            if(std::getenv("PS2X_NATIVE_UI_TEST_HUD_ONLY") && test.kind<9 && test.kind!=2 && test.kind!=7 && test.kind!=8)continue;
            const char* language=test.language;
            unsigned beforeEllipsis=state.ellipsized;
            if(test.kind==5)state.names[0]="ABCDEFGHIJKLMNOPQRSTUVWX";
            state.locale.select(language);
            auto& store=ps2x::ui::uiStore();auto& lifecycle=store.lifecycle;
            if(state.product) {
                ps2x_wshud::setPresentSize(test.wide==2?1440:test.kind==4 || test.wide==1?2560:test.kind>=9 && test.kind<35?1920:0,test.kind==4 || (test.kind>=9 && test.kind<35)?1080:0);
                {auto& hs=ps2x::ui::hudStore();std::lock_guard lock(hs.mutex);hs.state={};}
                auto previous=lifecycle.snapshot(ps2x::ui::uiNow());
                if(previous.screen==ps2x::ui::Screen::Loading)lifecycle.teardown(previous.generation,ps2x::ui::uiNow());
                else if(previous.screen!=ps2x::ui::Screen::Hidden)lifecycle.close(ps2x::ui::uiNow());
                lifecycle.language(state.locale.language());
                if(test.kind==7 || test.kind==8 || test.kind==23) {
                    state.creditsEnabled=true;state.creditsFull=test.kind!=8;state.creditsBorn=ps2x::ui::uiNow()-(test.kind==23?0:test.kind==7?2000:500);state.creditsFade=0;store.creditsSkip=false;
                } else if(test.kind==24) {
                    state.creditsEnabled=false;
                    const auto* record=menuFixtures.data()+test.one*menuRecord;
                    uint32_t meta[4];std::memcpy(meta,record+64,16);
                    lifecycle.open(meta[1]?ps2x::ui::Screen::About:ps2x::ui::Screen::Settings,ps2x::ui::uiNow());
                    std::lock_guard lock(store.mutex);store.menu={};
                    store.menu.rows=meta[2];store.menu.selected=meta[3];
                    std::memcpy(store.menu.text.data(),record+80,2048);
                    // Only bounded, NUL terminated production packet fields.
                    for(unsigned i=0;i<8;++i){store.menu.text[128+i*192+95]=0;store.menu.text[224+i*192+95]=0;}
                    store.menu.text[127]=store.menu.text[1919]=store.menu.text[2047]=0;
                } else if(test.kind==1 || test.kind==2) {
                    lifecycle.open(test.kind==1?ps2x::ui::Screen::Settings:ps2x::ui::Screen::About,ps2x::ui::uiNow());
                    std::lock_guard lock(store.mutex);store.menu={};
                    auto set=[&](size_t offset,std::string_view value,size_t capacity) {auto n=std::min(capacity-1,value.size());std::memcpy(store.menu.text.data()+offset,value.data(),n);};
                    set(0,state.locale.text(test.kind==1?ps2x::ui::TextId::Settings:ps2x::ui::TextId::About),128);
                    store.menu.rows=test.kind==1?4:6;store.menu.selected=1;
                    if(test.kind==1) {
                        ps2x::ui::TextId ids[]={ps2x::ui::TextId::Language,ps2x::ui::TextId::Resolution,ps2x::ui::TextId::AspectRatio,ps2x::ui::TextId::Fullscreen};
                        std::string_view values[]={state.locale.text(state.locale.language()==ps2x::ui::Language::Spanish?ps2x::ui::TextId::Spanish:ps2x::ui::TextId::English),"1920 x 1080","21:9",state.locale.text(ps2x::ui::TextId::Yes)};
                        for(unsigned i=0;i<4;++i){set(128+i*192,state.locale.text(ids[i]),96);set(224+i*192,values[i],96);}
                    } else {
                        set(128,"Power Scale: LetsPlayBt3",96);set(320,state.locale.text(ps2x::ui::TextId::PowerScaleLink),96);
                        set(512,"Tag Team: The Mufti",96);set(704,state.locale.text(ps2x::ui::TextId::TagTeamLink),96);
                        const auto thanks=state.locale.text(ps2x::ui::TextId::SpecialThanks);const auto split=thanks.rfind(' ',54);
                        set(896,thanks.substr(0,split),96);set(1088,thanks.substr(split+1),96);
                    }
                    set(1664,state.locale.text(ps2x::ui::TextId::MenuHelp),256);
                } else {
                    std::array<uint16_t,5> one{0,161,202,252,50},two{109,117,133,70,100};
                    if(!lifecycle.begin(++generation,ps2x::ui::uiNow(),{one.data(),test.one},{two.data(),test.two}))return 6;
                    std::array<uint8_t,10> seats{};
                    if(test.mode==1 || test.mode==4)for(unsigned i=0;i<test.humans;++i)seats[i]=uint8_t(i+1);
                    else if(test.mode==2)for(unsigned i=0;i<test.humans;++i)seats[i]=uint8_t(i+1);
                    else for(unsigned i=0;i<test.humans;++i) {unsigned side=i%2,slot=i/2;seats[side?test.one+slot:slot]=uint8_t(i+1);}
                    if(!lifecycle.matchDetails(generation,ps2x::ui::BattleMode(test.mode),uint8_t(test.humans),seats))return 20;
                    // Fast-preparation acceptance: no composited image before frame12.
                    if(state.render(device,background).get()!=background.get())return 7;
                    state.born=ps2x::ui::uiNow()-1500;
                    lifecycle.progress(generation,78,5,ps2x::ui::uiNow());state.smoothProgress=78;
                    if(test.kind==3)lifecycle.fail(generation,ps2x::ui::uiNow());
                    if(test.kind>=9) {
                        if(!lifecycle.ready(generation,ps2x::ui::uiNow()))return 21;
                        // Two-fighter cases retain Ready with no controller ACK:
                        // an active native fight must still draw its overheads.
                        if(test.kind!=9 && !lifecycle.released(generation,ps2x::ui::uiNow()))return 21;
                        auto& hs=ps2x::ui::hudStore();std::lock_guard lock(hs.mutex);auto& h=hs.state;
                        h.active=true;h.generation=generation;h.mode=ps2x::ui::BattleMode(test.mode);h.target=1;h.subject=0;
                        for(unsigned i=0;i<10;++i){auto& a=h.actors[i];a.present=true;a.alive=i!=9;a.character=uint16_t(i==0?0:i==1?202:70+i);a.hp=i==9?0:i%2?8000:32000;a.maxHp=40000;a.ki=3000;a.maxKi=5000;a.seat=i<4?uint8_t(i+1):0;}
                        h.actors[0].fusionSeconds=test.kind==12?7:test.kind==13?2:0;h.actors[0].fusionTotal=60;
                        if(test.kind>=25){auto& a=h.actors[0];a.fusionSeconds=test.kind==26?2:7;a.fusionTotal=100;
                            a.fusionLife=test.kind==26?.04f:.14f;a.fusionFast=true;
                            a.fused=true;a.showSwap=a.showFusionOwner=true;a.swapSeconds=4;a.swapTotal=5;a.attackSeat=1;a.moveSeat=3;h.actors[1].fusePrompt=true;h.showGameHud=false;}
                        h.training=test.kind==11;h.refill=true;h.idle=true;h.hits=7;h.damage=23456;
                        h.cinematic=test.kind==14;
                        h.viewCount=test.kind==12?2:test.kind==13?4:1;
                        if(test.kind>=25)h.viewCount=test.kind==25?4:test.kind==26?2:1;
                        const unsigned actorCount=test.kind==9?2:test.kind==11 || test.kind==12?6:10;
                        h.showGameHud=test.kind!=10 && test.kind!=13;
                        h.preferences.portraits=test.kind!=12;h.preferences.names=test.kind==11?2:1;
                        h.preferences.names=test.kind==11?2:test.kind==13?1:0;
                        h.preferences.shape=test.kind==10 || test.kind==12 || test.kind==17?2:test.kind==15?3:test.kind==16?0:1;
                        if(test.kind>=25)h.preferences.shape=test.kind==25?1:test.kind==26?2:0;
                        h.preferences.scale=test.kind==12?50:test.kind==13?120:65;
                        h.preferences.opacity=test.kind==12?30:test.kind==13?70:50;
                        if(test.kind==17){h.preferences.scale=120;h.preferences.opacity=100;h.showGameHud=false;
                            h.preferences.kiPips=h.preferences.fusion=false;h.actors[0].stocks=10;h.actors[0].fusionSeconds=9;
                            h.actors[0].transforming=true;h.actors[0].hp=h.actors[0].maxHp;h.actors[0].ki=0;h.actors[1].charging=true;}
                        for(unsigned i=0;i<10;++i){h.actors[i].present=i<actorCount;h.actors[i].stocks=uint8_t(i%4);}
                        if(test.kind==17)h.actors[0].stocks=10;
                        for(unsigned v=0;v<h.viewCount;++v){auto& view=h.views[v];view.valid=true;view.subject=uint8_t(v);view.target=uint8_t((v+1)%actorCount);view.seat=uint8_t(v+1);
                            if(h.viewCount==2){view.x=v*257.f;view.width=255;}
                            if(h.viewCount==4){view.x=(v%2)*257.f;view.y=(v/2)*225.f;view.width=255;view.height=223;}
                            for(unsigned i=0;i<actorCount;++i){auto& point=view.points[i];point.valid=true;point.onScreen=true;
                                point.x=view.x+view.width*(.15f+.15f*float(i%5));point.y=view.y+view.height*(.48f+.15f*float(i/5));point.scale=i%2?.8f:1.1f;}
                            if(test.kind==9){view.points[0].x=200;view.points[1].x=205;view.points[0].y=view.points[1].y=220;}
                            if(test.kind==10){view.points[1].x=560;view.points[1].onScreen=false;}
                        }
                        if(test.kind>=28 && test.kind<=30){
                            h.preferences.shape=test.kind==28?1:test.kind==29?2:3;h.mode=ps2x::ui::BattleMode::Coop;
                            h.viewCount=2;h.views[0].subject=0;h.views[1].subject=2;
                            for(unsigned v=0;v<2;++v){auto& view=h.views[v];view.x=v*257.f;view.width=255;view.height=448;
                                for(unsigned i=0;i<10;++i){view.points[i].x=view.x+view.width*(.12f+.16f*(i%5));view.points[i].y=180+90*(i/5);}}
                            h.actors[2].alive=false;h.actors[0].seat=1;
                            h.revives[0]={0,2,1,2,.62f,.5f,.5f,true};
                            h.revives[4]={4,6,2,3,0,.5f,0,true};h.actors[6].alive=false;
                            h.revives[8]={8,255,4,1,1,.5f,0,true};
                        }
                        if(test.kind>=31){
                            h.viewCount=test.kind==32?2:test.kind==33?4:1;h.views={};h.revives={};h.kills={};
                            h.preferences.shape=1;h.preferences.scale=65;h.preferences.opacity=50;
                            for(auto& a:h.actors){a.fused=a.fusePrompt=false;a.fusionSeconds=0;}
                            for(unsigned v=0;v<h.viewCount;++v){auto& view=h.views[v];view.valid=true;view.subject=uint8_t(v);view.seat=uint8_t(v+1);
                                if(h.viewCount==2){view.x=v*257.f;view.width=255;}
                                if(h.viewCount==4){view.x=(v%2)*257.f;view.y=(v/2)*225.f;view.width=255;view.height=223;}
                                for(unsigned i=0;i<10;++i){auto& p=view.points[i];p.valid=p.onScreen=true;
                                    p.x=view.x+view.width*(.15f+.17f*(i%5));p.y=view.y+view.height*(.4f+.3f*(i/5));p.scale=.8f;}
                                auto& p=h.prompts[v];p.target=uint8_t((v+1)%4);p.targetStyle=3;p.threats=0x1a;p.warnings=0x12;p.blink=v%2==0;
                                const bool es=std::strcmp(language,"es")==0;
                                const char* caption=test.kind==31?(es?"R3 - ASISTENCIA DE RAYO":"R3 - BEAM ASSIST"):
                                    test.kind==32?(es?"ASISTENCIA FALLIDA - BLOQUEADO":"ASSIST FAILED - BLOCKED"):
                                    (es?"ASISTENCIA DE RAYO X1.5":"BEAM ASSIST X1.5");
                                std::snprintf(p.beamText.data(),p.beamText.size(),"%s",caption);p.beamWarning=test.kind==32;
                                if(test.kind==34){view.points[1].onScreen=false;view.points[1].x=view.x+view.width+90;
                                    view.points[3].onScreen=false;view.points[3].y=view.y-120;}
                            }
                        }
                        if(test.kind==18 || test.kind==19 || test.kind==20){
                            h.preferences.detail=3;h.preferences.friends=h.preferences.enemies=false;
                            h.preferences.shape=test.kind==19?2:1;h.preferences.list=false;h.mode=ps2x::ui::BattleMode::FreeForAll;
                            for(auto& actor:h.actors)actor.alive=true;
                            h.paused=test.kind==20;
                        }
                        if(test.kind==21 || test.kind==22){h.active=false;h.introStage=test.kind==21?1:2;}
                        h.kills[0]={202,70,true,true};h.kills[1]={0,117,true,true};h.kills[2]={0,252,false,true};
                        state.fadeOut=ps2x::ui::uiNow()-400;
                    }
                }
            }
            auto renderStart=std::chrono::steady_clock::now();
            auto image=state.render(device,background);
            if(test.kind>=25 && test.kind<=27){
                // Model the next frame's visibility query before guest text
                // submission. It must not withdraw an acknowledged live HUD.
                if(ps2x::ui::hudStore().presentedGeneration.load()!=generation || !state.visible() ||
                   ps2x::ui::hudStore().presentedGeneration.load()!=generation)return 24;
            }
            if(test.kind>=9 && test.kind<23 && test.kind!=20 && ps2x::ui::hudStore().presentedGeneration.load()!=generation)return 23;
            auto renderMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count();
            std::fprintf(stderr,"[nativeui] CPU compose %s %.3f ms\n",test.name.c_str(),renderMs);
            BufferCreateInfo bi{}; bi.size=pixels.size(); bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT; bi.domain=BufferDomain::CachedHost;
            auto buffer=device.create_buffer(bi); auto cmd=device.request_command_buffer();
            cmd->image_barrier(*image,VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,0,VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
            cmd->copy_image_to_buffer(*buffer,*image,0,{}, {640,448,1},0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1});
            cmd->image_barrier(*image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
            Fence fence; device.submit(cmd,&fence); fence->wait();
            if(state.benchmark && state.gpuStart && state.gpuEnd) {
                uint64_t start=0,end=0;
                const auto& api=device.get_device_table();
                auto flags=VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT;
                auto a=api.vkGetQueryPoolResults(device.get_device(),state.gpuStart->get_query_pool(),state.gpuStart->get_query_pool_index(),1,8,&start,8,flags);
                auto b=api.vkGetQueryPoolResults(device.get_device(),state.gpuEnd->get_query_pool(),state.gpuEnd->get_query_pool_index(),1,8,&end,8,flags);
                if(a!=VK_SUCCESS || b!=VK_SUCCESS || end<start)return 15;
                double ms=double(end-start)*device.get_gpu_properties().limits.timestampPeriod/1000000;
                std::fprintf(stderr,"[nativeui] GPU compose %s %.3f ms\n",test.name.c_str(),ms);
            }
            auto mapped=static_cast<const uint8_t*>(device.map_host_buffer(*buffer,MEMORY_ACCESS_READ_BIT));
            size_t changed=0;
            for (size_t i=0;i<pixels.size();i+=4)
                if (mapped[i]!=18 || mapped[i+1]!=23 || mapped[i+2]!=32) ++changed;
            if(test.kind==20 && changed)return 24;
            if(test.kind==23 && changed<640*448*9/10)return 26;
            if((test.kind==21 || test.kind==22) && ps2x::ui::hudStore().presentedIntroStage.load()!=test.kind-20)return 25;
            auto file=std::filesystem::path(output)/(test.name+".png");
            bool saved=ps2x::gfx::GsWritePngRGBA8(file.string().c_str(),mapped,640,448);
            device.unmap_host_buffer(*buffer,MEMORY_ACCESS_READ_BIT);
            if(state.product && test.kind==0 && test.one==5 && test.two==5) {
                if(!lifecycle.progress(generation,100,6,ps2x::ui::uiNow()))return 8;
                if(state.render(device,background).get()==background.get())return 9;
                if(!lifecycle.ready(generation,ps2x::ui::uiNow()) || state.render(device,background).get()==background.get())return 10;
                if(!lifecycle.startAccepted(generation,ps2x::ui::uiNow()))return 17;
                if(state.render(device,background).get()==background.get())return 18;
                state.fadeOut=ps2x::ui::uiNow()-400;
                if(state.render(device,background).get()!=background.get() ||
                   lifecycle.snapshot(ps2x::ui::uiNow()).phase!=ps2x::ui::PreparationPhase::Ready)return 19;
                if(!lifecycle.released(generation,ps2x::ui::uiNow()))return 11;
                state.render(device,background);state.fadeOut=ps2x::ui::uiNow()-400;
                if(state.render(device,background).get()!=background.get())return 12;
                if(lifecycle.progress(generation-1,100,6,ps2x::ui::uiNow()))return 13;
                std::fprintf(stderr,"[nativeui] release/fade/stale/rematch lifecycle PASS\n");
            }
            if(test.kind==5 && state.ellipsized<=beforeEllipsis)return 16;
            if(test.kind==7 || test.kind==8 || test.kind==23) {
                store.creditsSkip=true;state.creditsFade=ps2x::ui::uiNow()-400;
                if(state.render(device,background).get()!=background.get())return 22;
                store.creditsSkip=false;
            }
            if(state.product && test.kind==3 && lifecycle.released(generation,ps2x::ui::uiNow()))return 14;
            if (!saved || (test.kind!=20 && changed<(test.kind>=21?100:1000)) || state.sink.uploads!=uploads) return 5;
            std::fprintf(stderr,"[nativeui] GPU self-test %s PASS uploads=%u draws=%u changed_pixels=%zu output=%s\n",test.name.c_str(),uploads,state.sink.draws,changed,file.string().c_str());
        }
        device.wait_idle();
    }
    device.wait_idle(); return 0;
}
#else
int ps2xNativeUiVulkanSelfTest(const char*,const char*) { return 2; }
#endif
