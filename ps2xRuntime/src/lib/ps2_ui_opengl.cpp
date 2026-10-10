#include "runtime/ps2_ui_opengl.h"
#include "runtime/ps2_ui_text_draw.h"
#include "runtime/ps2_ui_transport.h"
#include "runtime/ps2_ui_hud.h"
#include "runtime/ps2_ui_overhead_layout.h"
#include "runtime/ps2_wshud.h"
#include "gfx/bt3gl_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace ps2x::ui {
namespace {
const char* fragment =
#include "seamvk/ui_text_gl.frag.inc"
;
class Sink final : public TextSpriteSink {
    std::vector<bt3Texture2D> images_{1};
    Shader shader_{};
    int options_=-1, gauges_=-1, detail_=-1, palette_=-1;
    void draw(TextureHandle h,Rect box,float mode,float alpha,float brightness,
              float u0=0,float v0=0,float u1=1,float v1=1,
              TextureHandle palette=0,const float* gauges=nullptr,const float* detail=nullptr) {
        if(!h || h>=images_.size() || !images_[h].id)return;
        // Uniforms are not part of raylib's queued draw state. Flush before
        // changing one so earlier glyphs retain their palette/opacity.
        bt3rlDrawRenderBatchActive();
        float opts[]={mode,alpha*opacity,brightness,0};
        bt3SetShaderValue(shader_,options_,opts,SHADER_UNIFORM_VEC4);
        if(gauges)bt3SetShaderValue(shader_,gauges_,gauges,SHADER_UNIFORM_VEC4);
        if(detail)bt3SetShaderValue(shader_,detail_,detail,SHADER_UNIFORM_VEC4);
        if(palette && palette<images_.size())bt3SetShaderValueTexture(shader_,palette_,images_[palette]);
        auto t=images_[h];
        bt3DrawTexturePro(t,{u0*t.width,v0*t.height,(u1-u0)*t.width,(v1-v0)*t.height},
                          {box.x,box.y,box.width,box.height},{0,0},0,WHITE);
        bt3rlDrawRenderBatchActive();++draws;
    }
public:
    bool program=false;
    float virtualWidth=0,virtualHeight=0,opacity=1;
    unsigned uploads=0,draws=0;bool clipped=false;Rect clip{};
    explicit Sink(int&) {
        shader_=bt3LoadShaderFromMemory(nullptr,fragment);
        options_=bt3GetShaderLocation(shader_,"options");
        gauges_=bt3GetShaderLocation(shader_,"gauges");
        detail_=bt3GetShaderLocation(shader_,"detail");
        palette_=bt3GetShaderLocation(shader_,"palette");
        program=shader_.id && options_>=0 && gauges_>=0 && detail_>=0 && palette_>=0;
    }
    ~Sink() {
        for(auto t:images_)if(t.id)bt3UnloadTexture(t);
        if(shader_.id)UnloadShader(shader_);
    }
    TextureHandle uploadRgba(std::span<const uint8_t> bytes,unsigned w,unsigned h) {
        bt3Image image{const_cast<uint8_t*>(bytes.data()),int(w),int(h),1,BT3_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        auto t=bt3LoadTextureFromImage(image);if(!t.id)return 0;
        bt3SetTextureFilter(t,TEXTURE_FILTER_BILINEAR);bt3SetTextureWrap(t,TEXTURE_WRAP_CLAMP);
        images_.push_back(t);++uploads;return images_.size()-1;
    }
    TextureHandle uploadIndices(std::span<const uint8_t> bytes,uint16_t w,uint16_t h) override {
        // RGBA conversion avoids differing GL_RED swizzles between drivers.
        std::vector<uint8_t> rgba(bytes.size()*4);
        for(size_t i=0;i<bytes.size();++i){rgba[i*4]=bytes[i];rgba[i*4+3]=255;}
        auto handle=uploadRgba(rgba,w,h);
        if(handle)bt3SetTextureFilter(images_[handle],TEXTURE_FILTER_POINT);
        return handle;
    }
    TextureHandle uploadPalette(std::span<const uint8_t> bytes) override {
        auto handle=uploadRgba(bytes,256,1);
        if(handle)bt3SetTextureFilter(images_[handle],TEXTURE_FILTER_POINT);
        return handle;
    }
    void release(TextureHandle h) override {
        if(h<images_.size() && images_[h].id){bt3UnloadTexture(images_[h]);images_[h]={};}
    }
    void begin(Rect r) override {
        if(clipped){float right=std::min(r.x+r.width,clip.x+clip.width),bottom=std::min(r.y+r.height,clip.y+clip.height);
            r.x=std::max(r.x,clip.x);r.y=std::max(r.y,clip.y);r.width=std::max(0.f,right-r.x);r.height=std::max(0.f,bottom-r.y);}
        bt3BeginScissorMode(int(r.x),int(r.y),int(r.width),int(r.height));
        bt3BeginBlendMode(BLEND_ALPHA);bt3BeginShaderMode(shader_);
    }
    void sprite(const TextSprite& s) override {
        draw(s.indices,{s.x0,s.y0,s.x1-s.x0,s.y1-s.y0},1,1,1,s.u0,s.v0,s.u1,s.v1,s.palette);
    }
    void rgba(TextureHandle h,Rect box,float alpha=1,float brightness=1,
              float u0=0,float v0=0,float u1=1,float v1=1) {
        draw(h,box,0,alpha,brightness,u0,v0,u1,v1);
    }
    void gauge(TextureHandle h,Rect box,unsigned shape,unsigned relation,float hp,float ki,float fill,bool portraits,float fade) {
        float g[]={hp,ki,0,0},d[]={portraits?1.f:0.f,std::min(.45f,fill),3.f/std::max(24.f,box.width),0};
        draw(h,box,shape==1?2.f:3.f,fade,float(relation),0,0,1,1,0,g,d);
    }
    void end() override {bt3EndShaderMode();bt3EndBlendMode();bt3EndScissorMode();}
};
#include "seamvk/ui_state.inc"
using State=UiState<Sink>;
std::unique_ptr<State> live;
bool attempted=false;
void render(State& state,Rect viewport) {
    if(!state.valid || (state.product && !state.visible()))return;
    state.sink.virtualWidth=float(bt3GetScreenWidth());state.sink.virtualHeight=float(bt3GetScreenHeight());
    state.drawProduct(viewport);
    bt3rlDrawRenderBatchActive();
    // Unlike Vulkan's readback ring, GL draws straight into this window frame.
    // Publish acknowledgments only after the queued UI draw has been submitted.
    auto& ui=uiStore();ui.drawnGeneration=state.frameState.generation;ui.drawnRevision=state.frameState.revision;
}
}
void openglComposite(float x,float y,float width,float height) {
    const char* assets=std::getenv("PS2X_NATIVE_UI_ASSETS");const char* enabled=std::getenv("PS2X_NATIVE_UI_TEST");
    if(!assets || !*assets || !enabled || *enabled!='1')return;
    if(!attempted){attempted=true;int context=0;live=std::make_unique<State>(context,assets);
        std::fprintf(stderr,"\n[nativeui] OpenGL compositor %s\n",live->valid?"ready":"FAILED");
        std::fflush(stderr);}
    if(live)render(*live,{x,y,width,height});
}
void openglUiShutdown(){live.reset();attempted=false;}
} // namespace ps2x::ui

int ps2xNativeUiOpenGlSelfTest(const char* assets,const char* output) {
    using namespace ps2x::ui;
    SetConfigFlags(FLAG_WINDOW_HIDDEN);bt3InitWindow(960,672,"Native OpenGL UI probe");
    if(!IsWindowReady())return 2;
    std::filesystem::create_directories(output);
    int context=0;State state(context,assets);if(!state.valid)return 3;
    state.product=true;state.creditsEnabled=true;state.creditsFull=true;
    const auto now=uiNow();state.creditsBorn=now-500;
    bt3BeginDrawing();bt3ClearBackground(BLACK);render(state,{0,0,960,672});
    bt3rlDrawRenderBatchActive();auto image=LoadImageFromScreen();
    bool ok=ExportImage(image,(std::filesystem::path(output)/"opengl-credits.png").string().c_str());UnloadImage(image);
    bt3EndDrawing();
    state.creditsEnabled=false;
    auto& lifecycle=uiStore().lifecycle;
    for(unsigned test=0;test<3;++test) {
        if(test)lifecycle.teardown(99+test,uiNow());
        lifecycle.close(uiNow());
        const uint64_t generation=100+test;
        std::array<uint16_t,10> fighters{0,1,2,3,4,5,6,7,8,9};
        if(!lifecycle.begin(generation,uiNow(),{fighters.data(),3},{fighters.data()+3,2}))return 6;
        lifecycle.progress(generation,75,4,uiNow());
        state.lastGeneration=generation;state.born=uiNow()-1500;state.smoothProgress=75;
        const char* name=test==0?"opengl-loading.png":test==1?"opengl-hud.png":"opengl-hud-circle.png";
        if(test) {
            lifecycle.ready(generation,uiNow());lifecycle.released(generation,uiNow());
            auto& hs=hudStore();std::lock_guard lock(hs.mutex);auto& h=hs.state;
            h={};h.active=true;h.generation=generation;h.fade=1;h.viewCount=1;
            h.preferences.shape=test==1?1:2;h.preferences.portraits=true;
            h.preferences.scale=65;h.preferences.opacity=70;h.views[0].valid=true;h.views[0].target=1;
            for(unsigned i=0;i<5;++i){auto& a=h.actors[i];a.present=true;a.alive=true;a.character=uint16_t(i);
                a.hp=25000;a.maxHp=40000;a.ki=2500;a.maxKi=5000;a.seat=i?0:1;
                h.views[0].points[i].valid=h.views[0].points[i].onScreen=true;h.views[0].points[i].x=80.f+90.f*i;h.views[0].points[i].y=180;}
        }
        bt3BeginDrawing();bt3ClearBackground({24,40,50,255});render(state,{0,0,960,672});
        bt3rlDrawRenderBatchActive();image=LoadImageFromScreen();
        ok=ExportImage(image,(std::filesystem::path(output)/name).string().c_str()) && ok;UnloadImage(image);bt3EndDrawing();
        if(uiStore().drawnGeneration!=generation)return 7;
    }
    // State owns GPU resources: destroy it before the GL context in the caller.
    return ok?0:4;
}
