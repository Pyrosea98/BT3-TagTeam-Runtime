#version 450
layout(push_constant) uniform PC { vec4 uv; vec4 rect; vec4 options; vec4 gauges; vec4 detail; } pc;
layout(location=0) out vec2 texcoord;
void main() {
    vec2 corner=vec2(float(gl_VertexIndex&1),float((gl_VertexIndex>>1)&1));
    texcoord=mix(pc.uv.xy,pc.uv.zw,corner);
    gl_Position=vec4(mix(pc.rect.xy,pc.rect.zw,corner),0,1);
}
