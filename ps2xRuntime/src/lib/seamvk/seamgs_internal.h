// [seamvk] Internal contract between the GS front-end (ps2_seamgs.cpp, GS thread, stream order)
// and the Vulkan renderer (ps2_seamvk.cpp, GS thread, at the swap). Not a public header.
#pragma once
#include "runtime/ps2_seamvk.h"
#include <cstdint>
#include <vector>

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
        // SCISSOR / XYOFFSET
        uint16_t scax0 = 0, scax1 = 639, scay0 = 0, scay1 = 447;
        uint16_t ofx = 0, ofy = 0;
        // FOGCOL
        uint32_t fogcol = 0;
    };

    struct Draw
    {
        uint8_t kind = 0;     // 0: GS triangle list of Vtx; 1: seam host mesh (DrawPacket verts, strip)
        uint8_t prog = 0;     // kind 1: the vertex program
        State st;
        uint32_t vertOff = 0, count = 0, stride = sizeof(Vtx);
        seamvk::Consts c;     // kind 1 only
    };

    // A texture decoded this frame for a slot. Slots are stable until freed.
    struct TexUpload { int32_t slot; uint32_t w, h; std::vector<uint8_t> rgba; };

    struct FrameList
    {
        std::vector<Draw> draws;
        std::vector<uint8_t> verts;
        std::vector<TexUpload> texUploads;
        std::vector<int32_t> texFrees;
        uint64_t frame = 0;
    };

    // Renderer side, at the swap: take everything recorded since the last swap.
    void takeFrame(FrameList &out);
    // Front-end reset when the renderer drops all GPU textures.
    void dropAllTextures();
}
