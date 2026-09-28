#version 450
// [seamvk] Native post-chain passes into the scene target, at native resolution (frame = the scene, Ztop = the Z
// buffer's colour view at 0x1c00, written by native step 2):
//   mode 0 -- step 1 (FUN_00106ba8): the outline INK draw then the Kaioken body mask, as the packet oracle + kick transcript
//             read them: 16 sprites, ALPHA (2,0,1,1) COLCLAMP: frame.rgb := max(rgb - ink * A >> 7, 0), A := 0x80, then
//             16 alpha-only sprites through TEX0 0x1c00 PSMT8H / CLUT 0x3e90: frame.A := CLUT_0x3e90[Ztop].A (the palette is
//             all zero outside Kaioken -> A := 0, the "alpha clear" this pass was first taken for; in Kaioken entries 0..7 and
//             16..22 hold 0x80 -> the body's depth band -> the tint draw's Ad).
//   mode 1 -- step 3 (FUN_00245a50): frame.A := CLUT_0x3e84[Ztop].A, the depth -> blur-weight ramp (oracle: exact)
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(set = 0, binding = 1) uniform sampler2D uZtop;
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;   // the scene itself (mode 0)
layout(push_constant) uniform PC { uvec4 p; } pc;   // x: mode, y: ink rgb (0xRRGGBB)
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate] the stored alpha's bit 7
layout(location = 0) out vec4 outColor;
uint clutEntry(uint i) { uvec4 v = uClut.clut[i >> 2]; return (i & 3u) == 0u ? v.x : (i & 3u) == 1u ? v.y : (i & 3u) == 2u ? v.z : v.w; }
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy); ivec2 sz = textureSize(uZtop, 0);
    uint zt = uint(texelFetch(uZtop, clamp(p, ivec2(0), sz - 1), 0).a * 255.0 + 0.5);
    uint a = clutEntry(zt) >> 24;
    vec3 rgb = vec3(0.0);
    if (pc.p.x == 0u)
    {
        vec4 f = subpassLoad(uDst);
        ivec3 fc = ivec3(f.rgb * 255.0 + 0.5);
        int ad = int(f.a * 255.0 + 0.5);
        ivec3 ink = ivec3(int((pc.p.y >> 16) & 0xFFu), int((pc.p.y >> 8) & 0xFFu), int(pc.p.y & 0xFFu));
        rgb = vec3(max(fc - ((ink * ad) >> 7), ivec3(0))) / 255.0;
    }
    gl_FragStencilRefARB = int((a >> 7) & 1u);
    outColor = vec4(rgb, float(a) / 255.0);
}
