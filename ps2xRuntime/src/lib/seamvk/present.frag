#version 450
// [seamvk] CRTC circuit blit. [outscale] When the display target is larger than the output image (render scale above
// the output scale), one output pixel covers taps.z x taps.z target pixels: average a K x K grid of bilinear samples
// over that footprint (taps.xy = the footprint in uv) -- a box-filtered downsample, the supersampling paraLLEl-GS's
// scanout resolve gives. K = 1 is the plain blit.
layout(push_constant) uniform PCP { vec4 src; vec4 dst; vec4 alpha; vec4 taps; } pc;
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main()
{
    int k = int(pc.taps.z + 0.5);
    vec3 c = vec3(0.0);
    if (k <= 1) c = texture(uTex, vUv).rgb;
    else
    {
        for (int j = 0; j < k; ++j)
            for (int i = 0; i < k; ++i)
                c += texture(uTex, vUv + ((vec2(float(i), float(j)) + 0.5) / float(k) - 0.5) * pc.taps.xy).rgb;
        c /= float(k * k);
    }
    outColor = vec4(c, pc.alpha.x);
}
