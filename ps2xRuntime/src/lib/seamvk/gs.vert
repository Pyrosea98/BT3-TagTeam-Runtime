#version 450
// [seamvk] GS draws: vertices already in screen space (after XYOFFSET), depth normalised.
layout(location = 0) in vec4 inPos;   // x, y (GS pixels), z (0..1), q
layout(location = 1) in vec4 inTex;   // s, t, u, v
layout(location = 2) in vec4 inCol;   // rgba bytes / 255
layout(location = 3) in float inFog;
#include "pc.glsl"
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vTex;   // (s, t, q) or (u, v, 1)
layout(location = 2) out float vLit;
layout(location = 3) out float vFog;
void main()
{
    vec2 ndc = vec2(inPos.x / pc.view.z * 2.0 - 1.0, inPos.y / pc.view.w * 2.0 - 1.0);
    gl_Position = vec4(ndc, inPos.z, 1.0);
    vColor = inCol;
    vTex = ((pc.fA.x & 2) != 0) ? vec3(inTex.z, inTex.w, 1.0) : vec3(inTex.x, inTex.y, inPos.w);
    vLit = 1.0;
    vFog = inFog;
}
