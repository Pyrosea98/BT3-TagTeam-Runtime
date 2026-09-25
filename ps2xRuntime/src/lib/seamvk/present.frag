#version 450
layout(push_constant) uniform PCP { vec4 src; vec4 dst; vec4 alpha; } pc;
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(texture(uTex, vUv).rgb, pc.alpha.x); }
