// [seamvk] Internal contract between the GS front-end (ps2_seamgs.cpp, GS thread, stream order)
// and the Vulkan renderer (ps2_seamvk.cpp, GS thread, at the swap). Not a public header.
#pragma once
#include "runtime/ps2_seamvk.h"
#include <cstdint>
#include <vector>
#include <array>

namespace seamgs
{
    // A GS draw vertex: screen space after XYOFFSET, in GS pixels; z normalised to the Z buffer depth.
    struct Vtx
    {
        float x, y, z, q;
        float s, t;          // STQ mode: raw s, t (divide by q per pixel)
        float u, v;          // UV mode: texels
        uint32_t rgba;       // raw bytes, 0x80 = 1.0
        float fog;
        uint32_t pad[2];
    };
    static_assert(sizeof(Vtx) == 48, "Vtx is 48 bytes");

    // The register state a draw needs, snapshotted at the kick.
    struct State
    {
        // FRAME / ZBUF
        uint32_t fbp = 0, fbw = 8, fpsm = 0, fbmsk = 0;
        uint32_t zbp = 0, zpsm = 0; uint8_t zmsk = 0;
        // TEST
        uint8_t ate = 0, atst = 1, aref = 0, afail = 0, date = 0, datm = 0, zte = 1, ztst = 1;
        // ALPHA / PABE / FBA / COLCLAMP
        uint8_t abe = 0, aA = 0, aB = 1, aC = 0, aD = 1, fix = 0x80, pabe = 0, fba = 0, colclamp = 1;
        // PRIM
        uint8_t prim = 3, iip = 0, tme = 0, fge = 0, fst = 0, aa1 = 0, ctxt = 0;
        // TEX0 / TEX1 / CLAMP (only meaningful with tme)
        uint8_t tfx = 0, tcc = 0, wms = 0, wmt = 0, mmag = 0, mmin = 0;
        uint16_t minu = 0, maxu = 0, minv = 0, maxv = 0;
        uint32_t texW = 0, texH = 0;
        int32_t tex = -1;    // texture slot, -1 untextured
        uint8_t cpuRastered = 0;    // also rasterised into the VRAM mirror (small scratch target)
        uint8_t texFromDrawn = 0;   // the texture read VRAM pages the game had drawn into (mirror is stale there)
        uint32_t tex0lo = 0, tex0hi = 0;   // raw TEX0 for diagnostics
        uint64_t dbgPrim = 0, dbgPrmode = 0, dbgSc[2] = {}; uint8_t dbgPrmodecont = 0;   // raw PRIM / PRMODE / both SCISSORs for diagnostics
        // SCISSOR / XYOFFSET
        uint16_t scax0 = 0, scax1 = 639, scay0 = 0, scay1 = 447;
        uint16_t ofx = 0, ofy = 0;
        // FOGCOL
        uint32_t fogcol = 0;
    };

    // Decode a texture from a render target on the GPU (kind 2), in stream order: after the draws that wrote
    // the target and before the draw that samples it.
    struct RtDecode
    {
        int32_t slot = -1; uint32_t w = 0, h = 0;
        uint32_t tbp = 0, tbw = 0, psm = 0, cbp = 0, cpsm = 0, csa = 0;
        uint64_t texa = 0;
        uint32_t srcFbp = 0, srcFbw = 0, srcRows = 0;
        uint8_t clutFromTarget = 0, depthSrc = 0;
        uint8_t fromVram = 0; uint32_t upFirst = 0, upCount = 0;   // decode from the GPU copy of VRAM: pages [upFirst, upFirst+upCount) of FrameList::vramPages are uploaded first
        uint32_t clut[256] = {};
    };

    struct Draw
    {
        uint8_t kind = 0;     // 0: GS triangle list of Vtx; 1: seam host mesh (DrawPacket verts, strip); 2: RtDecode op
        uint8_t prog = 0;     // kind 1: the vertex program
        uint8_t hostPass = 0; // kind 1: which of the packet's passes this draw is (the two-pass character program: 0 = textured, 1 = toon ramp)
        State st;
        uint32_t vertOff = 0, count = 0, stride = sizeof(Vtx);
        int32_t rt = -1;      // kind 2: index into FrameList::rtDecodes
        seamvk::Consts c;     // kind 1 only
    };

    // A texture decoded this frame for a slot. Slots are stable until freed.
    struct TexUpload { int32_t slot; uint32_t w, h; std::vector<uint8_t> rgba; uint64_t share = 0; };   // [seampack] share: key of a session-wide shared image (pack replacements): rgba may be empty when the renderer already holds it

    struct FrameList
    {
        std::vector<Draw> draws;
        std::vector<uint8_t> verts;
        std::vector<TexUpload> texUploads;
        std::vector<RtDecode> rtDecodes;
        std::vector<uint16_t> vramPages; std::vector<uint8_t> vramBytes;   // page snapshots (8 KB each, in vramPages order) for the GPU VRAM copy
        std::vector<std::array<uint32_t, 256>> stepCluts;   // kind 3 draws: the palette a native step needs, captured at the marker (the mirror moves on)
        std::vector<int32_t> texFrees;
        struct RegEvent { uint32_t drawIndex; uint8_t path, hostGif, addr; uint64_t value; };   // FRAME/SCISSOR writes in stream order (diagnostics)
        std::vector<RegEvent> regEvents;
        uint64_t frame = 0;
        int32_t dispSlot[2] = { -1, -1 }; uint32_t dispFbp[2] = { ~0u, ~0u };   // [dispvram] per circuit: the slot holding a VRAM decode of the display buffer (movies: uploaded, never drawn)
    };

    // Renderer side, at the swap: take everything recorded since the last swap. dispfb[2] / enMask: the CRTC circuits
    // at this swap ([dispvram]: a display buffer the game uploaded into after its last draw is decoded from the mirror).
    void takeFrame(FrameList &out, const uint64_t dispfb[2], uint32_t enMask);
    // Front-end reset when the renderer drops all GPU textures.
    void dropAllTextures();
}
