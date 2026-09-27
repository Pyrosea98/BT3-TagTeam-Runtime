#version 450
// [seamvk] CRTC circuit blit: a quad covering dst (NDC) sampling src (normalised) of the display target.
layout(push_constant) uniform PCP { vec4 src; vec4 dst; vec4 alpha; vec4 taps; } pc;
layout(location = 0) out vec2 vUv;
void main()
{
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vUv = pc.src.xy + corner * pc.src.zw;
    gl_Position = vec4(mix(pc.dst.xy, pc.dst.zw, corner), 0.0, 1.0);
}
