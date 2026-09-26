#version 450
// [seamvk] Native outline pass (the post chain's FUN_00105cd8 replaced at the engine seam): the ink alpha the game's
// ink draw subtracts with (Cd - 100 * Ad/128) is written straight from the depth ramp's edges, at native resolution
// and on both sides of an edge, instead of the game's 16-bit scratch-view recipe (draw the ramp, draw it shifted,
// subtract, read back three rows). Alpha 0x80 on the edge core, 0x30 on the fringe, untouched elsewhere.
layout(set = 0, binding = 1) uniform sampler2D uZtop;                     // the Z top-byte plane (alpha of the 0x1c00 colour view)
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;   // CLUT 0x3e8c: Ztop -> depth ramp
layout(push_constant) uniform PC { uvec4 p; } pc;                         // scale, core radius (native px), fringe radius, 0
layout(location = 0) out vec4 outColor;

uint clutEntry(uint i) { uvec4 v = uClut.clut[i >> 2]; return (i & 3u) == 0u ? v.x : (i & 3u) == 1u ? v.y : (i & 3u) == 2u ? v.z : v.w; }
int rampAt(ivec2 p)
{
    ivec2 sz = textureSize(uZtop, 0);
    p = clamp(p, ivec2(0), sz - 1);
    uint zt = uint(texelFetch(uZtop, p, 0).a * 255.0 + 0.5);
    return int(clutEntry(zt) & 0xFFu);
}
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int r = rampAt(p);
    int core = int(pc.p.y), fringe = int(pc.p.z);
    bool onCore = false, onFringe = false;
    for (int dy = -fringe; dy <= fringe; ++dy)
        for (int dx = -fringe; dx <= fringe; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            if (rampAt(p + ivec2(dx, dy)) != r)
            {
                if (abs(dx) <= core && abs(dy) <= core) onCore = true;
                onFringe = true;
            }
        }
    if (!onFringe) discard;
    outColor = vec4(0.0, 0.0, 0.0, onCore ? 128.0 / 255.0 : 48.0 / 255.0);
}
