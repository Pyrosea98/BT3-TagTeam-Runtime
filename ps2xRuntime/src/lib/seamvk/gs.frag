#version 450
// [seamvk] one fragment shader for GS draws (NOPERSP: screen-linear s,t,q like the GS) and seam draws.
#ifdef NOPERSP
#define INTERP noperspective
#else
#define INTERP
#endif
layout(location = 0) INTERP in vec4 vColor;
layout(location = 1) INTERP in vec3 vTex;
layout(location = 2) INTERP in float vLit;
layout(location = 3) INTERP in float vFog;
layout(set = 0, binding = 1) uniform sampler2D uTex;   // RGBA8, raw GS alpha bytes / 255
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;   // the destination itself (DATE), alpha stored as A/128
#include "pc.glsl"
layout(location = 0, index = 0) out vec4 outColor;   // stored: exact GS bytes / 255
layout(location = 0, index = 1) out vec4 outBlend;   // dual-source: alpha = As / 128 for the blend factors

ivec2 wrapT(ivec2 t)
{
    ivec2 size = ivec2(pc.texInfo.xy);
    int wms = pc.fA.z & 3, wmt = (pc.fA.z >> 2) & 3;
    if (wms == 0) t.x = t.x & (size.x - 1);
    else if (wms == 1) t.x = clamp(t.x, 0, size.x - 1);
    else if (wms == 2) t.x = clamp(t.x, pc.fB.x, pc.fB.y);
    else t.x = (t.x & pc.fB.x) | pc.fB.y;
    if (wmt == 0) t.y = t.y & (size.y - 1);
    else if (wmt == 1) t.y = clamp(t.y, 0, size.y - 1);
    else if (wmt == 2) t.y = clamp(t.y, pc.fB.z, pc.fB.w);
    else t.y = (t.y & pc.fB.z) | pc.fB.w;
    return clamp(t, ivec2(0), size - 1);
}
vec4 fetchT(ivec2 t) { return texelFetch(uTex, wrapT(t), 0); }

void main()
{
    int flags = pc.fA.x;
    if ((flags & 512) != 0)
    {   // DATE: draw only where the destination alpha's bit 7 equals DATM (stored A/128 saturates at 1.0 for A >= 128)
        float da = subpassLoad(uDst).a;
        bool bit7 = da >= (127.5 / 255.0);
        bool datePass = bit7 == ((flags & 1024) != 0);
        if ((flags & 4096) != 0) { if (datePass) discard; outColor = vec4(1.0, 0.0, 0.0, 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); return; }   // PS2X_SEAMVK_DATEDBG=2: paint where the test FAILS
        if (!datePass) discard;
        if ((flags & 2048) != 0) { outColor = vec4(0.0, 1.0, 0.0, 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); return; }   // PS2X_SEAMVK_DATEDBG=1: paint where the test passes
    }
    vec4 cf = vColor;
    vec4 c = cf;
    if ((flags & 1) != 0)
    {
        vec2 uv = ((flags & 2) != 0) ? vTex.xy : (vTex.xy / vTex.z) * pc.texInfo.xy;
        vec4 ct;
        if ((flags & 8) != 0)
        {   // bilinear, GS convention: the sample point is uv - 0.5
            vec2 p = uv - 0.5;
            ivec2 i = ivec2(floor(p));
            vec2 f = p - vec2(i);
            vec4 c00 = fetchT(i), c10 = fetchT(i + ivec2(1, 0)), c01 = fetchT(i + ivec2(0, 1)), c11 = fetchT(i + ivec2(1, 1));
            ct = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
        }
        else ct = fetchT(ivec2(floor(uv)));
        int tfx = pc.fA.y;
        bool tcc = (flags & 128) != 0;
        vec3 cfs = cf.rgb * (255.0 / 128.0);   // vertex colour 0x80 = 1.0
        float afs = cf.a * (255.0 / 128.0);
        if (tfx == 0)      { c.rgb = ct.rgb * cfs;        c.a = tcc ? ct.a * afs : cf.a; }   // modulate
        else if (tfx == 1) { c.rgb = ct.rgb;              c.a = tcc ? ct.a : cf.a; }         // decal
        else if (tfx == 2) { c.rgb = ct.rgb * cfs + cf.a; c.a = tcc ? ct.a + cf.a : cf.a; }  // highlight
        else               { c.rgb = ct.rgb * cfs + cf.a; c.a = tcc ? ct.a : cf.a; }         // highlight2
        c = clamp(c, 0.0, 1.0);
    }
    c.rgb *= mix(0.55, 1.0, clamp(vLit, 0.0, 1.0));
    if ((flags & 256) != 0) c.rgb = mix(pc.fogcol.rgb, c.rgb, vFog);
    float a255 = c.a * 255.0;
    if ((flags & 8192) != 0) { outColor = vec4(vec3(a255 / 255.0), 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); return; }   // PS2X_SEAMVK_DATEDBG=3: the sampled alpha as grey, opaque
    if ((flags & 16) != 0)
    {   // alpha test on the GS byte alpha
        int atst = pc.fA.w & 7, aref = (pc.fA.w >> 3) & 0xFF, afail = (pc.fA.w >> 11) & 3;
        int a = int(a255 + 0.5);
        bool pass = true;
        if (atst == 0) pass = false;
        else if (atst == 2) pass = a < aref;
        else if (atst == 3) pass = a <= aref;
        else if (atst == 4) pass = a == aref;
        else if (atst == 5) pass = a >= aref;
        else if (atst == 6) pass = a > aref;
        else if (atst == 7) pass = a != aref;
        if (!pass && afail == 0) discard;   // KEEP; FB_ONLY / ZB_ONLY / RGB_ONLY approximated as pass
    }
    // FBA forces bit 7 of the alpha WRITTEN to the frame; the blend factor As is the source alpha before that (the HUD's
    // additive flashes carry vertex alpha 0 with FBA set: they add nothing but leave the mask bit behind).
    float aStored = ((flags & 32) != 0 && a255 < 128.0) ? a255 + 128.0 : a255;
    outColor = vec4(c.rgb, aStored / 255.0);
    float fac = a255 / 128.0;
    if ((flags & 16384) != 0) fac = subpassLoad(uDst).a * (255.0 / 128.0);   // Ad: the destination alpha, read in-pass like DATE (was Ad/255)
    outBlend = vec4(0.0, 0.0, 0.0, min(fac, 1.0));
}
