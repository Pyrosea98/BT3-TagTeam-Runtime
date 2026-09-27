#version 450
// [seamvk] Outline pass A (separable): per pixel, the min and max of the depth ramp over the horizontal windows of the core
// and fringe radii, written as (minC, maxC, minF, maxF) / 255. Pass B (outline.frag) takes the vertical min/max of these,
// which equals the min/max over the square window, so "any pixel in the square differs from the centre" is exact and the
// tap count grows with the radius, not its square.
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(push_constant) uniform PC { uvec4 p; } pc;   // scale, core radius, fringe radius, 0
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
    int core = int(pc.p.y), fringe = int(pc.p.z);
    int minC = 255, maxC = 0, minF = 255, maxF = 0;
    for (int dx = -fringe; dx <= fringe; ++dx)
    {
        int r = rampAt(p + ivec2(dx, 0));
        minF = min(minF, r); maxF = max(maxF, r);
        if (abs(dx) <= core) { minC = min(minC, r); maxC = max(maxC, r); }
    }
    outColor = vec4(float(minC), float(maxC), float(minF), float(maxF)) / 255.0;
}
