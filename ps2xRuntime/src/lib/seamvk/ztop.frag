#version 450
// [seamvk] Native step 2 (sub_0024B118 first part at the engine seam): Ztop := frame.A -- the scene's per-material alpha
// copied into the top byte of the Z buffer's colour view (0x1c00), at native resolution. The game does it as 16
// full-frame sprites reading the scene as a CT32 texture; under the native renderer that read was a GS-resolution
// decode, so the plane the outline (step 5) and the material tint read was a 512x448 upscale: the "pixelated" cel
// outline ([ztopnative], 2026-09-28).
layout(set = 0, binding = 1) uniform sampler2D uFrame;   // the scene target (same size as the destination)
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate] the stored alpha's bit 7
layout(location = 0) out vec4 outColor;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(uFrame, 0);
    float a = texelFetch(uFrame, clamp(p, ivec2(0), sz - 1), 0).a;
    gl_FragStencilRefARB = a >= 128.0 / 255.0 ? 1 : 0;
    outColor = vec4(0.0, 0.0, 0.0, a);
}
