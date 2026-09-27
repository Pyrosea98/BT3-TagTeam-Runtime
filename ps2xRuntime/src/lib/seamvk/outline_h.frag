#version 450
// [seamvk] Outline pass A: the game's edge buffer (FUN_00105cd8, docs/evidence/postchain-transcript-steps.txt 775-1100):
// the depth-ramp CLUT value of the pixel against its RIGHT and LOWER neighbour one GS pixel away (the game draws the
// ramp into a 16-bit scratch buffer, subtracts it shifted by (1, 0) and (0, 1) GS px with COLCLAMP off, and the
// read-back keys on "non-zero" through AEM -- the ramp steps by 8, so that is simply "differs"). ONE-SIDED: only the
// top/left pixel of a boundary is an edge, a band one GS pixel (pc.p.x native px) wide. Output x = edge (0/1).
// [outlinegame] Pinned against paraLLEl-GS's own edge buffer with the packet oracle (98.8% of the frame exact; the
// rest is its supersampling at material boundaries).
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(push_constant) uniform PC { uvec4 p; } pc;   // scale (= 1 GS px in native px), core alpha, fringe alpha, unused
layout(location = 0) out vec4 outColor;
uint clutEntry(uint i) { uvec4 v = uClut.clut[i >> 2]; return (i & 3u) == 0u ? v.x : (i & 3u) == 1u ? v.y : (i & 3u) == 2u ? v.z : v.w; }
int rampAt(ivec2 p)
{
    ivec2 sz = textureSize(uZtop, 0);
    p = clamp(p, ivec2(0), sz - 1);
    uint zt = uint(texelFetch(uZtop, p, 0).a * 255.0 + 0.5);
    uint e = clutEntry(zt);
    return (e >> 24) != 0u ? int(e & 0xFFu) : 0;   // the ramp draw blends by the CLUT alpha: alpha-0 entries (the background, 255) stay at the clear (0)
}
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int s = int(pc.p.x);
    int r = rampAt(p);
    bool e = rampAt(p + ivec2(s, 0)) != r || rampAt(p + ivec2(0, s)) != r;
    outColor = vec4(e ? 1.0 : 0.0, 0.0, 0.0, 0.0);
}
