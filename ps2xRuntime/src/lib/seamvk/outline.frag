#version 450
// [seamvk] Native outline pass B (the post chain's FUN_00105cd8 at the engine seam): the ink alpha the game's ink draw
// consumes, written straight into the frame's alpha, at native resolution.
// [outlinegame] The game's read-back of its edge buffer, PINNED EMPIRICALLY against paraLLEl-GS's ink alpha with the packet
// oracle (work/rig/readback_model.py, 96% exact near edges; the sprite-coordinate derivation with half-texel bilinear
// mixes was wrong -- the ink is pure 0x30/0x80, no mixes, and it left a gap next to the silhouette): three alpha-only
// reads, ATE "alpha != 0", AEM giving TA0 per non-zero texel: (5) E at p with 0x30, (6) E at p + (0, 1 GS px) with
// 0x30, (7) the same with 0x80 -- (7) overwrites (6). So A = core ? 0x80 : fringe ? 0x30 : 0 with core = E(p + (0, s))
// and fringe = E(p), s = one GS px in native px. The band E is one GS px wide above/left of a boundary, so the ink
// hugs the silhouette from outside on top/left edges and sits inside it on bottom/right edges -- the game's look.
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(push_constant) uniform PC { uvec4 p; } pc;   // scale, core alpha (0x80), fringe alpha (0x30), unused
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate]
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 2) uniform sampler2D uH;   // pass A: edge per pixel (linear sampler: the game's bilinear read-back)
float edgeAt(ivec2 q)
{
    ivec2 sz = textureSize(uH, 0);
    return texelFetch(uH, clamp(q, ivec2(0), sz - 1), 0).x;
}
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int s = int(pc.p.x);
    float core = edgeAt(p + ivec2(0, s));
    float fringe = edgeAt(p);
    float a = core > 0.0 ? float(pc.p.y) * core : (fringe > 0.0 ? float(pc.p.z) * fringe : 0.0);
    if (a <= 0.0) discard;
    gl_FragStencilRefARB = core >= 0.5 ? 1 : 0;
    outColor = vec4(0.0, 0.0, 0.0, a / 255.0);
}
