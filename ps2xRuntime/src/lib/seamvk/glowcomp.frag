#version 450
// [seamvk] Native step 4, part 2 (FUN_00103070): frame.rgb := floor(f + (round(U) - f) * A / 128), U = the 2:1 buffer at
// 0x2a00 sampled bilinearly at (x/2, y/2): even pixels read the texel, odd pixels the 50/50 mix with the next (the
// oracle's exact rule), rgb only, pixels with A == 0 untouched. The destination is read in-pass (input attachment).
layout(set = 0, binding = 1) uniform sampler2D uGlow;   // the 0x2a00 target (256 x scale wide)
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;   // the scene itself
layout(location = 0) out vec4 outColor;
void main()
{
    vec4 f = subpassLoad(uDst);
    uint a = uint(f.a * 255.0 + 0.5);
    if (a == 0u) discard;
    vec2 p = vec2(gl_FragCoord.xy) - 0.5;              // native pixel index
    vec2 gs = vec2(textureSize(uGlow, 0));
    vec3 u = texture(uGlow, (p * 0.5 + 0.5) / gs).rgb;   // texel x/2 at even x, the half-way mix at odd x
    ivec3 ur = ivec3(floor(u * 255.0 + 0.5));
    ivec3 fc = ivec3(f.rgb * 255.0 + 0.5);
    ivec3 o = clamp(ivec3(floor(vec3(fc) + vec3(ur - fc) * float(a) / 128.0)), ivec3(0), ivec3(255));
    outColor = vec4(vec3(o) / 255.0, f.a);
}
