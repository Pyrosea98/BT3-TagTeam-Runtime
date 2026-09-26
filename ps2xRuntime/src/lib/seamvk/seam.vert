#version 450
// [seamvk] Native vertex path for the five BT3 VU1 programs. One draw per chunk (a tristrip).
// Vertex layout is the raw VU quadwords of the list: stride 48 (P, A1, A2) or 64 (P, A1, A2, A3).
layout(location = 0) in vec4 inP;    // position; w = weight (characters) or 1 / int flag
layout(location = 1) in vec4 inA1;   // normal (characters, effects) or float colour (stage)
layout(location = 2) in vec4 inA2;   // texcoord (characters, stage) or colour (effects)
layout(location = 3) in vec4 inA3;   // texcoord (effects only)

layout(std140, set = 0, binding = 0) uniform Consts
{
    vec4 A[4];      // bone A / M1 (screen)
    vec4 B[4];      // bone B / M2 (clip)
    vec4 E[4];      // screen projection (characters)
    vec4 F[4];      // clip projection (characters)
    vec4 C[4];      // lit normal A (3b5dfe97) / M4 (effects)
    vec4 D[4];      // lit normal B (3b5dfe97) / light basis (ccb6aa07)
    vec4 pivA, pivB;
    vec4 colA, colB;   // constant colours (0..128)
    vec4 misc;         // x = program, y = stride, z = qw16.x scale (effects)
    vec4 view;         // ofx, ofy, width, height in GS pixels
} c;

#include "pc.glsl"
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vTex;   // (s, t, 1): perspective-interpolated by the hardware
layout(location = 2) out float vLit;
layout(location = 3) out float vFog;

vec4 xf1(vec4 r[4], vec3 v) { return r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3]; }          // MADDw with vf00.w = 1
vec4 xfw(vec4 r[4], vec4 v) { return r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3] * v.w; }    // MADDw with v.w

void main()
{
    const int prog = int(c.misc.x);
    vec4 screen;      // GS pixel space * w
    vec3 tex = vec3(0.0);
    vec4 col = vec4(1.0);
    float lit = 1.0;
    if (prog == 0)
    {   // stage: P, float RGBA, T; screen = M1 * P (P.w term)
        screen = xfw(c.A, inP);
        col = inA1 / 255.0;
        tex = inA2.xyz;
    }
    else if (prog == 1)
    {   // effects: P (w = flag), N, colour, T; screen = M1 * P (w = 1); texcoords generated
        screen = xf1(c.A, inP.xyz);
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
        tex = inA2.xyz;
        col = c.colA / 255.0;
        if (prog == 2)
        {   // 3b5dfe97: lit = blend of C*N and D*N by weight, then 0.5*lit+0.5 (toon lookup)
            vec3 na = xf1(c.C, inA1.xyz).xyz, nb = xf1(c.D, inA1.xyz).xyz;
            vec3 n = nb + (na - nb) * inP.w;
            lit = n.x * 0.5 + 0.5;
        }
        else if (prog == 4)
        {   // ccb6aa07: L*N, 2D toon lookup ((1+l.x)/2, (1+l.y)/2)
            vec3 n = xf1(c.D, inA1.xyz).xyz;
            lit = (1.0 + n.x) * 0.5;
        }
    }
    // GS pixel space -> NDC over the game's window (ofx, ofy, w, h), perspective-correct.
    // The VU1 programs' projection gives visible vertices a NEGATIVE w (they only ever take 1/w). Negate the whole
    // vector: visible vertices get w > 0 and vertices behind the camera get w < 0, which Vulkan clips. (Flipping
    // only when w < 0 left behind-camera vertices unclipped: screen-filling triangles whenever a chunk crossed the near plane.)
    screen = -screen;
    float w = screen.w;
    vec2 px = screen.xy;                       // still multiplied by w
    vec2 ndc = vec2((px.x - pc.view.x * w) / pc.view.z * 2.0 - w,
                    (px.y - pc.view.y * w) / pc.view.w * 2.0 - w);
    // Depth exactly as the GS receives it: FTOI4(z * Q), then bits 4..27 as an unsigned 24-bit value.
    float zq = screen.z / w;
    int zi = int(trunc(zq * 16.0));
    float gsz = float((zi >> 4) & 0xFFFFFF);
    float depth = gsz / 16777216.0;
    gl_Position = vec4(ndc, depth * w, w);
    vColor = col;
    vTex = vec3(tex.xy, 1.0);
    vLit = lit;
    vFog = 1.0;
}
