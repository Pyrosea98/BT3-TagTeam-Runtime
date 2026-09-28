// [seamvk] Native Vulkan renderer (Phase 4 of docs/NATIVE-RENDER-SEAM.md).
//
// The whole GIF stream (PATH1 XGKICK packets, PATH2 DIRECT, PATH3) is interpreted on the GS
// thread in exact stream order by the seamgs front-end (ps2_seamgs.cpp): register state, a
// VRAM mirror for uploads, palette + texture decode, and vertex kicks assembled into draws.
// The seam's host meshes arrive on the same stream as HostDraw packets and are drawn by the
// GPU vertex shaders at float precision, with the GS state in force at that point.
// At the frame swap the ordered draw list is rendered into per-FRAME render targets, the
// CRTC circuit(s) are composed from DISPFB/DISPLAY, and the result is read back and presented
// INSTEAD of the paraLLEl-GS frame when PS2X_SEAMVK=1 (paraLLEl-GS keeps running as the VRAM-exact
// reference until the native path covers everything).
#pragma once
#include <cstdint>
#include <vector>

namespace seamvk
{
#if defined(PS2X_HAVE_SEAMVK)
    bool on();
    void configure(bool enable);   // [nativeopt] settings: renderer = "native" (ignored once PS2X_SEAMVK is set in the environment)
    void requestDump();   // [dumpkey] dump the next rendered frame (draw list, targets, textures under PS2X_SEAMVK_TEXDUMP)

    // std140 mirror of the seam vertex shader's Consts block: 30 vec4.
    struct Consts
    {
        float A[4][4], B[4][4], E[4][4], F[4][4], C[4][4], D[4][4];
        float pivA[4], pivB[4], colA[4], colB[4], misc[4], view[4];
        float range[4];   // [batch] x = first vertex of this chunk in the batch's vertex buffer (the shader finds its chunk by gl_VertexIndex)
    };
    static_assert(sizeof(Consts) == 31 * 16, "Consts must match the shader's std140 block");

    // HostDraw packet 'SVKD': this header followed by count*stride bytes of raw list vertices.
    // The seam submits it immediately BEFORE the host-transformed GIF packet of the same chunk
    // ('SVKG' below), whose register writes (TEX0 etc.) are the state the draw must use.
    struct DrawPacket
    {
        uint32_t magic;        // 'SVKD'
        uint8_t prog;          // 0 stage, 1 effects, 2 char two-pass, 3 char single-pass, 4 char prefight
        uint8_t pad0[3];
        uint32_t stride, count;
        uint32_t gifTagWord1;  // the chunk's geometry GIF tag, word 1: PRE + PRIM (+ NREG)
        Consts c;
    };
    static constexpr uint32_t kDrawMagic = 0x444B5653u;   // 'SVKD'

    // HostDraw packet 'SVKG': the seam's host-transformed GIF packet (what VU1 would have kicked).
    // The arbiter forwards its payload to the GS backend as PATH1 and to the native front-end,
    // which applies its register writes but draws the preceding 'SVKD' mesh instead of its vertices.
    struct HostGifHeader { uint32_t magic, size; };
    static constexpr uint32_t kHostGifMagic = 0x474B5653u;   // 'SVKG'

    // GS thread, from the arbiter, in stream order.
    void onGifPacket(uint8_t path, const uint8_t *data, uint32_t size, bool hostGif);
    void onHostDraw(const uint8_t *data, uint32_t size);
    // [postnative] a post-chain step replaced at the engine seam: its packets' kicks are neutralised by the arbiter and the
    // native front end records the step at that stream position (kind 3 draw) for the GPU pass.
    void onNativeStep(int step);
    // [clutpass] generic steps (their packets' kicks are neutralised too): while a packet of the step is parsed, every kick
    // is recorded with its exact register state, vertex colour and palette as a native pass at that stream position.
    void nativeParse(int step);   // -1: none
    bool nativeHudOn();           // [nativehud] the front end does the widescreen HUD layout and the ink/shadow toggles itself (the packet walker is skipped)
    bool nativeStepGeneric(int step);

    // Present thread: the newest native frame (RGBA8), when one is ready.
    bool takeFrame(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);
    uint64_t lastFrameGframe();   // [presentlat] the game frame the last taken frame was rendered from
    void shutdown();              // [earlyframe] join the readback consumer (before the device is destroyed)
#else
    // [standalone] built without Granite (no Vulkan renderer at all): the runtime's hooks fall through
    inline bool on() { return false; }
    inline void configure(bool) {}
    inline void requestDump() {}
    inline void onGifPacket(uint8_t, const uint8_t *, uint32_t, bool) {}
    inline void onHostDraw(const uint8_t *, uint32_t) {}
    inline void onNativeStep(int) {}
    inline void nativeParse(int) {}
    inline bool nativeHudOn() { return false; }
    inline bool nativeStepGeneric(int) { return false; }
    inline bool takeFrame(std::vector<uint8_t> &, uint32_t &, uint32_t &) { return false; }
    inline uint64_t lastFrameGframe() { return 0; }
    inline void shutdown() {}
#endif

    struct PrivRegs { uint64_t pmode = 0, dispfb1 = 0, display1 = 0, dispfb2 = 0, display2 = 0, bgcolor = 0; };
}
namespace seamgs
{
#if defined(PS2X_HAVE_SEAMVK)
    // Diagnostics: the CSM1 palette at cbp as the VRAM mirror holds it (256 RGBA entries). False when the front-end is off.
    bool peekClut(uint32_t cbp, uint32_t cpsm, uint32_t *out256);
    bool dumpMirror(const char *path);   // diagnostics: the 4 MB VRAM mirror
    bool peekTexel(int32_t slot, uint32_t x, uint32_t y, uint32_t &rgba);   // diagnostics: a decoded texel of a cached slot (textures up to 256x256)
#else
    inline bool peekClut(uint32_t, uint32_t, uint32_t *) { return false; }
    inline bool dumpMirror(const char *) { return false; }
    inline bool peekTexel(int32_t, uint32_t, uint32_t, uint32_t &) { return false; }
#endif
}

struct GSRegisters;
namespace seamvk
{
#if defined(PS2X_HAVE_SEAMVK)
    // [standalone] The native renderer on its own Vulkan device, no paraLLEl-GS backend instantiated:
    //  - the display block arrives in stream order (the same [s1fence] GsApply jobs and [displatch] flip the backend got),
    //    with the live register block as the fallback for registers never written on the stream;
    //  - onSwap() at the GS renderer's frame swap composes the CRTC circuits from that block and renders the frame.
    void setLiveRegs(const GSRegisters *regs);         // the runtime's live privileged-register block
    void streamPriv(uint32_t regOff, uint64_t value);  // [s1fence] a display register, stream-ordered
    void streamFlip(uint64_t dispfb1);                 // [displatch] the game's DISPFB1 flip, stream-ordered
    void setPresentSize(uint32_t w, uint32_t h);       // the window's present size (HUD squeeze factor)
    void onSwap();                                     // GS thread: render the frame on the seam's own device
#else
    inline void setLiveRegs(const GSRegisters *) {}
    inline void streamPriv(uint32_t, uint64_t) {}
    inline void streamFlip(uint64_t) {}
    inline void setPresentSize(uint32_t, uint32_t) {}
    inline void onSwap() {}
#endif
}
#ifdef PS2X_HAVE_SEAMVK
namespace Vulkan { class Device; }
namespace seamvk
{
    // GS thread, under the paraLLEl-GS state lock, at the frame swap (PS2X_SEAMVK_REF=1: the reference backend runs
    // beside the native renderer on the backend's device).
    void renderFrame(Vulkan::Device &device, const PrivRegs &priv);
}
#endif
