#version 450
// [seamvk] one fragment shader for GS draws (NOPERSP: screen-linear s,t,q like the GS) and seam draws.
#ifdef NOPERSP
#define INTERP noperspective
#else
#define INTERP
#endif
#ifdef STENCIL_EXPORT
// [stencildate] the stencil buffer mirrors bit 7 of the STORED alpha of every pixel: a draw that writes alpha exports it here
// (REPLACE), so a DATE draw is a plain stencil test instead of an in-pass barrier + destination read per draw.
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;
#define STENCIL_OUT(a) gl_FragStencilRefARB = ((a) >= 128.0) ? 1 : 0
#else
#define STENCIL_OUT(a)
#endif
layout(location = 0) INTERP in vec4 vColor;
layout(location = 1) INTERP in vec3 vTex;
layout(location = 2) INTERP in float vLit;
layout(location = 3) INTERP in float vFog;
layout(location = 4) flat in vec4 vRect;   // [spriterect] the sprite's texel rect, x < 0 = none
layout(set = 0, binding = 1) uniform sampler2D uTex;   // RGBA8, raw GS alpha bytes / 255
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;   // the destination itself (DATE), alpha stored as A/128
#include "pc.glsl"
layout(depth_less) out float gl_FragDepth;   // [zquant] the depth only ever decreases (truncation), so early-Z can still reject
layout(location = 0, index = 0) out vec4 outColor;   // stored: exact GS bytes / 255
layout(location = 0, index = 1) out vec4 outBlend;   // dual-source: alpha = As / 128 for the blend factors

ivec2 wrapT(ivec2 t)
{
    int nat = max(pc.misc.x, 1);   // [rtnative] texel coordinates are in native texels: the GS sizes and region bounds scale
    ivec2 size = ivec2(pc.texInfo.xy) * nat;
    int wms = pc.fA.z & 3, wmt = (pc.fA.z >> 2) & 3;
    if (wms == 0) t.x = t.x & (size.x - 1);
    else if (wms == 1) t.x = clamp(t.x, 0, size.x - 1);
    else if (wms == 2) t.x = clamp(t.x, pc.fB.x * nat, pc.fB.y * nat + nat - 1);
    else t.x = ((t.x / nat & pc.fB.x) | pc.fB.y) * nat + (t.x % nat);
    if (wmt == 0) t.y = t.y & (size.y - 1);
    else if (wmt == 1) t.y = clamp(t.y, 0, size.y - 1);
    else if (wmt == 2) t.y = clamp(t.y, pc.fB.z * nat, pc.fB.w * nat + nat - 1);
    else t.y = ((t.y / nat & pc.fB.z) | pc.fB.w) * nat + (t.y % nat);
    return clamp(t, ivec2(0), size - 1);
}
vec4 fetchT(ivec2 t) { return texelFetch(uTex, wrapT(t), 0); }

void main()
{
    // [zquant] truncate the interpolated depth to the Z buffer's integer step, as the GS compares integers (pc.fogcol.w = 2^24 for Z24, 0 = off)
    gl_FragDepth = pc.fogcol.w > 0.0 ? floor(gl_FragCoord.z * pc.fogcol.w) / pc.fogcol.w : gl_FragCoord.z;
    int flags = pc.fA.x;
    if ((flags & 512) != 0)
    {   // DATE: draw only where the destination alpha's bit 7 equals DATM (stored A/128 saturates at 1.0 for A >= 128)
        float da = subpassLoad(uDst).a;
        bool bit7 = da >= (127.5 / 255.0);
        bool datePass = bit7 == ((flags & 1024) != 0);
        if ((flags & 4096) != 0) { if (datePass) discard; outColor = vec4(1.0, 0.0, 0.0, 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); STENCIL_OUT(255.0); return; }   // PS2X_SEAMVK_DATEDBG=2: paint where the test FAILS
        if (!datePass) discard;
        if ((flags & 2048) != 0) { outColor = vec4(0.0, 1.0, 0.0, 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); STENCIL_OUT(255.0); return; }   // PS2X_SEAMVK_DATEDBG=1: paint where the test passes
    }
    vec4 cf = vColor;
    vec4 c = cf;
    if ((flags & 1) != 0)
    {
        vec2 uv = ((flags & 2) != 0) ? vTex.xy : (vTex.xy / vTex.z) * pc.texInfo.xy;
        float natf = float(max(pc.misc.x, 1)); uv *= natf;   // [rtnative] into native texels
        vec4 rectN = vRect * natf;
        // [spriterect] the whole bilinear footprint (uv - 0.5 .. uv + 0.5) stays inside the primitive's own texels: a quad whose v runs
        // 0.5..64.5 over 64 rows samples p = v - 0.5 up to 63.75 at render scale 2 -- 75 % of row 64, which REPEAT wraps to row 0
        // (the HUD plate's light top edge showed as a 1-px line under the ki gauge). Clamp uv to [rmin, rmax - 1].
        if (vRect.x >= 0.0 && (flags & (32768 | 8)) != 0) uv = min(max(uv, rectN.xy), max(rectN.zw - 1.0, rectN.xy));
        vec4 ct;
        if ((flags & 32768) != 0) ct = texture(uTex, uv / (pc.texInfo.xy * natf));   // [hwfilter] plain REPEAT/CLAMP bilinear through the sampler: hardware samples texel index uv - 0.5 at coordinate uv / size, the GS convention
        else if ((flags & 8) != 0)
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
    if ((flags & 8192) != 0) { outColor = vec4(vec3(a255 / 255.0), 1.0); outBlend = vec4(0.0, 0.0, 0.0, 1.0); STENCIL_OUT(255.0); return; }   // PS2X_SEAMVK_DATEDBG=3: the sampled alpha as grey, opaque
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
    STENCIL_OUT(aStored);
    outColor = vec4(c.rgb, aStored / 255.0);
    float fac = a255 / 128.0;
    if ((flags & 16384) != 0) fac = subpassLoad(uDst).a * (255.0 / 128.0);   // Ad: the destination alpha, read in-pass like DATE (was Ad/255)
    if ((flags & 65536) != 0)
    {   // [shaderblend] a GS blend (A - B) * C + D the fixed-function factors cannot express (e.g. (1,2,0,1) = Cd * As + Cd,
        // the aura / ki-charge glow sprites): computed here against the destination read in-pass (the draw got the same
        // by-region barrier as a DATE draw) and written with hardware blending OFF. Before this, such draws were drawn
        // opaque -- the glow's black texture square around the character.
        vec4 dst = subpassLoad(uDst);
        int aA = pc.blend.y & 3, aB = (pc.blend.y >> 2) & 3, aC = (pc.blend.y >> 4) & 3, aD = (pc.blend.y >> 6) & 3;
        vec3 A = aA == 0 ? c.rgb : aA == 1 ? dst.rgb : vec3(0.0);
        vec3 B = aB == 0 ? c.rgb : aB == 1 ? dst.rgb : vec3(0.0);
        float C = aC == 0 ? a255 / 128.0 : aC == 1 ? dst.a * (255.0 / 128.0) : float(pc.blend.z) / 128.0;
        vec3 D = aD == 0 ? c.rgb : aD == 1 ? dst.rgb : vec3(0.0);
        vec3 res = (A - B) * C + D;
        outColor.rgb = pc.blend.w != 0 ? clamp(res, 0.0, 1.0) : fract(res);   // COLCLAMP: saturate, else wrap
    }
    outBlend = vec4(0.0, 0.0, 0.0, min(fac, 1.0));
}
