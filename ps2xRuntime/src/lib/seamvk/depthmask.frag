#version 450
// [seamvk] Native depth-mask pass (the post chain's sub_00109848 at the engine seam): frame.A := Z24[15:8], pinned exact
// by the packet oracle. The game does it as 64 sprites through a PSMCT16 view of the frame sampling a PSMZ16 view of
// the Z buffer; here it is one pass reading the depth image.
layout(set = 0, binding = 1) uniform sampler2D uDepth;   // D32: Z24 / 2^24
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate]
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform P { uint dofOff; } pc;   // [dofoff] 1: the "Depth-of-Field Blur" switch is off
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float z = texelFetch(uDepth, p, 0).r;
    uint zi = uint(clamp(z, 0.0, 1.0) * 16777216.0 + 0.5) & 0xFFFFFFu;
    // [dofoff] DoF off = what paraLLEl-GS does with the game's own mask sprites (ps2_gs_pgs.cpp, PS2X_PGS_DOFMODE 2:
    // TCC cleared, vertex alpha 0x80): the mask reads "near" everywhere, so the blur composite blends nothing in.
    if (pc.dofOff != 0u) zi = 0x8000u;
    gl_FragStencilRefARB = int((zi >> 15) & 1u);
    outColor = vec4(0.0, 0.0, 0.0, float((zi >> 8) & 0xFFu) / 255.0);
}
