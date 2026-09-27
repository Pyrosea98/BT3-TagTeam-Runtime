#version 450
// [seamvk] Native outline pass B (the post chain's FUN_00105cd8 at the engine seam): the ink alpha the game's ink draw
// consumes, written straight into the frame's alpha, at native resolution.
// [outlinegame] The game's read-back of its edge buffer, exactly (packet oracle, docs/evidence/postchain-transcript-steps.txt
// 1031-1110): three bilinear full-frame reads of the 16-bit edge buffer, alpha-only, ATE "alpha != 0", AEM giving alpha
// TA0 per non-zero texel: (5) at GS offset (+0.5, +0.5) with TA0 0x30, (6) at (+0.5, +1.5) with 0x30, (7) at (+0.5, +1.5)
// with 0x80 -- (7) overwrites everything (6) wrote, so: A = core > 0 ? 0x80 * core : fringe > 0 ? 0x30 * fringe : 0, with
// core = E sampled bilinearly at p + (0.5, 1.5) GS px and fringe = E at p + (0.5, 0.5) GS px (offsets scale with the
// render scale, one GS px = pc.p.x native px). This is what paraLLEl-GS renders at its scale: the band stays one GS pixel
// wide while the bodies scale, which is why a far character is readable there and a solid blob at GS resolution.
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(push_constant) uniform PC { uvec4 p; } pc;   // scale, core alpha (0x80), fringe alpha (0x30), unused
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate]
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 2) uniform sampler2D uH;   // pass A: edge per pixel (linear sampler: the game's bilinear read-back)
float edgeAt(vec2 q)   // bilinear sample of the edge buffer at native texel coordinate q (texel centres at .5)
{
    vec2 sz = vec2(textureSize(uH, 0));
    return texture(uH, (q + 0.5) / sz).x;
}
void main()
{
    vec2 p = vec2(gl_FragCoord.xy) - 0.5;   // this pixel's texel coordinate
    float s = float(pc.p.x);
    float core = edgeAt(p + vec2(0.5 * s, 1.5 * s));
    float fringe = edgeAt(p + vec2(0.5 * s, 0.5 * s));
    float a = core > 0.0 ? float(pc.p.y) * core : (fringe > 0.0 ? float(pc.p.z) * fringe : 0.0);
    if (a <= 0.0) discard;
    gl_FragStencilRefARB = core >= 0.5 ? 1 : 0;
    outColor = vec4(0.0, 0.0, 0.0, a / 255.0);
}
