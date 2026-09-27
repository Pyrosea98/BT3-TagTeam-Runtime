#version 450
// [seamvk] Native vertex path for the five BT3 VU1 programs. One draw per chunk (a tristrip).
// Vertex layout is the raw VU quadwords of the list: stride 48 (P, A1, A2) or 64 (P, A1, A2, A3).
layout(location = 0) in vec4 inP;    // position; w = weight (characters) or 1 / int flag
layout(location = 1) in vec4 inA1;   // normal (characters, effects) or float colour (stage)
layout(location = 2) in vec4 inA2;   // texcoord (characters, stage) or colour (effects)
layout(location = 3) in vec4 inA3;   // texcoord (effects only)

struct Consts
{
    vec4 A[4];      // bone A / M1 (screen)
    vec4 B[4];      // bone B / M2 (clip)
    vec4 E[4];      // screen projection (characters)
    vec4 F[4];      // clip projection (characters)
    vec4 C[4];      // lit normal A (3b5dfe97) / M4 (effects)
    vec4 D[4];      // lit normal B (3b5dfe97) / light basis (ccb6aa07)
    vec4 pivA, pivB;
    vec4 colA, colB;   // constant colours (0..128)
    vec4 misc;         // x = program, y = pass index within the packet, z = qw16.x scale (effects), w = flat-colour debug
    vec4 view;         // ofx, ofy, width, height in GS pixels
    vec4 range;        // x = first vertex index of this chunk within the batch
};
// [batch] up to 128 chunks share one draw: each vertex carries its chunk's index in inA3.w (pc.texInfo.z = 1), so the
// per-chunk constants (bone matrices, pass index) come from this array instead of one draw per VU chunk.
layout(std430, set = 0, binding = 0) readonly buffer ConstsArr { Consts arr[]; } ca;

#include "pc.glsl"
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vTex;   // (s, t, 1): perspective-interpolated by the hardware
layout(location = 2) out float vLit;
layout(location = 3) out float vFog;
out float gl_ClipDistance[2];

vec4 xf1(vec4 r[4], vec3 v) { return r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3]; }          // MADDw with vf00.w = 1
vec4 xfw(vec4 r[4], vec4 v) { return r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3] * v.w; }    // MADDw with v.w

void main()
{
    // [batch] the chunk this vertex belongs to: the last chunk whose range.x <= gl_VertexIndex (pc.texInfo.w = chunk count)
    int ci = 0;
    if (pc.texInfo.z > 0.5)
    {
        int lo = 0, hi = int(pc.texInfo.w + 0.5) - 1;
        const float vi = float(gl_VertexIndex);
        while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (vi >= ca.arr[mid].range.x) lo = mid; else hi = mid - 1; }
        ci = lo;
    }
#define c ca.arr[ci]
    const int prog = int(c.misc.x);
    vec4 screen;      // GS pixel space * w
    vec4 clipv;       // the program's clip-space position (M2 / F): the VU1 clips |x|,|y|,|z| <= |w| here
    vec3 tex = vec3(0.0);
    vec4 col = vec4(1.0);
    float lit = 1.0;
    if (prog == 0)
    {   // stage: P, float RGBA, T; screen = M1 * P (P.w term)
        screen = xfw(c.A, inP);
        clipv = xfw(c.B, inP);
        col = inA1 / 255.0;
        tex = inA2.xyz;
    }
    else if (prog == 1)
    {   // effects: P (w = flag), N, colour, T; screen = M1 * P (w = 1); texcoords generated
        screen = xf1(c.A, inP.xyz);
        clipv = xf1(c.B, inP.xyz);
        col = inA2 / 255.0;
        vec4 p4 = xf1(c.C, inP.xyz);
        tex = vec3((p4.x * c.misc.z + 1.0) * 0.5, (p4.y * c.misc.z + 1.0) * 0.5, 1.0);
    }
    else
    {   // characters: two-bone skin about the pivots, then E
        vec3 pa = xf1(c.A, inP.xyz - c.pivA.xyz).xyz;
        vec3 pb = xf1(c.B, inP.xyz - c.pivB.xyz).xyz;
        vec3 skinned = pb + (pa - pb) * inP.w;
        screen = xf1(c.E, skinned);
        clipv = xf1(c.F, skinned);
        tex = inA2.xyz;
        col = c.colA / 255.0;
        if (prog == 2)
        {   // 3b5dfe97, two passes of the same strip: pass 0 = model texture with colour A; pass 1 = the toon ramp, sampled at
            // s = 0.5 * lit + 0.5 (lit = the blend of C*N and D*N by the skin weight, x lane), t = 0, with colour B.
            vec3 na = xf1(c.C, inA1.xyz).xyz, nb = xf1(c.D, inA1.xyz).xyz;
            vec3 n = nb + (na - nb) * inP.w;
            float ramp = n.x * 0.5 + 0.5;
            if (c.misc.y > 0.5) { tex = vec3(ramp, 0.0, 1.0); col = c.colB / 255.0; }
            // lit stays 1: the toon look IS pass 1 (the ramp texel subtracted from pass 0); the fragment shader's
            // extra darkening by lit was a stand-in from before pass 1 existed and made the cel shading a gradient.
        }
        else if (prog == 4)
        {   // ccb6aa07: L*N, 2D toon lookup ((1+l.x)/2, (1+l.y)/2)
            vec3 n = xf1(c.D, inA1.xyz).xyz;
            lit = (1.0 + n.x) * 0.5;
        }
    }
    // The programs only ever take 1/w, so their projections' w sign is a convention per program (measured with the
    // PS2X_SEAMVK_DUMPFRAME statistics): the stage, effects and two-pass character programs give visible vertices a
    // POSITIVE w, the single-pass character program a NEGATIVE one. Negate that one so visible vertices have w > 0 and
    // vertices behind the camera w < 0, which Vulkan clips. (Flipping per vertex on the sign of w is wrong: it makes
    // behind-camera vertices visible, mirrored, and gives screen-filling triangles whenever a chunk crosses the camera.)
    // Exception: an orthographic screen matrix (rows 0..2 have no w term, w = row 3's constant) has no behind-camera
    // side, and its sign is whatever the game stored: the character silhouette passes (single-pass program into the
    // 256x256 shadow target, w = +862) would otherwise be negated into w < 0 and clipped away entirely.
    vec4 M3 = prog == 0 || prog == 1 ? c.A[3] : c.E[3];
    vec3 Mw = prog == 0 || prog == 1 ? vec3(c.A[0].w, c.A[1].w, c.A[2].w) : vec3(c.E[0].w, c.E[1].w, c.E[2].w);
    bool ortho = all(equal(Mw, vec3(0.0)));
    if (ortho ? (M3.w < 0.0) : (prog == 3)) screen = -screen;
    float w = screen.w;
    vec2 px = screen.xy;                       // still multiplied by w
    vec2 ndc = vec2((px.x - pc.view.x * w) / pc.view.z * 2.0 - w,
                    (px.y - pc.view.y * w) / pc.view.w * 2.0 - w);
    // The VU1 program's own clip volume: a strip triangle with a vertex outside |z| <= |w| of the clip matrix is not
    // drawn from the strip (the clipper re-emits its clipped fan, which the front-end drops in favour of this mesh),
    // so clip this mesh against those two planes exactly, in homogeneous space, where the interpolation is right.
    // (x/y are left to Vulkan's own frustum from the screen projection; the game's x/y planes are a guard band.)
    float cw = abs(clipv.w);
    gl_ClipDistance[0] = cw - clipv.z;
    gl_ClipDistance[1] = cw + clipv.z;
    if (prog == 3)
    {   // the single-pass character program computes the clip flags but never acts on them (its FCAND branch skips a
        // NOP): nothing is dropped. The shadow silhouette pass relies on that: its orthographic matrix puts z at -9e6
        // against w = 862, and the VU1 draws it anyway.
        gl_ClipDistance[0] = 1.0; gl_ClipDistance[1] = 1.0;
    }
    // Depth: the GS receives FTOI4(z * Q) >> 4 as an unsigned 24-bit value per vertex and interpolates it linearly in
    // screen space; Vulkan interpolates z/w linearly in screen space too, so gl_Position.z = gsz / 2^24 * w reproduces it.
    // FTOI4 saturates, so z past the far plane is clamped, and z < 0 (the sky dome, drawn without depth writes) becomes
    // 0, never a Vulkan-clipped vertex. Vertices behind the camera (w <= 0) keep the linear value screen.z / 2^24 so the
    // clipped edge lands where the plane really is; the clip planes above remove them before they can matter.
    float zc = screen.z / 16777216.0;
    if (w > 0.0)
    {
        float zq = clamp(screen.z / w, 0.0, 16777215.0);
        int zi = int(trunc(zq * 16.0));
        zc = float((zi >> 4) & 0xFFFFFF) / 16777216.0 * w;
    }
    gl_Position = vec4(ndc, zc, w);
    if (c.misc.w > 0.5) col = prog == 0 ? vec4(1.0, 0.3, 0.0, 1.0) : prog == 1 ? vec4(0.0, 0.0, 1.0, 1.0) : prog == 2 ? vec4(0.0, 1.0, 0.0, 1.0) : prog == 3 ? vec4(1.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 1.0, 1.0);
    vColor = col;
    vTex = vec3(tex.xy, 1.0);
    vLit = lit;
    vFog = 1.0;
}
