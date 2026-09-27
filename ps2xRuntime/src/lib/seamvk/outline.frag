#version 450
// [seamvk] Native outline pass (the post chain's FUN_00105cd8 replaced at the engine seam): the ink alpha the game's
// ink draw subtracts with (Cd - 100 * Ad/128) is written straight from the depth ramp's edges, at native resolution
// and on both sides of an edge, instead of the game's 16-bit scratch-view recipe (draw the ramp, draw it shifted,
// subtract, read back three rows). Alpha 0x80 on the edge core, 0x30 on the fringe, untouched elsewhere.
// [outlineaa] The core/fringe decision is by the exact distance to the nearest edge pixel (see outline_h.frag) and the
// alpha fades over the last pixel of each band, so the line is anti-aliased instead of a 1-px staircase; an edge is a
// ramp difference above the threshold (the CLUT ramp steps by 8; every step inked the far character solid).
layout(set = 0, binding = 1) uniform sampler2D uZtop;                     // the Z top-byte plane (alpha of the 0x1c00 colour view)
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;   // CLUT 0x3e8c: Ztop -> depth ramp
layout(push_constant) uniform PC { uvec4 p; } pc;                         // scale, core radius (native px), fringe radius, ramp threshold
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate]
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 2) uniform sampler2D uH;   // pass A: (horizontal edge distance, ramp) per pixel
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float core = float(pc.p.y), fringe = float(pc.p.z);
    int ifringe = int(pc.p.z), thr = int(pc.p.w);
    ivec2 sz = textureSize(uH, 0);
    ivec2 h0 = ivec2(texelFetch(uH, clamp(p, ivec2(0), sz - 1), 0).xy * 255.0 + 0.5);
    int r = h0.y;
    float dist = float(h0.x);   // this row: its own horizontal distance
    for (int dy = 1; dy <= ifringe; ++dy)
    {
        ivec2 a = ivec2(texelFetch(uH, clamp(p + ivec2(0, dy), ivec2(0), sz - 1), 0).xy * 255.0 + 0.5);
        ivec2 b = ivec2(texelFetch(uH, clamp(p - ivec2(0, dy), ivec2(0), sz - 1), 0).xy * 255.0 + 0.5);
        float da = abs(a.y - r) > thr ? float(dy) : sqrt(float(dy * dy + a.x * a.x));
        float db = abs(b.y - r) > thr ? float(dy) : sqrt(float(dy * dy + b.x * b.x));
        dist = min(dist, min(da, db));
    }
    if (dist > fringe + 0.5) discard;
    float coreCov = clamp(core + 0.5 - dist, 0.0, 1.0);      // 1 inside the core band, fading over its last pixel
    float fringeCov = clamp(fringe + 0.5 - dist, 0.0, 1.0);
    float a = max(128.0 * coreCov, 48.0 * fringeCov);
    gl_FragStencilRefARB = coreCov > 0.5 ? 1 : 0;
    outColor = vec4(0.0, 0.0, 0.0, a / 255.0);
}
