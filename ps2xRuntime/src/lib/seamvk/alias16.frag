#version 450
// [seamvk] alias16: a GS draw through a 16-bit FRAME view (PSMCT16/16S, PSMZ16/16S) of a 32-bit target, in the target's
// own pass. Gather form: every target texel holds two 16-bit pixels of the view; each is mapped back to its view
// coordinate, tested against the draw's sprites, shaded (texture / flat colour, TFX, ATE), blended in 8-bit against
// the pixel's own expanded 16-bit value and packed back with the 16-bit FBMSK. The post chain's outline mask and the
// depth-mask pass are such draws (they were skipped before, leaving stale bytes for the read-back).
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate] bit 7 of the stored alpha byte
#include "gs_tables.glsl"
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;
layout(set = 0, binding = 1) uniform sampler2D uTex;
layout(set = 0, binding = 0, std140) uniform Tris { vec4 v[96]; } uT;   // per triangle: three vertices (x, y, u, v) in GS pixels / texels
layout(push_constant) uniform PC
{
    uvec4 tgt;      // fbpT, fbwT (pages per row of the target), scale, ntris
    uvec4 v16;      // fbp16, fbw16, psm16, fbmsk
    ivec4 fA;       // flags (1 tme, 2 fst, 8 bilinear, 16 ate, 128 tcc), tfx, wms | wmt<<2, atst | aref<<3 | afail<<11
    ivec4 blend;    // abe, aA | aB<<2 | aC<<4 | aD<<6, fix, colclamp
    ivec4 texInfo;  // texW, texH, minu | maxu<<16, minv | maxv<<16
    vec4 col;       // the sprites' flat colour, bytes
} pc;
layout(location = 0) out vec4 outColor;

uint addr32(uint block, uint bw, uint x, uint y)
{
    uint ppr = bw != 0u ? bw : 1u;
    uint page = (block >> 5) + (y >> 5) * ppr + (x >> 6);
    uint blockId = (block & 31u) + uint(blockTable32[((y >> 3) & 3u) * 8u + ((x >> 3) & 7u)]);
    return (page << 13) + ((blockId >> 5) << 13) + (blockId & 31u) * 256u + uint(columnTable32[(y & 7u) * 8u + (x & 7u)]) * 4u;
}
// The pixel of the 16-bit view at byte address a, or (-1, -1)
ivec2 view16(uint a)
{
    uint page = a >> 13, base = pc.v16.x >> 5;
    if (page < base) return ivec2(-1);
    uint rel = page - base, fbw = pc.v16.y != 0u ? pc.v16.y : 1u;
    uint py = rel / fbw, px = rel - py * fbw;
    uint blk = (a >> 8) & 31u, off = a & 255u;
    uint psm = pc.v16.z;
    int ib = psm == 10u ? invBlock16S[blk] : psm == 50u ? invBlockZ16[blk] : psm == 58u ? invBlockZ16S[blk] : invBlock16[blk];
    int ic = invColumn16[(off & 63u) >> 1];
    uint cy = (off >> 6) * 2u + uint(ic >> 4);
    return ivec2(int(px * 64u + uint(ib & 15) * 16u + uint(ic & 15)), int(py * 64u + uint(ib >> 4) * 8u + cy));
}
ivec2 wrapT(ivec2 t)
{
    ivec2 size = ivec2(pc.texInfo.xy);
    int wms = pc.fA.z & 3, wmt = (pc.fA.z >> 2) & 3;
    int minu = pc.texInfo.z & 0xFFFF, maxu = pc.texInfo.z >> 16, minv = pc.texInfo.w & 0xFFFF, maxv = pc.texInfo.w >> 16;
    if (wms == 0) t.x = t.x & (size.x - 1); else if (wms == 1) t.x = clamp(t.x, 0, size.x - 1); else if (wms == 2) t.x = clamp(t.x, minu, maxu); else t.x = (t.x & minu) | maxu;
    if (wmt == 0) t.y = t.y & (size.y - 1); else if (wmt == 1) t.y = clamp(t.y, 0, size.y - 1); else if (wmt == 2) t.y = clamp(t.y, minv, maxv); else t.y = (t.y & minv) | maxv;
    return clamp(t, ivec2(0), size - 1);
}
vec4 fetchT(ivec2 t) { return texelFetch(uTex, wrapT(t), 0); }
vec4 sampleT(vec2 uv)
{
    if ((pc.fA.x & 8) != 0)
    {   // bilinear, GS convention: the sample point is uv - 0.5
        vec2 p = uv - 0.5; ivec2 i = ivec2(floor(p)); vec2 f = p - vec2(i);
        return mix(mix(fetchT(i), fetchT(i + ivec2(1, 0)), f.x), mix(fetchT(i + ivec2(0, 1)), fetchT(i + ivec2(1, 1)), f.x), f.y);
    }
    return fetchT(ivec2(floor(uv)));
}
// shade the view pixel q with texcoord uv: the GS colour bytes (r, g, b, a) or -1 when the pixel is not drawn
ivec4 shade(vec2 uv)
{
    int flags = pc.fA.x;
    vec4 cf = pc.col;               // bytes
    vec4 c = cf;
    if ((flags & 1) != 0)
    {
        vec4 ct = sampleT(uv) * 255.0;
        int tfx = pc.fA.y; bool tcc = (flags & 128) != 0;
        vec3 cfs = cf.rgb / 128.0; float afs = cf.a / 128.0;
        if (tfx == 0)      { c.rgb = ct.rgb * cfs;        c.a = tcc ? ct.a * afs : cf.a; }
        else if (tfx == 1) { c.rgb = ct.rgb;              c.a = tcc ? ct.a : cf.a; }
        else if (tfx == 2) { c.rgb = ct.rgb * cfs + cf.a; c.a = tcc ? ct.a + cf.a : cf.a; }
        else               { c.rgb = ct.rgb * cfs + cf.a; c.a = tcc ? ct.a : cf.a; }
        c = clamp(c, 0.0, 255.0);
    }
    ivec4 ci = ivec4(c + 0.5);
    if ((flags & 16) != 0)
    {
        int atst = pc.fA.w & 7, aref = (pc.fA.w >> 3) & 0xFF, afail = (pc.fA.w >> 11) & 3;
        int a = ci.a; bool pass = true;
        if (atst == 0) pass = false; else if (atst == 2) pass = a < aref; else if (atst == 3) pass = a <= aref; else if (atst == 4) pass = a == aref;
        else if (atst == 5) pass = a >= aref; else if (atst == 6) pass = a > aref; else if (atst == 7) pass = a != aref;
        if (!pass && afail == 0) return ivec4(-1);
    }
    return ci;
}
uint pk16(ivec4 c) { return uint(c.r >> 3) | (uint(c.g >> 3) << 5) | (uint(c.b >> 3) << 10) | ((c.a & 0x80) != 0 ? 0x8000u : 0u); }
ivec4 unpk16(uint v) { return ivec4(int(v & 31u) << 3, int((v >> 5) & 31u) << 3, int((v >> 10) & 31u) << 3, (v & 0x8000u) != 0u ? 0x80 : 0); }
uint blend16(uint old, ivec4 cs)
{
    ivec4 cd = unpk16(old);
    ivec3 res = cs.rgb;
    if (pc.blend.x != 0)
    {
        int aA = pc.blend.y & 3, aB = (pc.blend.y >> 2) & 3, aC = (pc.blend.y >> 4) & 3, aD = (pc.blend.y >> 6) & 3;
        ivec3 A = aA == 0 ? cs.rgb : aA == 1 ? cd.rgb : ivec3(0);
        ivec3 B = aB == 0 ? cs.rgb : aB == 1 ? cd.rgb : ivec3(0);
        int   C = aC == 0 ? cs.a : aC == 1 ? cd.a : pc.blend.z;
        ivec3 D = aD == 0 ? cs.rgb : aD == 1 ? cd.rgb : ivec3(0);
        res = ((A - B) * C >> 7) + D;
        res = pc.blend.w != 0 ? clamp(res, 0, 255) : (res & 255);
    }
    uint v = pk16(ivec4(res, cs.a));
    uint m = pc.v16.w;
    uint m16 = ((m >> 3) & 0x1Fu) | (((m >> 11) & 0x1Fu) << 5) | (((m >> 19) & 0x1Fu) << 10) | ((m >> 31) << 15);
    return (old & m16) | (v & ~m16);
}

void main()
{
    uvec2 p = uvec2(gl_FragCoord.xy) / pc.tgt.z;
    uint a = addr32(pc.tgt.x, pc.tgt.y, p.x, p.y);
    uint d = packUnorm4x8(subpassLoad(uDst));
    uint o = d; bool touched = false;
    for (uint h = 0u; h < 2u; ++h)
    {
        ivec2 q = view16(a + h * 2u);
        if (q.x < 0) continue;
        for (uint k = 0u; k < pc.tgt.w; ++k)
        {   // coverage and attributes at the pixel centre (the GS convention, as gs.vert), edge functions with a top-left rule
            vec4 A = uT.v[k * 3u], B = uT.v[k * 3u + 1u], C = uT.v[k * 3u + 2u];
            vec2 P = vec2(q) + 0.5;
            float area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
            if (area == 0.0) continue;
            float sgn = area > 0.0 ? 1.0 : -1.0;
            float e0 = ((B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x)) * sgn;   // edge AB (opposite C)
            float e1 = ((C.x - B.x) * (P.y - B.y) - (C.y - B.y) * (P.x - B.x)) * sgn;   // edge BC (opposite A)
            float e2 = ((A.x - C.x) * (P.y - C.y) - (A.y - C.y) * (P.x - C.x)) * sgn;   // edge CA (opposite B)
            bool tl0 = sgn * (B.y - A.y) < 0.0 || (B.y == A.y && sgn * (B.x - A.x) > 0.0);
            bool tl1 = sgn * (C.y - B.y) < 0.0 || (C.y == B.y && sgn * (C.x - B.x) > 0.0);
            bool tl2 = sgn * (A.y - C.y) < 0.0 || (A.y == C.y && sgn * (A.x - C.x) > 0.0);
            if (!((e0 > 0.0 || (e0 == 0.0 && tl0)) && (e1 > 0.0 || (e1 == 0.0 && tl1)) && (e2 > 0.0 || (e2 == 0.0 && tl2)))) continue;
            float aa = abs(area);
            vec2 uv = (A.zw * e1 + B.zw * e2 + C.zw * e0) / aa;
            ivec4 cs = shade(uv);
            if (cs.r < 0) break;
            uint old = h == 0u ? (o & 0xFFFFu) : (o >> 16);
            uint nv = blend16(old, cs);
            o = h == 0u ? ((o & 0xFFFF0000u) | nv) : ((o & 0xFFFFu) | (nv << 16));
            touched = true;
            break;
        }
    }
    if (!touched) discard;
    gl_FragStencilRefARB = int(o >> 31);
    outColor = unpackUnorm4x8(o);
}
