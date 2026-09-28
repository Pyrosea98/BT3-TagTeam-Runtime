#version 450
// [seamvk] Native step 4, part 1 (FUN_00103070): the glow / DoF source: 0x2a00 (fbw 4) := 2:1 box downscale of the scene,
// alpha 0x80 -- (sum + 2) / 4 per channel, as the oracle pinned it. At native resolution: each output pixel averages the
// 2x2 native block of the scene target (the game's 32 strips read the scene bilinearly at u = 2x, v = 2y).
layout(set = 0, binding = 1) uniform sampler2D uScene;
layout(location = 0) out vec4 outColor;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy) * 2; ivec2 sz = textureSize(uScene, 0);
    uvec3 s = uvec3(0u);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            s += uvec3(texelFetch(uScene, clamp(p + ivec2(i, j), ivec2(0), sz - 1), 0).rgb * 255.0 + 0.5);
    outColor = vec4(vec3((s + 2u) / 4u) / 255.0, 128.0 / 255.0);
}
