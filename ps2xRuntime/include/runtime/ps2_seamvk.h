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
    bool on();

    // std140 mirror of the seam vertex shader's Consts block: 30 vec4.
    struct Consts
    {
        float A[4][4], B[4][4], E[4][4], F[4][4], C[4][4], D[4][4];
        float pivA[4], pivB[4], colA[4], colB[4], misc[4], view[4];
    };
    static_assert(sizeof(Consts) == 30 * 16, "Consts must match the shader's std140 block");

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

    // Present thread: the newest native frame (RGBA8), when one is ready.
    bool takeFrame(std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h);

    struct PrivRegs { uint64_t pmode = 0, dispfb1 = 0, display1 = 0, dispfb2 = 0, display2 = 0, bgcolor = 0; };
}

#ifdef PS2X_HAVE_PGS
namespace Vulkan { class Device; }
namespace seamvk
{
    // GS thread, under the paraLLEl-GS state lock, at the frame swap.
    void renderFrame(Vulkan::Device &device, const PrivRegs &priv);
}
#endif
