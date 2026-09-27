#version 450
// [seamvk] Outline pass A (separable): per pixel, the horizontal distance (in pixels, 1..fringe) to the nearest pixel whose
// depth ramp differs from this pixel's by more than the threshold, or fringe + 1 when there is none in the window; plus the
// pixel's own ramp value. Pass B (outline.frag) combines the rows into the exact Euclidean distance to the nearest edge
// pixel inside the square window (for a differing pixel at (dx, dy): if the pixel straight above/below already differs the
// distance is |dy|, else that row's own horizontal distance applies), and fades the ink with it -- an anti-aliased line
// instead of the binary min/max mask ([outlineaa], 2026-09-28). Tap count still grows with the radius, not its square.
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(push_constant) uniform PC { uvec4 p; } pc;   // scale, core radius, fringe radius, ramp threshold
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
    int fringe = int(pc.p.z), thr = int(pc.p.w);
    int r = rampAt(p);
    int dH = fringe + 1;
    for (int dx = 1; dx <= fringe && dx < dH; ++dx)
        if (abs(rampAt(p + ivec2(dx, 0)) - r) > thr || abs(rampAt(p - ivec2(dx, 0)) - r) > thr) dH = dx;
    outColor = vec4(float(dH), float(r), 0.0, 0.0) / 255.0;
}
