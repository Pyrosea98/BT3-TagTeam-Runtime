#version 450
// [seamvk] Build a GS texture from a render target: texel -> GS byte address (real swizzle) -> the CT32 target
// pixel holding that byte (inverse swizzle) -> palette. Exact for CT32 targets holding GS bytes; the Z24 bits of a
// depth buffer come from the depth image, its top byte from the colour target the game writes it through.
#include "gs_tables.glsl"
layout(set = 0, binding = 0) uniform sampler2D uSrc;     // the colour target (RGBA8 = GS bytes)
layout(set = 0, binding = 1) uniform sampler2D uDepth;   // D32 for Z reads (or a dummy)
layout(set = 0, binding = 2, std140) uniform Clut { uvec4 clut[64]; } uClut;   // 256 entries from the mirror
layout(push_constant) uniform PC
{
    uvec4 tex;      // tbp, tbw, psm, tw|th<<8
    uvec4 clut;     // cbp, cpsm, csa, flags (1 clut from target, 2 depth source, 4 indexed)
    uvec4 src;      // srcFbp, srcFbw, srcRows, scale
    uvec4 texa;     // ta0, aem, ta1, 0
} pc;
layout(location = 0) out vec4 outColor;

uint addr32(uint block, uint bw, uint x, uint y)
{
    uint ppr = bw != 0u ? bw : 1u;
    uint page = (block >> 5) + (y >> 5) * ppr + (x >> 6);
    uint blockId = (block & 31u) + uint(blockTable32[((y >> 3) & 3u) * 8u + ((x >> 3) & 7u)]);
    return (page << 13) + ((blockId >> 5) << 13) + (blockId & 31u) * 256u + uint(columnTable32[(y & 7u) * 8u + (x & 7u)]) * 4u;
}
uint addr8(uint block, uint bw, uint x, uint y)
{
    uint ppr = (bw >> 1) != 0u ? (bw >> 1) : 1u;
    uint page = (block >> 5) + (y >> 6) * ppr + (x >> 7);
    uint blockId = (block & 31u) + uint(blockTable8[((y >> 4) & 3u) * 8u + ((x >> 4) & 7u)]);
    return (page << 13) + ((blockId >> 5) << 13) + (blockId & 31u) * 256u + uint(columnTable8[(y & 15u) * 16u + (x & 15u)]);
}
uint addr4(uint block, uint bw, uint x, uint y)   // nibble address
{
    uint ppr = (bw >> 1) != 0u ? (bw >> 1) : 1u;
    uint page = (block >> 5) + (y >> 7) * ppr + (x >> 7);
    uint blockId = (block & 31u) + uint(blockTable4[((y >> 4) & 7u) * 4u + ((x >> 5) & 3u)]);
    return (page << 14) + ((blockId >> 5) << 14) + (blockId & 31u) * 512u + uint(columnTable4[(y & 15u) * 32u + (x & 31u)]);
}
uint addr16(uint psm, uint block, uint bw, uint x, uint y)
{
    uint ppr = bw != 0u ? bw : 1u;
    uint page = (block >> 5) + (y >> 6) * ppr + (x >> 6);
    uint bi = ((y >> 3) & 7u) * 4u + ((x >> 4) & 3u);
    int bt = psm == 10u ? blockTable16S[bi] : psm == 50u ? blockTableZ16[bi] : psm == 58u ? blockTableZ16S[bi] : blockTable16[bi];
    uint blockId = (block & 31u) + uint(bt);
    return (page << 13) + ((blockId >> 5) << 13) + (blockId & 31u) * 256u + uint(columnTable16[(y & 1u) * 16u + (x & 15u)]) * 2u;
}

// The CT32 target pixel holding GS byte address a (dword-aligned), or (-1,-1) when outside the target.
ivec2 rtPixel(uint a)
{
    uint page = a >> 13;
    uint base = pc.src.x >> 5;
    if (page < base) return ivec2(-1);
    uint rel = page - base;
    uint fbw = pc.src.y != 0u ? pc.src.y : 1u;
    uint py = rel / fbw, px = rel - py * fbw;
    if (py >= pc.src.z) return ivec2(-1);
    uint blk = (a >> 8) & 31u, word = (a & 255u) >> 2;
    int ib = invBlock32[blk], ic = invColumn32[word];
    return ivec2(int(px * 64u + uint(ib & 15) * 8u + uint(ic & 15)), int(py * 32u + uint(ib >> 4) * 8u + uint(ic >> 4)));
}
uint rtDword(uint a)
{
    ivec2 p = rtPixel(a & ~3u);
    if (p.x < 0) return 0u;
    ivec2 sp = p * int(pc.src.w);
    uint d = packUnorm4x8(texelFetch(uSrc, sp, 0));
    if ((pc.clut.w & 2u) != 0u)
    {   // depth buffer: the Z24 value from the depth image, the top byte from the colour target
        float z = texelFetch(uDepth, sp, 0).r;
        uint zi = uint(clamp(z, 0.0, 1.0) * 16777216.0 + 0.5) & 0xFFFFFFu;
        d = (d & 0xFF000000u) | zi;
    }
    return d;
}
uint expand16(uint c)
{
    uint r = (c & 0x1Fu) << 3, g = ((c >> 5) & 0x1Fu) << 3, b = ((c >> 10) & 0x1Fu) << 3;
    uint a = (c & 0x8000u) != 0u ? pc.texa.z : ((pc.texa.y != 0u && (c & 0x7FFFu) == 0u) ? 0u : pc.texa.x);
    return r | (g << 8) | (b << 16) | (a << 24);
}
uint clutEntry(uint i)
{
    if ((pc.clut.w & 1u) != 0u)
    {   // palette rendered into the target: CT32 16x16 CSM1 layout at cbp, buffer width 1
        uint x = (i & 7u) | ((i & 0x10u) >> 1), y = ((i & 8u) >> 3) | ((i & 0xE0u) >> 4);
        return rtDword(addr32(pc.clut.x, 1u, x, y));
    }
    uvec4 v = uClut.clut[i >> 2];
    return (i & 3u) == 0u ? v.x : (i & 3u) == 1u ? v.y : (i & 3u) == 2u ? v.z : v.w;
}

void main()
{
    uint x = uint(gl_FragCoord.x), y = uint(gl_FragCoord.y);
    uint tbp = pc.tex.x, tbw = pc.tex.y, psm = pc.tex.z;
    uint csaOff = (psm == 20u || psm == 36u || psm == 44u) ? (pc.clut.z & 15u) * 16u : 0u;
    uint v;
    if (psm == 19u)      { uint a = addr8(tbp, tbw, x, y); v = clutEntry((rtDword(a) >> ((a & 3u) * 8u)) & 0xFFu); }
    else if (psm == 20u) { uint n = addr4(tbp, tbw, x, y); uint a = n >> 1; uint b = (rtDword(a) >> ((a & 3u) * 8u)) & 0xFFu; v = clutEntry((csaOff + ((b >> ((n & 1u) * 4u)) & 0xFu)) & 255u); }
    else if (psm == 27u) { v = clutEntry(rtDword(addr32(tbp, tbw, x, y)) >> 24); }
    else if (psm == 36u) { v = clutEntry((csaOff + ((rtDword(addr32(tbp, tbw, x, y)) >> 24) & 0xFu)) & 255u); }
    else if (psm == 44u) { v = clutEntry((csaOff + (rtDword(addr32(tbp, tbw, x, y)) >> 28)) & 255u); }
    else if (psm == 1u || psm == 49u) { uint c = rtDword(addr32(tbp, tbw, x, y)) & 0xFFFFFFu; v = c | (((pc.texa.y != 0u && c == 0u) ? 0u : pc.texa.x) << 24); }   // AEM: RGB 0 -> alpha 0
    else if (psm == 2u || psm == 10u || psm == 50u || psm == 58u)
    {
        uint a = addr16(psm, tbp, tbw, x, y);
        uint d = rtDword(a);
        v = expand16((a & 2u) != 0u ? (d >> 16) : (d & 0xFFFFu));
    }
    else v = rtDword(addr32(tbp, tbw, x, y));
    outColor = unpackUnorm4x8(v);
}
