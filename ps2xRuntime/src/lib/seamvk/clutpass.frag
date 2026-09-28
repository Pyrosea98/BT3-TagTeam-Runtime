#version 450
// [clutpass] A generic native post-chain pass: the game's full-screen sprite passes over the scene / the Z buffer's colour
// view (index = the target's alpha byte, PSMT8H), or untextured, with the GS blend done per pixel at native resolution.
// Register state, vertex colour and the palette are captured by the front end at the packet's (neutralised) kick, so the
// pass IS the game's draw -- nothing is hard-coded per step. Destination read in-pass (input attachment).
layout(set = 0, binding = 0, std140) uniform Clut { uvec4 clut[64]; } uClut;
layout(set = 0, binding = 1) uniform sampler2D uSrc;   // the source target when it is not the destination (Ztop at 0x1c00)
layout(input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput uDst;
layout(push_constant) uniform PC { uvec4 a; } pc;
// a.x: src (0 none, 1 dst.a, 2 uSrc.a) | tfx << 4 | tcc << 8 | abe << 12 | ate << 13 | atst << 16 | colclamp << 20
// a.y: blend A | B << 2 | C << 4 | D << 6 | fix << 8 | aref << 16
// a.z: vertex rgba
#extension GL_ARB_shader_stencil_export : require
out int gl_FragStencilRefARB;   // [stencildate] the stored alpha's bit 7
layout(location = 0) out vec4 outColor;
uint clutEntry(uint i) { uvec4 v = uClut.clut[i >> 2]; return (i & 3u) == 0u ? v.x : (i & 3u) == 1u ? v.y : (i & 3u) == 2u ? v.z : v.w; }
void main()
{
    vec4 f = subpassLoad(uDst);
    ivec4 cd = ivec4(f * 255.0 + 0.5);
    uint src = pc.a.x & 0xFu;
    ivec4 vc = ivec4(int(pc.a.z & 0xFFu), int((pc.a.z >> 8) & 0xFFu), int((pc.a.z >> 16) & 0xFFu), int(pc.a.z >> 24));
    ivec4 cs = vc;
    if (src != 0u)
    {
        uint idx = uint(cd.a);
        if (src == 2u) { ivec2 p = ivec2(gl_FragCoord.xy); ivec2 sz = textureSize(uSrc, 0); idx = uint(texelFetch(uSrc, clamp(p, ivec2(0), sz - 1), 0).a * 255.0 + 0.5); }
        uint e = clutEntry(idx);
        ivec4 ct = ivec4(int(e & 0xFFu), int((e >> 8) & 0xFFu), int((e >> 16) & 0xFFu), int(e >> 24));
        uint tfx = (pc.a.x >> 4) & 3u; bool tcc = ((pc.a.x >> 8) & 1u) != 0u;
        if (tfx == 1u) { cs.rgb = ct.rgb; cs.a = tcc ? ct.a : vc.a; }                                   // decal
        else { cs.rgb = min((ct.rgb * vc.rgb) >> 7, ivec3(255)); cs.a = tcc ? min((ct.a * vc.a) >> 7, 255) : vc.a; }   // modulate (highlight modes: not used by these passes)
    }
    if (((pc.a.x >> 13) & 1u) != 0u)
    {   // alpha test, AFAIL KEEP
        uint atst = (pc.a.x >> 16) & 7u; int aref = int((pc.a.y >> 16) & 0xFFu); bool ok;
        switch (atst) { case 0u: ok = false; break; case 2u: ok = cs.a < aref; break; case 3u: ok = cs.a <= aref; break; case 4u: ok = cs.a == aref; break;
                        case 5u: ok = cs.a >= aref; break; case 6u: ok = cs.a > aref; break; case 7u: ok = cs.a != aref; break; default: ok = true; break; }
        if (!ok) discard;
    }
    ivec3 o = cs.rgb;
    if (((pc.a.x >> 12) & 1u) != 0u)
    {   // GS blend: ((A - B) * C >> 7) + D, C in 0..255 (0x80 = 1.0)
        uint A = pc.a.y & 3u, B = (pc.a.y >> 2) & 3u, C = (pc.a.y >> 4) & 3u, D = (pc.a.y >> 6) & 3u; int fix = int((pc.a.y >> 8) & 0xFFu);
        ivec3 ca = A == 0u ? cs.rgb : (A == 1u ? cd.rgb : ivec3(0));
        ivec3 cb = B == 0u ? cs.rgb : (B == 1u ? cd.rgb : ivec3(0));
        int cc = C == 0u ? cs.a : (C == 1u ? cd.a : fix);
        ivec3 dd = D == 0u ? cs.rgb : (D == 1u ? cd.rgb : ivec3(0));
        o = (((ca - cb) * cc) >> 7) + dd;
        o = ((pc.a.x >> 20) & 1u) != 0u ? clamp(o, ivec3(0), ivec3(255)) : (o & 0xFF);
    }
    gl_FragStencilRefARB = (cs.a >> 7) & 1;
    outColor = vec4(vec3(o) / 255.0, float(cs.a) / 255.0);
}
